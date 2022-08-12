/* Copyright (C) Grid Connect, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Evandro Copercini <evandro@gridconnect.com>, May 2019
 */


#include <stdio.h>
#include <string.h>
#include <esp32/spiram.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_console.h>
#include <argtable3/argtable3.h>
#include "pinout.h"
#include "cs_binhex.h"

static int cmd_cpuid(int argc, char** argv)
{
	esp_err_t	status;
	uint8_t	macBin[6];

	if ((status = esp_efuse_mac_get_default(macBin)) != ESP_OK) {
		return status;
	}

	char	macStr[16];
	csBinToHex8(macBin, 6, macStr, sizeof(macStr));
	printf("{\"cpu_id\": %s}\n", macStr);

	return ESP_OK;
}

static const esp_console_cmd_t	cmdTab[] = {
	{
		.command = "CPU-ID",
		.help = "Read the 12-digit factory MAC address.",
		.hint = NULL,
		.func = cmd_cpuid,
	}
};
static const int 	cmdTabSz = sizeof(cmdTab) / sizeof(esp_console_cmd_t);

void register_self_test()
{
	int	i;
	for (i = 0; i < cmdTabSz; i++) {
	    ESP_ERROR_CHECK(esp_console_cmd_register(&cmdTab[i]));
	}
}
