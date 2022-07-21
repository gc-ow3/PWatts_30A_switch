/*
 */

#include "sdkconfig.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <driver/uart.h>
#include <esp_system.h>
#include <esp_log.h>
#include <esp_console.h>
#include <argtable3/argtable3.h>
#include <esp_err.h>

#include "cmd_serial.h"

#include "pinout.h"
#include "uart_test.h"

static const char	TAG[] = {"CMD-SERIAL"};

void initialize_serial(void)
{
	// ToDo
}


static struct {
    struct arg_int *chan;
    struct arg_str *mode;
    struct arg_int *baud;
    struct arg_end *end;
} ser_mode_args;


typedef enum {
	uartMode_off = 0,
	uartMode_loop,
	uartMode_spy,
} uartMode_t;

static uartMode_t	uartMode[2];

static int cmd_ser_mode(int argc, char** argv)
{
	int nerrors = arg_parse(argc, argv, (void **)&ser_mode_args);
	if (nerrors != 0) {
		arg_print_errors(stderr, ser_mode_args.end, argv[0]);
		return 1;
	}

	int	chan = ser_mode_args.chan->ival[0];

	uart_port_t	port;
	if (1 == chan) {
		port = UART_NUM_1;
	} else if (2 == chan) {
		port = UART_NUM_2;
	} else {
		ESP_LOGE(TAG, "Invalid channel (%d)", chan);
		return 1;
	}

	int	uartIdx = chan - 1;

	uint32_t	baud;
	if (ser_mode_args.baud->count > 0) {
		baud = (uint32_t)ser_mode_args.baud->ival[0];
	} else {
		baud = 9600;
	}

	const char	*mode = ser_mode_args.mode->sval[0];
	uartMode_t	newMode;

	if (strcmp(mode, "loop") == 0) {
		newMode = uartMode_loop;
	} else if (strcmp(mode, "spy") == 0) {
		newMode = uartMode_spy;
	} else if (strcmp(mode, "off") == 0) {
		newMode = uartMode_off;
	} else {
		ESP_LOGE(TAG, "Invalid selected mode (%s)", mode);
		return 1;
	}

	if (newMode == uartMode[uartIdx]) {
		return 0;
	}

	if (newMode == uartMode_off) {
		if (uartMode_spy == uartMode[uartIdx]) {
			uartSpyStop(port);
		} else if (uartMode_loop == uartMode[uartIdx]) {
			uartLoopStop(port);
		} else {
			ESP_LOGE(TAG, "Invalid current mode (%d)", uartMode[uartIdx]);
			return 1;
		}
	} else if (uartMode_off != uartMode[uartIdx]) {
		ESP_LOGE(TAG, "Current mode active (%d)", uartMode[uartIdx]);
		return 1;
	} else if (uartMode_spy == newMode) {
		uartSpyStart(port, baud);
	} else if (uartMode_loop == newMode) {
		uartLoopStart(port, baud);
	} else {
		ESP_LOGE(TAG, "Invalid new mode (%d)", newMode);
		return 1;
	}

	uartMode[uartIdx] = newMode;
	return 0;
}


const static esp_console_cmd_t cmdTab[] = {
	{
		.command = "SER-TEST",
		.help = "Set serial test mode.",
		.hint = NULL,
		.func = cmd_ser_mode,
		.argtable = &ser_mode_args
	},
};
static const int cmdTabSz = sizeof(cmdTab) / sizeof(esp_console_cmd_t);


void register_serial(void)
{
    ser_mode_args.chan = arg_int1("c", "chan", "<int>", "Channel: 1|2");
    ser_mode_args.mode = arg_str1("m", "mode", "<str>", "off|loop|spy");
    ser_mode_args.baud = arg_int0("b", "baud", "<int>", "Baud rate (default 9600)");
    ser_mode_args.end = arg_end(4);

	int							i;
	const esp_console_cmd_t *	cmd;

	for (i = 0, cmd = cmdTab; i < cmdTabSz; i++, cmd++) {
	    ESP_ERROR_CHECK( esp_console_cmd_register(cmd) );
	}
}
