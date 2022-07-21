/* Copyright (C) Grid Connect, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <driver/i2c.h>
#include <esp_log.h>
#include <esp_console.h>
#include <argtable3/argtable3.h>

#include "cs_common.h"
#include "cmd_leds.h"
#include "led_drv.h"

//static const char	TAG[] = {"CMD_LEDS"};


void initialize_leds()
{
	ledDrvInit();
	ledDrvStart();
}


static struct {
    struct arg_str *mode;
    struct arg_end *end;
} led_set_args;


static int led_set(ledId_t led, int argc, char** argv)
{
	int nerrors = arg_parse(argc, argv, (void **)&led_set_args);
	if (nerrors != 0) {
		arg_print_errors(stderr, led_set_args.end, argv[0]);
		return 1;
	}

	if (led_set_args.mode->count <= 0) {
		return 1;
	}
	const char *	mode = led_set_args.mode->sval[0];

	// Apply changes to bit mask per command
	if (strcmp("OFF", mode) == 0) {
		ledDrvSetMode(led, ledMode_off);
	} else if (strcmp("RED", mode) == 0) {
		ledDrvSetMode(led, ledMode_red);
	} else if (strcmp("GRN", mode) == 0) {
		ledDrvSetMode(led, ledMode_grn);
	} else if (strcmp("BLU", mode) == 0) {
		ledDrvSetMode(led, ledMode_blu);
	} else {
		// Invalid mode
		return 1;
	}

	return 0;
}


static int led_set_sys(int argc, char** argv)
{
	return led_set(ledId_system, argc, argv);
}


static int led_set_bt(int argc, char** argv)
{
	return led_set(ledId_ble, argc, argv);
}


static const esp_console_cmd_t	cmdTab[] = {
	{
        .command = "LED-SET-SYS",
        .help    = "Switch the system status LED",
        .hint    = NULL,
        .func    = led_set_sys,
		.argtable = &led_set_args
	},
	{
        .command = "LED-SET-BLE",
        .help    = "Switch the Bluetooth status LED",
        .hint    = NULL,
        .func    = led_set_bt,
		.argtable = &led_set_args
	},
};
static const int 	cmdTabSz = sizeof(cmdTab) / sizeof(esp_console_cmd_t);


void register_leds()
{
    led_set_args.mode = arg_str1(NULL, NULL, "<str>", "OFF|RED|GRN|BLU");
    led_set_args.end = arg_end(1);

	int	i;
	for (i = 0; i < cmdTabSz; i++) {
	    ESP_ERROR_CHECK(esp_console_cmd_register(&cmdTab[i]));
	}
}
