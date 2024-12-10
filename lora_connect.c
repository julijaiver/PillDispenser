#include "lora_connect.h"

// Functions for lora connection

//AT+MODE=LWOTAA      Returns: +MODE: LWOTAA     / Enter LWOTAA mode successfully
//AT+KEY=APPKEY, “16 bytes length key”      /Set key    Returns: +KEY: APPKEY 2B7E151628AED2A6ABF7158809CF4F3C(key num)
//AT+CLASS=A    // Enable Class A mode
//AT+PORT=8     // Set port to 8
// AT+JOIN   //To join a known network
/* Returns
a) Join successfully
+JOIN: Starting
+JOIN: NORMAL
+JOIN: NetID 000024 DevAddr 48:00:00:01
+JOIN: Done
b) Join failed
+JOIN: Join failed
c) Join process is ongoing
+JOIN: LoRaWAN modem is busy */
//AT+MSG="Data to send"
/*Return: (Full return message)
+MSG: Start
+MSG: FPENDING
+MSG: Link 20, 1
+MSG: ACK Received
+MSG: MULTICAST
+MSG: PORT: 8; RX: "12345678"
+MSG: RXWIN214, RSSI -106, SNR 4
+MSG: Done*/

void send_to_uart(uart_inst_t *uart, char *string) {
    while (uart_is_readable(uart)) {
        uart_getc(uart);
    }
    uart_write_blocking(uart, string, strlen((char *)string));
}

bool send_command_to_lora(char *response, const char *command, uint32_t timeout) {
    send_to_uart(uart1, command);
    if (read_string_from_uart(uart1, timeout, response)) {
        printf("Response: %s\n", response);
        return true;
    } else {
        printf("Module stopped responding\n");
        return false;
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

//function for initializing and connecting lora
bool initialize_lora() {
    uint state = 0;
    uint retries = 0;
    const int max_retries = 3;
    char response[INPUT_SIZE] = {0};
    //bool response_received = false;
    bool joined_network = false;
    printf("Initializing lora\n");

    while (state != 9) {  // Exit when initialization is complete
        switch (state) {
            case 0: // Check if the LoRa module is responsive
                if (send_command_to_lora(response, "AT\n", 500000)) {
                    printf("Connected to LoRa module: %s\n", response);
                    state = 1;
                } else {
                    printf("Module not responding.\n");
                    retries++;
                    if (retries >= max_retries) return; // Exit after max retries
                }
                break;

            case 1: // Get firmware version
                if (send_command_to_lora(response, "AT+VER\n", 500000)) {
                    printf("LoRa version: %s\n", response);
                    state = 2;
                } else {
                    printf("Failed to get LoRa version.\n");
                    state = 0;
                }
                break;

            case 2: // Get DevEui
                if (send_command_to_lora(response, "AT+ID=DEVEUI\n", 500000)) {
                    printf("DevEui: %s\n", response);
                    state = 3;
                } else {
                    printf("Failed to get DevEui.\n");
                    state = 0;
                }
                break;

            case 3: // Set mode to LWOTAA
                if (send_command_to_lora(response, "AT+MODE=LWOTAA\n", 500000)) {
                    printf("Mode set: %s\n", response);
                    state = 4;
                } else {
                    printf("Failed to set mode.\n");
                    state = 0;
                }
                break;

            case 4: // Set AppKey
                if (send_command_to_lora(response, "AT+KEY=APPKEY,\"dbad61a383a2aff0c3f4cfe2244080e3\"\n", 500000)) {
                    printf("AppKey configured: %s\n", response);
                    state = 5;
                } else {
                    printf("Failed to configure AppKey.\n");
                    state = 0;
                }
                break;

            case 5: // Set Class A
                if (send_command_to_lora(response, "AT+CLASS=A\n", 500000)) {
                    printf("Class A mode set: %s\n", response);
                    state = 6;
                } else {
                    printf("Failed to set Class A mode.\n");
                    state = 0;
                }
                break;

            case 6: // Set port
                if (send_command_to_lora(response, "AT+PORT=8\n", 500000)) {
                    printf("Port set to 8: %s\n", response);
                    state = 7;
                } else {
                    printf("Failed to set port.\n");
                    state = 0;
                }
                break;

            case 7: // Attempt to join the network
                retries = 0;
                while (retries < max_retries) {
                    if (send_command_to_lora(response, "AT+JOIN\n", 30000000)) {
                        printf("Successfully joined LoRa network: %s\n", response);
                        joined_network = true;
                        state = 0;
                        return true;
                        break;
                    } else {
                        printf("Join attempt %d failed.\n", retries + 1);
                        retries++;
                    }
                }
                if (!joined_network) {
                    printf("Failed to join network after %d retries.\n", max_retries);
                    state = 0;
                    return false;
                }
                break;
            default:
                printf("Invalid state encountered.\n");
                return;
        }
    }
}

