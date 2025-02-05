#include "eeprom_log.h"

// Implementations of functions for eeprom log messages

//CRC msb and lsb calculation
uint16_t crc16(const uint8_t *buffer_p, size_t buffer_len) {
    uint8_t x;
    uint16_t crc = 0xFFFF;

    while (buffer_len--) {
        x = (crc >> 8) ^ *buffer_p++;
        x ^= x >> 4;
        crc = (crc << 8) ^ ((uint16_t) (x << 12)) ^ ((uint16_t) (x << 5)) ^ ((uint16_t) x);
    }
    return crc;
}

bool validate_crc(uint8_t *data_buffer, size_t buffer_len) {
    if (crc16(data_buffer, buffer_len) != 0) {
        return false;
    }
    return true;
}

// basic function for simple eeprom write functionality
bool eeprom_write(uint16_t address, uint8_t *data, size_t data_len) {
    uint8_t addr_buf[2] = {((address >> 8) & 0xff), (address & 0xff)};
    uint8_t data_buf[data_len + 2];

    memcpy(data_buf, addr_buf, sizeof(addr_buf));
    memcpy(&data_buf[2], data, data_len);

    int result = i2c_write_blocking(i2c0, EEPROM_ADDRESS, data_buf, data_len+2, false);
    sleep_ms(5);
    return result == data_len + 2;
}
// Writing a log message to eeprom at the next available address
bool write_log_to_eeprom(const uint8_t *message, size_t message_len) {
    uint16_t log_addr;
    if (!log_empty(&log_addr)) {
        delete_eeprom_log();
        log_addr = 0;
    }

    // Adding message to message buffer and terminating with null
    uint8_t log_message_buf[BUFFER_SIZE] = {0};
    memcpy(log_message_buf, message, message_len);
    log_message_buf[message_len] = '\0';

    // Calculating crc and adding to messge buffer
    uint16_t crc = crc16(log_message_buf, message_len+1);
    log_message_buf[message_len+1] = (uint8_t)(crc >> 8);
    log_message_buf[message_len+2] = (uint8_t) (crc & 0xff);

    if (!eeprom_write(log_addr, log_message_buf, BUFFER_SIZE)) {
        return false;
    }

    log_addr += BUFFER_SIZE;
    if (log_addr > MAX_LOG_ADDRESS) {
        delete_eeprom_log();
        log_addr = 0;
    }

    eeprom_write(MAX_LOG_ADDRESS + BUFFER_SIZE, (uint8_t *)&log_addr, sizeof(log_addr));
    return true;
}
//Higher level function to write a string message
void write_log_message(const char *message_content, messaging *messaging_values) {
    messaging_values->message_len = strlen(message_content);
    memcpy(messaging_values->curr_state, message_content, messaging_values->message_len);
    if (write_log_to_eeprom(messaging_values->curr_state, messaging_values->message_len)) {
        printf("Successfully written to eeprom\n");
    } else {
        printf("Failed to write to eeprom\n");
    }
}

bool eeprom_read(uint16_t address, uint8_t *data, size_t data_len) {
    uint8_t addr_buf[2] = {((address >> 8) & 0xff), (address & 0xff)};
    if (i2c_write_blocking(i2c0, EEPROM_ADDRESS, addr_buf, sizeof(addr_buf), true) != sizeof(addr_buf)) {
        return false;
    }
    return i2c_read_blocking(i2c0, EEPROM_ADDRESS, data, data_len, false) == data_len;
}

uint16_t read_log_addr_from_eeprom(void) {
    uint16_t  log_addr;
    if (!eeprom_read(MAX_LOG_ADDRESS + BUFFER_SIZE, (uint8_t *) &log_addr, sizeof(log_addr))) {
        return 0;
    }
    return log_addr;
}
// Printing logs after data validation
void print_eeprom_logs(const size_t *message_len) {
    for (uint16_t i = 0; i <= MAX_LOG_ADDRESS; i+=BUFFER_SIZE) {
        uint8_t read_data[BUFFER_SIZE];

        if (eeprom_read(i, read_data, sizeof(read_data))) {
            if (read_data[0] != 0) {
                if (read_data[BUFFER_SIZE-3] == 0) {
                    if (validate_crc(read_data, *message_len+3)) {
                        char *message_read = (char *) read_data;
                        printf("Log message at address 0x%04x: %s\n", i, message_read);
                    }
                }
            }
        } else {
            printf("No read from EEPROM\n");
            break;
        }
    }
}
//Check if the log is empty
bool log_empty(uint16_t *log_addr) {
    for (uint16_t i = 0; i <= MAX_LOG_ADDRESS; i+=BUFFER_SIZE) {
        uint8_t read_data[BUFFER_SIZE];
        if (eeprom_read(i, read_data, sizeof(read_data))) {
            if (read_data[0] == 0) {
                *log_addr = i;
                return true;
            }
        }
    }
    return false;
}
// Delete the whole log
void delete_eeprom_log(void) {
    for (uint16_t i = 0; i <= MAX_LOG_ADDRESS; i+=BUFFER_SIZE) {
        eeprom_write(i, 0, 1);
    }
}
