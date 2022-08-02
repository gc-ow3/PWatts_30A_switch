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

static struct {
	struct arg_str	*name;
	struct arg_end	*end;
} inp_args;


static int input_read(int argc, char** argv)
{
	int nerrors = arg_parse(argc, argv, (void **)&inp_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, inp_args.end, argv[0]);
        return 1;
    }

	inpId_t	inpId;
	const char*	name = inp_args.name->sval[0];

	if (strcmp("BTN1", name) == 0) {
		inpId = inpId_button1;
	} else if (strcmp("BTN2", name) == 0) {
		inpId = inpId_button2;
	} else if (strcmp("SW1", name) == 0) {
		inpId = inpId_switch1;
	} else {
		return 1;
	}

	inpState_t	inpState;

	if (inpDrvStateRead(inpId, &inpState) != ESP_OK) {
		return 1;
	}

	printf("{\"active\": %s}\n", inpState ? "true" : "false");
    return 0;
}


static const esp_console_cmd_t cmd = {
    .command = "INPUT-READ",
    .help     = "The UUT will report active state of the input.",
    .hint     = NULL,
    .func     = input_read,
	.argtable = &inp_args
};

void register_inputs(void)
{
	inpDrvStart();

	inp_args.name = arg_str1(NULL, NULL, "<name>", "BTN1 | BTN2 | SW1");
	inp_args.end  = arg_end(1);

    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}
