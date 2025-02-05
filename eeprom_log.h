#ifndef EEPROM_LOG_H
#define EEPROM_LOG_H

#define EEPROM_ADDRESS 0x50
#define BUFFER_SIZE 64
#define MAX_LOG_ADDRESS 1984

#include <stdio.h>
#include<string.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "shared_structs.h"

uint16_t crc16(const uint8_t *buffer_p, size_t buffer_len);
bool validate_crc(uint8_t *data_buffer, size_t buffer_len);
bool write_log_to_eeprom(const uint8_t *message, size_t message_len);
void write_log_message(const char *message_content, messaging *messaging_values);
bool eeprom_write(uint16_t address, uint8_t *data, size_t data_len);
bool eeprom_read(uint16_t address, uint8_t *data, size_t data_len);
void print_eeprom_logs(const size_t *message_len);
bool log_empty(uint16_t *log_addr);
void delete_eeprom_log(void);

#endif //EEPROM_LOG_H

