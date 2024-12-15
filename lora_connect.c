#include "lora_connect.h"

#include <hardware/timer.h>
#include <pico/time.h>

// Functions for lora connection

void send_to_uart(uart_inst_t *uart, char *string) {
    while (uart_is_readable(uart)) {
        uart_getc(uart);
    }
    uart_write_blocking(uart, string, strlen((char *)string));
}

bool send_and_execute_lora_command(char *response, const char *command, const char *error_message, uint32_t timeout) {
    send_to_uart(uart1, command);
    if (read_string_from_uart(uart1, timeout, response)) {
        //printf("Response: %s\n", response);
        return true;
    } else {
        printf("%s\n", error_message);
        return false;
    }
}


bool send_message_to_lora(char *response, const char *command, uint32_t timeout) {
    // Send command to UART
    send_to_uart(uart1, "");
    send_to_uart(uart1, command);
    uint64_t start_time = time_us_64(); // Record start time
    uint64_t current_time;

    // Attempt to read response within timeout period
    while (true) {
        current_time = time_us_64();
        if (read_string_from_uart(uart1, timeout, response)) {
            printf("Response: %s\n", response);
            if (strstr(response, "+MSG: Done")) {
                printf("Message \"%s\" successfully sent to LoRa.\n", command);
                return true; // Command successful
            }
        }
        if ((current_time - start_time) >= timeout) {
            printf("Timeout reached, no response from LoRa module.\n");
            return false; // Timeout
        }
    }
}


bool read_string_from_uart(uart_inst_t *uart, uint32_t time_us, char *str) {
    memset(str, 0, sizeof(str));

    int current_position = 0;
    while (uart_is_readable_within_us(uart, time_us)) {
        char c = uart_getc(uart);

        if (c == '\n' || c == '\r') {
            if (current_position > 0) {
                str[current_position] = '\0';
                //printf("Received from LoRa: %s\n", str);
                //current_position = 0;
                return true;
            }
        } else {
            if (current_position < INPUT_SIZE - 1) {
                str[current_position++] = c;
            }
        }
    }
    return false;
}

void process_string(char *string) {
    char *read_ptr = string;
    char *write_ptr = string;
    int found_comma = 0;

    while (*read_ptr != '\0') {
        if (!found_comma) {
            if (*read_ptr == ',') {
                found_comma = 1;
            }
        } else {
            if (*read_ptr != ' ' && *read_ptr != ':') {
                *write_ptr++ = *read_ptr;
            }
        }
        read_ptr++;
    }
    *write_ptr = '\0';

    int len = strlen(string);
    for (int i = 0; i < len; ++i) {
        string[i] = tolower(string[i]);
    }
}

bool initialize_lora(char *response, int max_retries, uint32_t timeout) {
    typedef struct {
        const char *command;
        const char *error_message;
    } LoraCommand;

    const LoraCommand commands[] = {
        {"AT\r\n", "Module not responding."},
        {"AT+VER\r\n", "Failed to get LoRa version."},
        {"AT+ID=DEVEUI\r\n", "Failed to get DevEui."},
        {"AT+MODE=LWOTAA\r\n", "Failed to set mode."},
        {"AT+KEY=APPKEY,\"dbad61a383a2aff0c3f4cfe2244080e3\"\r\n", "Failed to configure AppKey."},
        {"AT+CLASS=A\r\n", "Failed to set Class A mode."},
        {"AT+PORT=8\r\n", "Failed to set port."}
    };

    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        int retries = 0;
        while (retries < max_retries) {
            if (send_and_execute_lora_command(response, commands[i].command, commands[i].error_message, timeout)) {
                break;
            }
            retries++;
            if (retries >= max_retries) {
                printf("Failed to execute command: %s after %d retries.\n", commands[i].command, max_retries);
                return false;
            }
        }
    }
    return true;
}

bool join_lora(char *response, char *command, int max_retries, uint32_t timeout) {
    int retries = 0;
    bool joined = false;

    while (retries < max_retries) {
        snprintf(command, sizeof(command),"AT+JOIN\r\n");
        send_to_uart(uart1, command);

        uint64_t start_time = time_us_64();
        uint64_t elapsed_time = 0;
        bool response_received = false;

        while (elapsed_time < timeout) {
            if (read_string_from_uart(uart1, timeout, response)) {
                printf("Response: %s\n", response);
                if (strstr(response, "+JOIN: Network joined") || strstr(response, "+JOIN: Joined already")) {
                    printf("Successfully joined LoRa network.\n");
                    joined = true;
                    response_received = true;
                    break;
                }
                if (strstr(response, "+JOIN: Join failed")) {
                    printf("Failed to join LoRa.\n");
                    break;
                }else {
                    printf("Trying to join.\n");
                }
            }
            elapsed_time = time_us_64() - start_time;
        }

        if (response_received) break;
        printf("Join attempt %d failed.\n", retries + 1);
        retries++;
    }
    if (!joined) {
        printf("Failed to join network after %d retries.\n", max_retries);
        return false;
    }
    return true;
}

bool setup_lora(int max_retries, uint32_t timeout) {
    char response[256];

    // Initialize LoRa module
    if (!initialize_lora(response, max_retries, timeout)) {
        printf("LoRa initialization failed. Proceeding to next steps.\n");
        return false;
    } else {
        printf("LoRa module initialized successfully.\n");
    }

    // Join LoRa network
    if (!join_lora(response, "AT+JOIN\r\n", max_retries, 30000000)) { // 30 seconds timeout for join
        printf("Failed to join LoRa network. Proceeding to next steps.\n");
        return false;
    } else {
        printf("Successfully joined LoRa network.\n");
    }
    return true;
}

