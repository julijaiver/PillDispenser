#ifndef LORA_CONNECT_H
#define LORA_CONNECT_H

#define INPUT_SIZE 80

#include <string.h>
#include "hardware/uart.h"

void send_to_uart(uart_inst_t *uart, char *string);
bool read_string_from_uart(uart_inst_t *uart, uint32_t time_us, char *str);


#endif //LORA_CONNECT_H
