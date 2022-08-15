/*
 * atca_608a_test.c
 *
 *  Created on: Feb 3, 2020
 *      Author: wesd
 */

#include "sdkconfig.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <esp_system.h>
#include <esp_log.h>
#include <esp_console.h>
#include <argtable3/argtable3.h>
#include <esp_err.h>
#include <cJSON.h>

//#include "cs_binhex.h"
//#include "cs_packer.h"
#include "app_emtr_drv.h"
#include "app_emtr_cal.h"
#include "cmd_emtr.h"

// Sleep for a number of milliseconds
#define	SLEEP_MS(t)		vTaskDelay(pdMS_TO_TICKS(t))

// Read number of seconds since boot
#define	TIME_SEC()		(esp_timer_get_time()/1000000)
#define	TIME_MS()		(esp_timer_get_time()/1000)


static int	emtrIsInitialized;


void initialize_emtr(void)
{
	if (appEmtrDrvInit() == ESP_OK) {
		if (appEmtrDrvStart() == ESP_OK) {
			printf("\r\n");
			printf("EMTR driver started\r\n");
			printf("  BL version   : %s\r\n", csEmtrDrvBlVersion());
			printf("  FW version   : %s\r\n", csEmtrDrvFwVersion());
			emtrIsInitialized = 1;
		} else {
			printf("EMTR driver failed to start\r\n");
		}
	} else {
		printf("EMTR driver failed to initialize\r\n");
	}
}


static int cmd_read_emtr_ver(int argc, char** argv)
{
	if (!emtrIsInitialized) {
		return ESP_ERR_INVALID_STATE;
	}

	const char*	bl = csEmtrDrvBlVersion();
	const char*	fw = csEmtrDrvFwVersion();

	printf("{\"bl\":%s, \"app\":%s}\n", bl, fw);

	return ESP_OK;
}


static int cmd_read_energy(int argc, char** argv)
{
	if (!emtrIsInitialized) {
		return ESP_ERR_INVALID_STATE;
	}

	appEmtrInstant_t	inst;
	if (appEmtrDrvGetInstant(&inst) != ESP_OK) {
		return 1;
	}

	cJSON*	jObj = cJSON_CreateObject();

	cJSON_AddNumberToObject(jObj, "volts", ((float)inst.dVolts)/10.0);
	cJSON_AddNumberToObject(jObj, "amps", ((float)inst.mAmps)/1000.0);
	cJSON_AddNumberToObject(jObj, "watts", ((float)inst.dWatts)/10.0);
	cJSON_AddNumberToObject(jObj, "pf", ((float)inst.pFactor)/100.0);

	char*	jStr = cJSON_PrintUnformatted(jObj);
	cJSON_Delete(jObj);
	printf("%s\n", jStr);
	cJSON_free(jStr);

	return ESP_OK;
}


static esp_err_t decodeCalData(uint8_t * buf, int bufSz)
{
	appEmtrCalData_t	cal;
	esp_err_t	status;

	if ((status = appEmtrCalibrationUnpack(buf, bufSz, &cal)) != ESP_OK) {
		return status;
	}

	cJSON*	jObj = cJSON_CreateObject();

	cJSON_AddNumberToObject(jObj, "u_gain", cal.uGain);
	cJSON_AddNumberToObject(jObj, "i_gain", cal.iGain);
	// ToDo leakage gain

	char*	jStr = cJSON_PrintUnformatted(jObj);
	cJSON_Delete(jObj);
	printf("%s\n", jStr);
	cJSON_free(jStr);

	return 0;
}


static int cmd_save_cal(int argc, char** argv)
{
	if (!emtrIsInitialized) {
		return ESP_ERR_INVALID_STATE;
	}

	// Command send back the calibration data structure
	uint8_t	calData[32];
	int		calLen = sizeof(calData);

	esp_err_t	status;

	if ((status = appEmtrCalibrationDataSave(calData, &calLen)) != ESP_OK) {
		return status;
	}

	return decodeCalData(calData, calLen);
}


static int cmd_read_cal(int argc, char** argv)
{
	if (!emtrIsInitialized) {
		return ESP_ERR_INVALID_STATE;
	}

	uint8_t	calData[64];
	int		calLen = sizeof(calData);

	esp_err_t	status;

	if ((status = appEmtrCalibrationDataRead(calData, &calLen)) != ESP_OK) {
		return status;
	}

	return decodeCalData(calData, calLen);
}

static struct {
	struct arg_dbl	*gain;
	struct arg_end	*end;
} gain_args;


