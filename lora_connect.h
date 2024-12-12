#ifndef LORA_CONNECT_H
#define LORA_CONNECT_H

#define INPUT_SIZE 80
#define MSG_TIMEOUT 3000000

#include <string.h>
#include "hardware/uart.h"
#include "stdio.h"
#include <ctype.h>

void send_to_uart(uart_inst_t *uart, char *string);
bool send_command_to_lora(char *response, const char *command, uint32_t timeout) ;
bool read_string_from_uart(uart_inst_t *uart, uint32_t time_us, char *str);
void process_string(char *str);
bool initialize_lora();
bool send_message_to_lora(char *response, const char *command, uint32_t timeout);


#endif //LORA_CONNECT_H
