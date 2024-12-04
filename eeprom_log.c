#include "eeprom_log.h"

// Implementations of functions for eeprom log messages

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

bool write_byte_to_eeprom(uint16_t address, uint8_t *data, size_t data_len) {
    uint8_t addr_buf[2];
    addr_buf[0] = (address >> 8) & 0xff;
    addr_buf[1] = (address & 0xff);

    uint8_t data_buf[data_len + 2];
    data_buf[0] = addr_buf[0];
    data_buf[1] = addr_buf[1];
    memcpy(&data_buf[2], data, data_len);

    uint len = (sizeof(data_buf) / sizeof(data_buf[0]));

    int result = i2c_write_blocking(i2c0, EEPROM_ADDRESS, data_buf, len, false);
    sleep_ms(20);

    return result == len;
}

bool write_log_to_eeprom(const uint8_t *message, size_t message_len) {
    uint16_t log_addr = 0;
    /*if (!read_from_eeprom(MAX_LOG_ADDRESS + BUFFER_SIZE, (uint8_t *) &log_addr, sizeof(log_addr))) {
        log_addr = 0;
    }*/
    bool found_empty_address = false;
    for (uint16_t i = 0; i <= MAX_LOG_ADDRESS; i+=BUFFER_SIZE) {
        uint8_t read_data[BUFFER_SIZE];
        if (read_from_eeprom(i, read_data, sizeof(read_data))) {
            if (read_data[0] == 0) {
                log_addr = i;
                found_empty_address = true;
                break;
            }
        }
    }
    if (!found_empty_address) {
        delete_eeprom_log();
        log_addr = 0;
    }

    // Address buffer specified in the beginning of whole data buffer
    uint8_t addr_buf[2] = {0};
    addr_buf[0] = (log_addr >> 8) & 0xff;
    addr_buf[1] = (log_addr & 0xff);

    // Adding message to message buffer and terminating with null
    uint8_t log_message_buf[BUFFER_SIZE] = {0};
    memcpy(log_message_buf, message, message_len);
    log_message_buf[message_len] = '\0';

    // Calculating crc and adding to messge buffer
    uint16_t crc = crc16(log_message_buf, BUFFER_SIZE-2);
    log_message_buf[message_len+1] = (uint8_t)(crc >> 8);
    log_message_buf[message_len+2] = (uint8_t) (crc & 0xff);

    uint8_t data_buf[BUFFER_SIZE + 2];
    data_buf[0] = addr_buf[0];
    data_buf[1] = addr_buf[1];
    memcpy(&data_buf[2], log_message_buf, BUFFER_SIZE);

    int result = i2c_write_blocking(i2c0, EEPROM_ADDRESS, data_buf, BUFFER_SIZE+2, false);
    sleep_ms(20);

    log_addr += BUFFER_SIZE;
    if (log_addr > MAX_LOG_ADDRESS) {
        delete_eeprom_log();
        log_addr = 0;
    }

    write_byte_to_eeprom(MAX_LOG_ADDRESS + BUFFER_SIZE, (uint8_t *)&log_addr, sizeof(log_addr));

    return result == BUFFER_SIZE + 2;
}

bool read_from_eeprom(uint16_t address, uint8_t *data, size_t data_len) {
    uint8_t addr_buf[2];
    addr_buf[0] = (address >> 8) & 0xff;
    addr_buf[1] = (address & 0xff);
    uint len = (sizeof(addr_buf) / sizeof(addr_buf[0]));

    if (i2c_write_blocking(i2c0, EEPROM_ADDRESS, addr_buf, len, true) != len) {
        return false;
    }

    if (i2c_read_blocking(i2c0, EEPROM_ADDRESS, data, data_len, false) != data_len) {
        return false;
    }
    return true;
}

uint16_t read_log_addr_from_eeprom(void) {
    uint16_t  log_addr;
    if (!read_from_eeprom(MAX_LOG_ADDRESS + BUFFER_SIZE, (uint8_t *) &log_addr, sizeof(log_addr))) {
        return 0;
    }
    return log_addr;
}

void print_eeprom_logs(void) {
    for (uint16_t i = 0; i <= MAX_LOG_ADDRESS; i+=BUFFER_SIZE) {
        uint8_t read_data[BUFFER_SIZE];

        if (read_from_eeprom(i, read_data, sizeof(read_data))) {
            if (read_data[0] != 0) {
                if (read_data[BUFFER_SIZE-3] == 0) {
                    /*if (validate_crc(read_data, BUFFER_SIZE)) {
                        printf("CRC OK\n");
                        /*printf("Unprocessed log data at address 0x%04x: \n", i);
                        for (size_t j = 0; j < BUFFER_SIZE; ++j) {
                            printf("%02x ", read_data[j]);
                        }
                        printf("\n");
                    } else printf("CRC ERROR\n"); */
                    char *message_read = (char *) read_data;
                    printf("Log message at address 0x%04x: %s\n", i, message_read);
                    //printf("\n");
                }
            }
        } else {
            printf("No read from EEPROM\n");
            break;
        }
    }
}

void delete_eeprom_log(void) {
    for (uint16_t i = 0; i <= MAX_LOG_ADDRESS; i+=BUFFER_SIZE) {
        write_byte_to_eeprom(i, 0, 1);
    }
}
