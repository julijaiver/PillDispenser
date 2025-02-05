#include <stdio.h>
#include "pico/stdlib.h"
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <sys/unistd.h>
#include "pico/util/queue.h"
#include "pico/time.h"
#include "eeprom_log.h"
#include "lora_connect.h"
#include "shared_structs.h"

void initialize_i2c(void);
void initialize_controller(uint controller);
void rotate_one_compartment(device *device);
void move_one_step(device *device);
void check_for_edge(bool rising_edge, device *device);
void perform_calib(device *device);
int check_pressed(int button);
void initialize_button (int button);
void initialize_led(int led);
void blink_led(int led, uint delay);
void led_bright(int led);
bool detect_pill();
bool check_pill_dispensed(void);
void led_off(int led);
void recovery_calib(device *device);
uint16_t read_steps_per_revolution_from_eeprom();
void write_steps_per_revolution_to_eeprom(uint16_t revolution);
int check_power_cut(device *device, messaging *messaging_values);
void set_boot(int state, device *device);
void remove_events();

#define UART_BAUDRATE 9600
#define I2C_BAUDRATE 100000   // 100kHz baudrate for eeprom
#define HIGH 1
#define LOW 0
#define TOTAL_STEP 8
#define COILS 4
#define CHANGE_SPEED 1
#define IN1 2
#define IN2 3
#define IN3 6
#define IN4 13
#define OPTO_FORK 28
#define TRIAL 1
#define MAX_SIZE 256
#define EQUIP_INACCURACY 130
#define DELAY 50
#define SW_1 8
#define SW_2 7
#define LED 22
#define TX_PIN 4
#define RX_PIN 5
#define BLINK_WAIT 500
#define DAYS 7
#define PIEZO_SENSOR 27
#define MAX_QUEUE 100
#define FALL_TIME 100 //calculated what is the maximum time needed in theory for a pill to drop. t= sqrt((2*0.035)/9.8) = 0.085 s.
#define EQUIP_INACCURACY_REVERSE 207
#define ADDRESS_FOR_DAY 0x0802
#define ADDRESS_FOR_STEP 0x0803
#define ADDRESS_BOOT_STATUS 0X0806
#define UN_BOOT 0
#define TIME_SLEEP 30000 // Pills dispensed every 30 seconds

typedef enum {
    INITIAL_STATE = 0,
    SW1_PRESSED = 1,
    SW2_PRESSED = 2,
    PILL_DISPENSED = 3,
    LED_ON = 4,
} Event;

static queue_t events;
static uint last_event_time = 0;

//function for interrupts and events
static void gpio_handler(uint gpio, uint32_t event) {
    uint64_t current_time = time_us_64();
    uint64_t elapsed_time = current_time - last_event_time;

    if (elapsed_time > 50000) {  //debounce
        last_event_time = current_time;

        if (gpio == SW_1) {
            Event sw1_pressed = SW1_PRESSED;
            queue_try_add(&events, &sw1_pressed);
        } else if (gpio == SW_2) {
            Event sw2_pressed = SW2_PRESSED;
            queue_try_add(&events, &sw2_pressed);
        }
    }
    if (gpio == PIEZO_SENSOR) {
        Event pill_detected = PILL_DISPENSED;
        queue_try_add(&events, &pill_detected);
    }
}

//set driving sequence for half stepping
static const uint half_stepping[TOTAL_STEP][COILS] = {
    {HIGH, LOW, LOW, LOW},
    {HIGH, HIGH, LOW, LOW},
    {LOW, HIGH, LOW, LOW},
    {LOW, HIGH, HIGH, LOW},
    {LOW, LOW, HIGH, LOW},
    {LOW, LOW, HIGH, HIGH},
    {LOW, LOW, LOW, HIGH},
    {HIGH, LOW, LOW, HIGH}
    };

