#ifndef SHARED_STRUCTS_H
#define SHARED_STRUCTS_H

#include <stdbool.h>
#include "pico/stdlib.h"

#define LOG_MESSAGE_SIZE 61
#define RESPONSE_BUFFER 256

typedef struct device{
    uint8_t boot_status;
    uint8_t last_day_dispensed;
    uint steps_per_revolution;
    bool reverse;
    bool calibrated;
} device;

typedef struct messaging {
    uint8_t curr_state[LOG_MESSAGE_SIZE];
    char response[RESPONSE_BUFFER];
    size_t message_len;
} messaging;

#endif //SHARED_STRUCTS_H