static int _set_gain(emtrCalType_t calType, int argc, char ** argv)
{
	if (!emtrIsInitialized) {
		return ESP_ERR_INVALID_STATE;
	}

	//printf("parse gain args\r\n");

	int nerrors = arg_parse(argc, argv, (void **) &gain_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, gain_args.end, argv[0]);
        return ESP_FAIL;
    }

    double gainVal = gain_args.gain->dval[0];

    //printf("Call appEmtrCalibrateSetGain\r\n");
   	return appEmtrCalibrateSetGain(calType, (float)gainVal);
}


static int cmd_set_u_gain(int argc, char** argv)
{
	return _set_gain(emtrCalType_voltageGain, argc, argv);
}


static int cmd_set_i_gain(int argc, char** argv)
{
	return _set_gain(emtrCalType_currentGain, argc, argv);
}


static struct {
	struct arg_str	*mode;
	struct arg_end	*end;
} relay_set_args;


static int cmd_relay_set(int argc, char** argv)
{
	if (!emtrIsInitialized) {
		return ESP_ERR_INVALID_STATE;
	}

	int nerrors = arg_parse(argc, argv, (void **) &relay_set_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, gain_args.end, argv[0]);
        return ESP_FAIL;
    }

    const char*	modeStr = relay_set_args.mode->sval[0];
    bool	mode;

    if (strcmp(modeStr, "ON") == 0) {
    	mode = true;
    } else if (strcmp(modeStr, "OFF") == 0) {
    	mode = false;
    } else {
    	return ESP_ERR_INVALID_ARG;
    }

    return appEmtrDrvSetRelay(mode);
}

static int cmd_read_alarms(int argc, char** argv)
{
	if (!emtrIsInitialized) {
		return ESP_ERR_INVALID_STATE;
	}

	esp_err_t		status;
	appEmtrStatus_t	emtr;

	if ((status = appEmtrDrvGetStatus(&emtr)) != ESP_OK) {
		return status;
	}

	cJSON*	jObj = cJSON_CreateObject();
	cJSON_AddItemToObject(jObj, "alarms", appEmtrDrvAlarmListJson(emtr.alarm.flags));
	char*	jStr = cJSON_PrintUnformatted(jObj);
	cJSON_Delete(jObj);
	printf("%s\n", jStr);
	cJSON_free(jStr);

	return ESP_OK;
}

const static esp_console_cmd_t cmdTab[] = {
	{
		.command = "EMTR-READ-VER",
		.help = "The UUT will respond with the EMTR boot loader and firmware versions.",
		.hint = NULL,
		.func = cmd_read_emtr_ver,
	},
	{
		.command = "EMTR-READ-ENERGY",
		.help = "The UUT will respond with instant energy values.",
		.hint = NULL,
		.func = cmd_read_energy,
	},
	{
		.command = "EMTR-SET-U-GAIN",
		.help     = "Set voltage gain.",
		.hint     = NULL,
		.func     = cmd_set_u_gain,
		.argtable = &gain_args
	},
	{
		.command = "EMTR-SET-I-GAIN",
		.help     = "Set current gain.",
		.hint     = NULL,
		.func     = cmd_set_i_gain,
		.argtable = &gain_args
	},
	{
		.command = "EMTR-SAVE-CAL",
		.help = "Save EMTR calibration data.",
		.hint = NULL,
		.func = cmd_save_cal,
	},
	{
		.command = "EMTR-READ-CAL",
		.help = "The UUT will respond with the EMTR calibration data.",
		.hint = NULL,
		.func = cmd_read_cal,
	},
	{
		.command = "EMTR-SET-RELAY",
		.help     = "Turn the relay on/off.",
		.hint     = NULL,
		.func     = cmd_relay_set,
		.argtable = &relay_set_args
	},
	{
		.command = "EMTR-READ-ALARMS",
		.help     = "Read alarm flags.",
		.hint     = NULL,
		.func     = cmd_read_alarms
	}
};
static const int cmdTabSz = sizeof(cmdTab) / sizeof(esp_console_cmd_t);


void register_emtr(void)
{
	int							i;
	const esp_console_cmd_t *	cmd;

	gain_args.gain = arg_dbl1(NULL, NULL, "<gain>", "floating point value 0.0 to 0.999(9)");
	gain_args.end  = arg_end(1);

	relay_set_args.mode = arg_str1(NULL, NULL, "<ON|OFF>", "Set relay mode");
	relay_set_args.end = arg_end(1);

	for (i = 0, cmd = cmdTab; i < cmdTabSz; i++, cmd++) {
	    ESP_ERROR_CHECK( esp_console_cmd_register(cmd) );
	}
}
