#ifndef LORA_CONNECT_H
#define LORA_CONNECT_H

#define INPUT_SIZE 80
#define MSG_TIMEOUT 5000000


#include <string.h>
#include "hardware/uart.h"
#include "stdio.h"
#include <ctype.h>
#include "shared_structs.h"

void send_to_uart(uart_inst_t *uart, char *string);
bool send_and_execute_lora_command(char *response, const char *command, const char *error_message, uint32_t timeout);
bool read_string_from_uart(uart_inst_t *uart, uint32_t time_us, char *str);
void process_string(char *str);
bool initialize_lora();
bool join_lora(char *response, char *command, int max_retries, uint32_t timeout);
bool send_message_to_lora(char *response, const char *command, uint32_t timeout);
bool setup_lora(int max_retries, uint32_t timeout);


#endif //LORA_CONNECT_H
