/* Copyright (C) Grid Connect, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Evandro Copercini <evandro@gridconnect.com>, May 2019
 */

#include "sdkconfig.h"

#include <stdio.h>
#include <string.h>
#include <esp_system.h>
#include <esp_log.h>
#include <esp_console.h>
#include <esp_vfs_dev.h>
#include <esp_netif.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include <hal/gpio_types.h>
#include <driver/uart.h>
#include <driver/i2c.h>
#include <linenoise/linenoise.h>
#include <argtable3/argtable3.h>
#include <esp_vfs_fat.h>

#include "cmd_leds.h"
#include "cmd_inputs.h"
#include "cmd_self_test.h"
#include "cmd_tester.h"
#include "cmd_serial.h"
#include "cmd_ble.h"
#include "cmd_emtr.h"

const char	testFwVersion[] = {"0.1.0"};

static void initialize_console()
{
    /* Disable buffering on stdin */
    setvbuf(stdin, NULL, _IONBF, 0);

    /* Minicom, screen, idf_monitor send CR when ENTER key is pressed */
    esp_vfs_dev_uart_port_set_rx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CR);
    /* Move the caret to the beginning of the next line on '\n' */
    esp_vfs_dev_uart_port_set_tx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CRLF);

    /* Configure UART. Note that REF_TICK is used so that the baud rate remains
     * correct while APB frequency is changing in light sleep mode.
     */
    const uart_config_t uart_config = {
        .baud_rate = CONFIG_ESP_CONSOLE_UART_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        //.use_ref_tick = true
    };
    ESP_ERROR_CHECK( uart_param_config(CONFIG_ESP_CONSOLE_UART_NUM, &uart_config) );

    /* Install UART driver for interrupt-driven reads and writes */
    ESP_ERROR_CHECK( uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, 4096, 0, 0, NULL, 0) );

    /* Tell VFS to use UART driver */
    esp_vfs_dev_uart_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);

    /* Initialize the console */
    esp_console_config_t console_config = {
            .max_cmdline_args = 10,
            .max_cmdline_length = 4096
    };
    ESP_ERROR_CHECK( esp_console_init(&console_config) );


    /* Tell linenoise where to get command completions and hints */
    //linenoiseSetCompletionCallback(&esp_console_get_completion);
    //linenoiseSetHintsCallback((linenoiseHintsCallback*) &esp_console_get_hint);
}

void register_tester(void);

void app_main()
{
	nvs_flash_init();

    initialize_console();

    ESP_ERROR_CHECK(esp_event_loop_create_default());

	vTaskDelay(pdMS_TO_TICKS(100));

    // Initialize command handlers
    initialize_leds();
    initialize_emtr();
    initialize_inputs();
    initialize_serial();
    initialize_ble();

    // Register command handlers
    esp_console_register_help_command();
    register_tester();
    register_self_test();
    register_leds();
    register_inputs();
    register_serial();
    register_ble();
    register_emtr();

    linenoiseSetDumbMode(1);
    const char* prompt = "[cmd]>";

    /* Main loop */
    while (true) {
        /* Get a line using linenoise.
         * The line is returned when ENTER is pressed.
         */
        char* line = linenoise(prompt);
        printf("\n");
        if (line == NULL) { /* Ignore empty lines */
            continue;
        }

        /* Try to run the command */
        int ret;
        esp_err_t err = esp_console_run(line, &ret);
        if (err == ESP_OK && ret == ESP_OK) {
            printf("OK\n");
        } else if (err == ESP_ERR_NOT_FOUND) {
            printf("ERR:CMD\n");
        } else if (err == ESP_ERR_INVALID_ARG) {
            printf("ERR:PARAM\n");
        } else if (err == ESP_OK && ret == ESP_ERR_INVALID_ARG) {
            printf("ERR:PARAM\n");
        } else if (err == ESP_OK && ret != ESP_OK) {
            printf("ERR:INTERNAL\n");
        } else {
            printf("ERR:INTERNAL\n");
        }

        /* linenoise allocates line buffer on the heap, so need to free it */
        linenoiseFree(line);
    }
}
