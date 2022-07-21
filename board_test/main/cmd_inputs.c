/*
 * cmd_button.c

 *
 *  Created on: Feb 3, 2020
 *      Author: wesd
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"

#include "inp_drv.h"
#include "cmd_inputs.h"


void initialize_inputs(void)
{
	inpDrvInit(NULL, NULL);
}


static int input_read(int argc, char** argv)
{
	inpState_t	inpState;

	if (inpDrvStateRead(inpId_button1, &inpState) != ESP_OK) {
		return 1;
	}
	const char*	btn1 = inpState ? "true" : "false";

	if (inpDrvStateRead(inpId_button2, &inpState) != ESP_OK) {
		return 1;
	}
	const char*	btn2 = inpState ? "true" : "false";

	if (inpDrvStateRead(inpId_switch1, &inpState) != ESP_OK) {
		return 1;
	}
	const char*	sw1 = inpState ? "true" : "false";

	printf("{\"btn1\":%s, \"btn2\":%s, \"sw1\":%s}\n", btn1, btn2, sw1);
    return 0;
}


static const esp_console_cmd_t cmd = {
    .command = "INPUT-READ",
    .help = "The UUT will respond with the state of each input.",
    .hint = NULL,
    .func = input_read,
};

void register_inputs(void)
{
	inpDrvStart();

    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}
