#include <stdio.h>
#include "pico/stdlib.h"
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "pico/util/queue.h"
#include "pico/time.h"


void initialize_controller(uint controller);
void rotate_one_compartment();
void move_one_step(void);
bool check_for_edge(bool rising_edge);
void perform_calib();
int check_pressed(int button);
void initialize_button (int button);
void initialize_led(int led);
void blink_led(int led, uint delay);
void led_bright(int led);
bool detect_pill();
bool check_pill_dispensed(void);
void led_off(int led);
void recovery_calib(int current_compartment, bool dispensed, int compartment_steps);

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
#define BLINK_WAIT 500
#define DAYS 7
#define PIEZO_SENSOR 27
#define MAX_QUEUE 100
#define FALL_TIME 85 //calculated what is the maximum time needed in theory for a pill to drop. t= sqrt((2*0.035)/9.8) = 0.085 s.
#define EQUIP_INACCURACY_REVERSE 207

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

    if (elapsed_time > 10000) {  //debounce
        last_event_time = current_time;

        if (gpio == SW_1) {
            Event sw1_pressed = SW1_PRESSED;
            queue_try_add(&events, &sw1_pressed);
        } else if (gpio == SW_2) {
            Event sw2_pressed = SW2_PRESSED;
            queue_try_add(&events, &sw2_pressed);
        }
    }
    if (elapsed_time > 20000) {
        //debounce for pill dropping
        last_event_time = current_time;
        if (gpio == PIEZO_SENSOR) {
            Event pill_detected = PILL_DISPENSED;
            queue_try_add(&events, &pill_detected);
        }
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

static int steps_per_revolution = 0;
static bool reverse = false;

int main() {
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

    // Initialize chosen serial port
    stdio_init_all();
    //Initialize queue
    queue_init(&events, sizeof(Event), MAX_QUEUE);

    //enable irq
    gpio_set_irq_enabled_with_callback(PIEZO_SENSOR, GPIO_IRQ_EDGE_FALL, true, &gpio_handler);
    gpio_set_irq_enabled(SW_1, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(SW_2, GPIO_IRQ_EDGE_FALL, true);

    Event current_event = INITIAL_STATE;
    uint state = 0;
    bool calibrated = false;

    //Main menu
    while(true) {

        switch (state) {
            case INITIAL_STATE:
                blink_led(LED, BLINK_WAIT);
                break;
            case SW1_PRESSED:
                printf("SW1_PRESSED\n");
                if(!calibrated) {
                    perform_calib();
                    printf("Device calibrated.\n");
                    calibrated = true;
                }
                else {
                    printf("Calibration done already for this round.\n");
                }
                state = LED_ON;
            break;

            case LED_ON:
                led_bright(LED);
            break;

            case SW2_PRESSED:
                //make led off to see the blinks properly
                led_off(LED);
                printf("SW2_PRESSED\n");
                for (int i = 0; i < DAYS; i++) {
                    rotate_one_compartment();
                    if (detect_pill()) {
                        printf("Pill detected for day %d\n", i + 1);
                    } else {
                        //led blinks when no pill detected
                        for (int i = 0; i < 5; ++i) {
                            blink_led(LED, 100);
                        }
                        printf("Pill NOT detected for day %d\n", i + 1);
                    }

                    //THIS PIECE OF CODE(141-144) IS JUST FOR TESTING!!! SHOULD NOT BE USED!
                    if(i == 4) {
                        recovery_calib(5,false,512);
                    }

                    sleep_ms(3000);
                }
                state = INITIAL_STATE;
                calibrated = false;
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
                    }
                break;
                default:
                    break;
            }

        }
    }
    return 0;
}

//initialize motor controller
void initialize_controller(uint controller) {
    gpio_init(controller);
    gpio_set_dir(controller, GPIO_OUT);
}

//function that controls the rotation of the motor and return current step
void rotate_one_compartment() {
    //record the current days(compartment)
    static int current_day = 0;
    current_day = (current_day % DAYS)+1;

    //record the current position (range 0-511)
    static int current_position = 0;

    //driving the motor with half stepping and run motor number times 1/8 of a revolution
    int number = steps_per_revolution / TOTAL_STEP;
    for (int i = 0; i< number; ++i) {
        current_position = i;
        move_one_step();
    }
}

//function that moves one step
void move_one_step(void) {
    static int current_step = 0;

    //Update the current step, offers both clockwise and anticlockwise rotation options
    if(reverse) {
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

bool check_for_edge(bool rising_edge) {
    bool start = false;
    int previous_value = 0;
    int current_value = 0;

    //set the motor to rotate reversely
    if(rising_edge) {
        reverse = true;
        previous_value = 1;
        current_value = 1;
    }

    while (!start) {
        //move the motor step by step and record the current value and previous value
        move_one_step();
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
    return start;
}

//function that calibrate the motor
void perform_calib() {
    //initialize variable
    int step_count = 0;

    //clears step per revolution for another round of calibration
    steps_per_revolution = 0;

    //clear the step_count for a new calibration
    step_count = 0;
    check_for_edge(false);

    //since falling edge is detected, the previous value is set as 1 and the current value as 0
    int previous_value = 1;
    int current_value = 0;

    //iterate for trial times
    for(int i = 0; i< TRIAL; ++i) {
        //detect a full falling edge and increment the steps based on that
        do {
            move_one_step();
            previous_value = current_value;
            current_value = gpio_get(OPTO_FORK);
            ++step_count;
        }while(!(previous_value && !current_value));
    }

    //Take the equipment inaccuracy into account and move the compartment exactly in the middle
    for(int i= 0; i < EQUIP_INACCURACY; ++i) {
        move_one_step();
    }

    //calculates the steps per revolution
    steps_per_revolution = step_count/TRIAL;
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

//fucntion that turns off led
void led_off(int led) {
    gpio_put(led, false);
}

//NOT YET COMPLETED IN THE MAIN PROGRAM! NEED EEPROM SAVING STATUS TO PROCEED.
void recovery_calib(int current_compartment, bool dispensed, int compartment_steps) {
    //reverse = true;

    check_for_edge(true);

    reverse = false;

    //Take the equipment inaccuracy into account and move the compartment exactly in the middle
    for(int i= 0; i < EQUIP_INACCURACY_REVERSE; ++i) {
        move_one_step();
    }

    //wait for 2s
    sleep_ms(2000);

    if(dispensed) {
        ++current_compartment;
    }
    for(int i = 0; i < current_compartment * compartment_steps; ++i) {
        move_one_step();
    }
}