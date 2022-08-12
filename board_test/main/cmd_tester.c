/*
 * cmd_tester.c
 *
 *  Created on: Feb 3, 2020
 *      Author: wesd
 */

#include <stdio.h>
#include <stdlib.h>
#include "driver/i2c.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "esp_err.h"


extern const char	testFwVersion[];

static int cmd_read_ver(int argc, char** argv)
{
	printf("{\"ver\": %s}\n", testFwVersion);

	return ESP_OK;
}


const static esp_console_cmd_t cmdTab[] = {
	{
		.command = "TST-VER",
		.help = "Show the test firmware version.",
		.hint = NULL,
		.func = cmd_read_ver,
	},
};
static const int cmdTabSz = sizeof(cmdTab) / sizeof(esp_console_cmd_t);


void register_tester(void)
{
	int							i;
	const esp_console_cmd_t *	cmd;

	for (i = 0, cmd = cmdTab; i < cmdTabSz; i++, cmd++) {
	    ESP_ERROR_CHECK( esp_console_cmd_register(cmd) );
	}
}