int main() {
    timer_hw->dbgpause = 0;
    // Initialize motor controllers and opto fork
    initialize_controller(IN1);
    initialize_controller(IN2);
    initialize_controller(IN3);
    initialize_controller(IN4);

    gpio_init(OPTO_FORK);
    gpio_set_dir(OPTO_FORK, GPIO_IN);
    gpio_pull_up(OPTO_FORK);

    //Initialize button and LED
    initialize_button(SW_1);
    initialize_button(SW_2);
    initialize_button(PIEZO_SENSOR);
    initialize_led(LED);

    // Set up uart
    uart_init(uart1, UART_BAUDRATE);
    gpio_set_function(TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(RX_PIN, GPIO_FUNC_UART);

    // Initialize eeprom
    initialize_i2c();

    // Initialize chosen serial port
    stdio_init_all();

    // Initialize structures
    device device = {.boot_status = UN_BOOT, .last_day_dispensed = 0, .steps_per_revolution = 0, .reverse = false, .calibrated = false };
    messaging messaging_values = { .curr_state = {0}, .response = {0}};

    //Initialize lora and connect
    int max_retries = 3;
    uint32_t timeout = 500000;

    bool joined_lora_network = setup_lora(max_retries, timeout);
    if (joined_lora_network) {
        send_message_to_lora(messaging_values.response, "AT+MSG=\"Boot\"\n", MSG_TIMEOUT);
    }

    //Initialize queue
    queue_init(&events, sizeof(Event), MAX_QUEUE);

    //enable irq
    gpio_set_irq_enabled_with_callback(PIEZO_SENSOR, GPIO_IRQ_EDGE_FALL, true, &gpio_handler);
    gpio_set_irq_enabled(SW_1, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(SW_2, GPIO_IRQ_EDGE_FALL, true);


    Event current_event = INITIAL_STATE;
    uint state = 0;
    device.calibrated = false;

    eeprom_read(ADDRESS_BOOT_STATUS, &device.boot_status, 1);
    state = check_power_cut(&device, &messaging_values);

    //Main menu
    while(true) {
        switch (state) {
            case INITIAL_STATE:
                set_boot(INITIAL_STATE, &device);
                blink_led(LED, BLINK_WAIT);
                break;
            case SW1_PRESSED:
                set_boot(SW1_PRESSED, &device);
                printf("SW1_PRESSED\n");
                if(!device.calibrated) {
                    write_log_message("Calibrating", &messaging_values);
                    perform_calib(&device);
                    remove_events();
                    if (joined_lora_network) {
                        send_message_to_lora(messaging_values.response, "AT+MSG=\"Device calibrated.\"\n", MSG_TIMEOUT);
                    }
                    write_log_message("Device calibrated", &messaging_values);
                    printf("Device calibrated. Place the pills to the device.\n");
                    device.calibrated = true;
                }
                else {
                    printf("Calibration done already for this round.\n");
                }
                state = LED_ON;
            break;

            case LED_ON:
                set_boot(LED_ON, &device);
                led_bright(LED);
            break;

            case SW2_PRESSED:
                set_boot(SW2_PRESSED, &device);
                //make led off to see the blinks properly
                led_off(LED);
                printf("SW2_PRESSED\n");
                int day = 0;
                if(device.last_day_dispensed != 0) {
                    day = device.last_day_dispensed;
                }
                while (day < DAYS) {
                    char message[LOG_MESSAGE_SIZE];
                    char at_message[LOG_MESSAGE_SIZE];
                    //check_pill_dispensed();
                    remove_events();
                    rotate_one_compartment(&device);
                    if (detect_pill()) {
                        sprintf(message, "Pill detected for day %d", day + 1);
                        sprintf(at_message, "AT+MSG=\"Pill detected for day %d.\"\n", day + 1);
                        write_log_message(message, &messaging_values);
                        if (joined_lora_network) {
                            send_message_to_lora(messaging_values.response, at_message, MSG_TIMEOUT);
                        }
                        printf("%s\n", message);

                    } else {
                        //led blinks when no pill detected
                        for (int i = 0; i < 5; ++i) {
                            blink_led(LED, 100);
                        }
                        sprintf(message, "Pill NOT detected for day %d", day + 1);
                        sprintf(at_message, "AT+MSG=\"Pill not detected for day %d.\"\n", day + 1);
                        if (joined_lora_network) {
                            send_message_to_lora(messaging_values.response, at_message, MSG_TIMEOUT);
                        }
                        write_log_message(message, &messaging_values);
                        printf("%s\n", message);
                    }
                    // Saving last day dispensed
                    device.last_day_dispensed = day + 1;
                    eeprom_write(ADDRESS_FOR_DAY, &device.last_day_dispensed, 1);
                    sleep_ms(TIME_SLEEP);
                    day++;
                }
                print_eeprom_logs(&messaging_values.message_len);
                if (joined_lora_network) {
                    send_message_to_lora(messaging_values.response, "AT+MSG=\"Dispenser empty.\"\n", MSG_TIMEOUT);
                }
                device.calibrated = false;
                device.last_day_dispensed = 0;
                set_boot(UN_BOOT, &device);
                device.steps_per_revolution = 0;
                delete_eeprom_log();
                state = INITIAL_STATE;
                break;
            }

        while (queue_try_remove(&events, &current_event)) {
            switch(current_event) {
                case SW1_PRESSED:
                    state = SW1_PRESSED;
                break;
                case SW2_PRESSED:
                    if (state == LED_ON) {
                        state = SW2_PRESSED;
                    }else {
                        printf("Not calibrated yet.\n");
                    }
                break;
                default:
                    break;
            }

        }
    }
    return 0;
}

void initialize_i2c(void) {
    const int sda = 16;
    const int scl = 17;
    i2c_init(i2c0, I2C_BAUDRATE);
    gpio_set_function(sda, GPIO_FUNC_I2C);
    gpio_set_function(scl, GPIO_FUNC_I2C);
    gpio_pull_up(sda);
    gpio_pull_up(scl);
}

//initialize motor controller
void initialize_controller(uint controller) {
    gpio_init(controller);
    gpio_set_dir(controller, GPIO_OUT);
}

//function that controls the rotation of the motor and return current step
void rotate_one_compartment(device *device) {
    //record the current days(compartment)
    static int current_day = 0;
    current_day = (current_day % DAYS)+1;

    //record the current position (range 0-511)
    //static int current_position = 0;

    //driving the motor with half stepping and run motor number times 1/8 of a revolution
    uint number = device->steps_per_revolution / TOTAL_STEP;
    for (int i = 0; i< number; ++i) {
        //uint16_t current_position = i;
        move_one_step(device);
    }
}

//function that moves one step
void move_one_step(device *device) {
    static int current_step = 0;

    //Update the current step, offers both clockwise and anticlockwise rotation options
    if(device->reverse) {
        //Anticlockwise rotation.
        current_step = (current_step - 1 + TOTAL_STEP) % TOTAL_STEP;
    }
    else {
        //Clockwise rotation. If current step is 7, then set it to 0. Otherwise, increment by 1.
        current_step = (current_step + 1) % TOTAL_STEP;
    }

    gpio_put(IN1,half_stepping[current_step][0]);
    gpio_put(IN2,half_stepping[current_step][1]);
    gpio_put(IN3,half_stepping[current_step][2]);
    gpio_put(IN4,half_stepping[current_step][3]);


    sleep_ms(CHANGE_SPEED);
}

void check_for_edge(bool rising_edge, device *device) {
    bool start = false;
    int previous_value = 0;
    int current_value = 0;

    //set the motor to rotate reversely
    if(rising_edge) {
        device->reverse = true;
        previous_value = 1;
        current_value = 1;
    }

    while (!start) {
        //move the motor step by step and record the current value and previous value
        move_one_step(device);
        previous_value = current_value;
        current_value = gpio_get(OPTO_FORK);

        // Detect edge
        if (rising_edge && current_value && !previous_value) {
            start = true;
        }
        else if (!rising_edge && !current_value && previous_value) {
            start = true;
        }
    }
}

//function that calibrate the motor
void perform_calib(device *device) {
    //initialize variable
    int step_count = 0;

    //clears step per revolution for another round of calibration
    device->steps_per_revolution = 0;
    write_steps_per_revolution_to_eeprom(device->steps_per_revolution);

    //clear the step_count for a new calibration
    step_count = 0;
    check_for_edge(false, device);

    //since falling edge is detected, the previous value is set as 1 and the current value as 0
    int previous_value = 1;
    int current_value = 0;

    //iterate for trial times
    for(int i = 0; i< TRIAL; ++i) {
        //detect a full falling edge and increment the steps based on that
        do {
            move_one_step(device);
            previous_value = current_value;
            current_value = gpio_get(OPTO_FORK);
            ++step_count;
        }while(!(previous_value && !current_value));
    }

    //Take the equipment inaccuracy into account and move the compartment exactly in the middle
    for(int i= 0; i < EQUIP_INACCURACY; ++i) {
        move_one_step(device);
    }

    //calculates the steps per revolution
    device->steps_per_revolution = step_count/TRIAL;
    write_steps_per_revolution_to_eeprom(device->steps_per_revolution);
}

//function that checks if button is pressed. Return 1 if pressed.
int check_pressed(int button) {
    if (!gpio_get(button)) {
        // wait until the release of the button. Sleep for 50ms
        while (!gpio_get(button)){
            sleep_ms(DELAY);
        }
        return 1;
    }
    return 0;
}

//function that initializes button
void initialize_button (int button) {
    gpio_init(button);
    gpio_set_dir(button, GPIO_IN);
    gpio_pull_up(button);
}

//function that initializes led
void initialize_led(int led) {
    gpio_init(led);
    gpio_set_dir(led, GPIO_OUT);
}

//function that blinks led
void blink_led(int led, uint delay) {
    gpio_put(led, true);
    sleep_ms(delay);
    gpio_put(led, false);
    sleep_ms(delay);
}

//function that makes led always on
void led_bright(int led) {
    gpio_put(led, true);
}

//function for detecting the pill dropping
bool detect_pill() {
    const uint pill_drop_timeout = FALL_TIME;
    uint elapsed_time = 0;
    bool detected = false;

    while (elapsed_time < pill_drop_timeout) {
        const uint check_interval = 3;
        if(check_pill_dispensed()) {
            detected = true;
        }
        //check interval
        sleep_ms(check_interval);
        elapsed_time += check_interval;
    }

    return detected;
}

bool check_pill_dispensed(void) {
    Event current_event;
    while (queue_try_remove(&events, &current_event)) {
        if (current_event == PILL_DISPENSED) {
            return true;
        }
    }
    return false;
}

//function that turns off led
void led_off(int led) {
    gpio_put(led, false);
}

//function that recovers the motor to the correct position
void recovery_calib(device *device) {
    //reverse = true;

    check_for_edge(true, device);
    device->reverse = false;

    //Take the equipment inaccuracy into account and move the compartment exactly in the middle
    for(int i= 0; i < EQUIP_INACCURACY_REVERSE; ++i) {
        move_one_step(device);
    }

    //wait for 2s
    sleep_ms(2000);

    //++current_compartment;
    for(int i = 0; i < (device->steps_per_revolution/(DAYS+1)) * device->last_day_dispensed; ++i) {
        move_one_step(device);
    }
}

uint16_t read_steps_per_revolution_from_eeprom() {
    uint8_t msb, lsb;

    eeprom_read(ADDRESS_FOR_STEP, &msb, sizeof(msb));
    eeprom_read(ADDRESS_FOR_STEP + 1, &lsb, sizeof(lsb));

    uint16_t revolution = ((uint16_t)msb << 8) | lsb;
    return revolution;
}

void write_steps_per_revolution_to_eeprom(uint16_t revolution) {
    uint8_t msb = (revolution >> 8) & 0xFF;
    uint8_t lsb = revolution & 0xFF;

    eeprom_write(ADDRESS_FOR_STEP, &msb, sizeof(msb));
    eeprom_write(ADDRESS_FOR_STEP + 1, &lsb, sizeof(lsb));
}

//function that checks reset or power cut off during running and react differently based on different state
int check_power_cut(device *device, messaging *messaging_values){
    eeprom_read(ADDRESS_BOOT_STATUS, &device->boot_status, 1);
    if (device->boot_status != UN_BOOT) {
        send_message_to_lora(messaging_values->response, "AT+MSG=\"Reset of power cut off detected during turning.\"\n", MSG_TIMEOUT);
        printf("Reset or power cut off detected during running\n");

            if(device->boot_status == SW1_PRESSED) {
            write_log_message("Re-Boot during CALIBRATION", messaging_values);
            if(read_steps_per_revolution_from_eeprom()!= 0) {
                printf("Calibration complete\n");
                return LED_ON;
            }
            return INITIAL_STATE;
        }

        if(device->boot_status == LED_ON) {
            write_log_message("Re-Boot during WAITING", messaging_values);
            device->steps_per_revolution = read_steps_per_revolution_from_eeprom();
            device->calibrated = true;
            return LED_ON;
        }

        if(device->boot_status == SW2_PRESSED) {
            write_log_message("Re-Boot during PILL DISPENSING", messaging_values);
            // Print last saved step
            device->steps_per_revolution = read_steps_per_revolution_from_eeprom();
            if (device->steps_per_revolution == 0xFFFF) {  // Assuming 0xFFFF means no step has been saved
                printf("No previous step saved.\n");
            }else{
                printf("Total steps per revolution for last round: %u\n", device->steps_per_revolution);
            }

            eeprom_read(ADDRESS_FOR_DAY, &device->last_day_dispensed, 1);
            recovery_calib(device);
            sleep_ms(TIME_SLEEP);
            return SW2_PRESSED;
        }
    }
    return INITIAL_STATE;
}

void set_boot(int state, device *device) {
        device->boot_status = state;
        eeprom_write(ADDRESS_BOOT_STATUS, &device->boot_status, 1);
}

void remove_events() {
    Event current_event;
    while (queue_try_remove(&events, &current_event));
}
