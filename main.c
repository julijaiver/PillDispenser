#include <stdio.h>
#include "pico/stdlib.h"
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

void initialize_controller(uint controller);
void rotate_one_compartment();
void move_one_step(void);
void perform_calib();
int check_pressed(int button);
void initialize_button (int button);
void initialize_led(int led);
void blink_led(int led);
void led_bright(int led);

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
#define LED 22
#define BLINK_WAIT 500
#define DAYS 7

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
    initialize_led(LED);

    // Initialize chosen serial port
    stdio_init_all();

    //Main menu
    while(true) {
        while(!(check_pressed(SW_1))) {
            blink_led(LED);
        }
        perform_calib();
        led_bright(LED);
        while(!(check_pressed(SW_1))) {
        }
        for(int i = 0; i < DAYS; i++) {
            sleep_ms(3000); //used 3s for testing. Should be 30s
            rotate_one_compartment();
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
    //driving the motor with half stepping and run motor number times 1/8 of a revolution
    int number = steps_per_revolution / TOTAL_STEP;
    for (int i = 0; i< number; ++i) {
        move_one_step();
    }
}

//function that moves one step
void move_one_step(void) {
    static int current_step = 0;
    //update the current step. If current step is 7, then set it to 0. Otherwise, increment by 1.
    current_step = (current_step + 1) % TOTAL_STEP;

    gpio_put(IN1,half_stepping[current_step][0]);
    gpio_put(IN2,half_stepping[current_step][1]);
    gpio_put(IN3,half_stepping[current_step][2]);
    gpio_put(IN4,half_stepping[current_step][3]);


    sleep_ms(CHANGE_SPEED);
}

//function that calibrate the motor
void perform_calib() {
    //initialize variables
    bool start = false;
    int previous_value = 0;
    int current_value = 0;
    int step_count = 0;

    //clears step per revolution for another round of calibration
    steps_per_revolution = 0;

    //clear the step_count for a new calibration
    step_count = 0;
    while(!start) {
        //move the motor step by step and record the current value and previous value
        move_one_step();
        previous_value = current_value;
        current_value = gpio_get(OPTO_FORK);

        //detect falling edge and set start as true
        if(!current_value && previous_value) {
            start = true;
        }
    }
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
void blink_led(int led) {
    gpio_put(led, true);
    sleep_ms(BLINK_WAIT);
    gpio_put(led, false);
    sleep_ms(BLINK_WAIT);
}

//function that makes led always on
void led_bright(int led) {
    gpio_put(led, true);
}