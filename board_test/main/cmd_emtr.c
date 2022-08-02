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

#include "cs_binhex.h"
#include "cs_packer.h"
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
	if (!emtrIsInitialized)
		return 1;

	const char*	bl = csEmtrDrvBlVersion();
	const char*	fw = csEmtrDrvFwVersion();

	printf("BL:%s,FW:%s\n", bl, fw);
	return 0;
}


static int cmd_read_energy(int argc, char** argv)
{
	if (!emtrIsInitialized)
		return 1;

	appEmtrInstant_t	inst;
	if (appEmtrDrvGetInstValues(&inst) == ESP_OK) {
		printf(
			"%.2f, %.3f, %.3f, %.3f, %u\n",
			((float)inst.dVolts)/10.0,
			((float)inst.mAmps)/1000.0,
			((float)inst.dWatts)/10.0,
			((float)inst.pFactor)/100.0,
			inst.relayOnSecs
		);
		return 0;
	}

	return 1;
}


static int cmd_save_cal(int argc, char** argv)
{
	if (!emtrIsInitialized)
		return ESP_ERR_INVALID_STATE;

	// Command send back the calibration data structure
	uint8_t	calData[32];
	int		calLen = sizeof(calData);

	if (appEmtrCalibrationDataSave(calData, &calLen) == ESP_OK) {
		if (decodeCalData(calData, calLen) == ESP_OK) {
			return 0;
		}
	}

	return 1;
}


static esp_err_t decodeCalData(uint8_t * buf, int bufSz)
{
	appEmtrCalData_t	cal;
	esp_err_t	status;

	if ((status = appEmtrCalibrationUnpack(buf, bufSz, &cal)) != ESP_OK) {
		return status;
	}

	printf(
		"%.4f, %.4f, %u\n",
		cal.uGain, cal.iGain, cal.hcci
	);

	return ESP_OK;
}


static int cmd_read_cal(int argc, char** argv)
{
	if (!emtrIsInitialized)
		return ESP_ERR_INVALID_STATE;

	uint8_t	calData[64];
	int		calLen = sizeof(calData);

	if (appEmtrCalibrationDataRead(calData, &calLen) == ESP_OK) {
		if (decodeCalData(calData, calLen) == ESP_OK) {
			return 0;
		}
	}

	return 1;
}

#if 0
static int cmd_read_alarms(int argc, char** argv)
{
	if (!emtrIsInitialized)
		return ESP_ERR_INVALID_STATE;

	wwPumpStatus_t	pump;

	if (wwPumpDrvGetStatus(&pump) == ESP_OK) {
		cJSON *	jObj = cJSON_CreateObject();
		cJSON_AddItemToObject(jObj, "alarms", wwPumpDrvAlarmListJson(pump.alarm.flags));
		char *	jStr = cJSON_PrintUnformatted(jObj);
		cJSON_Delete(jObj);
		printf("%s\n", jStr);
		cJSON_free(jStr);
		return 0;
	}

	return 1;
}
#endif

static struct {
	struct arg_dbl	*gain;
	struct arg_end	*end;
} gain_args;


static int _set_gain(emtrCalType_t calType, int argc, char ** argv)
{
	if (!emtrIsInitialized)
		return 1;

	printf("parse gain args\r\n");

	int nerrors = arg_parse(argc, argv, (void **) &gain_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, gain_args.end, argv[0]);
        return 1;
    }

    if (gain_args.gain->count > 0) {
        double gainVal = gain_args.gain->dval[0];

    	printf("Call appEmtrCalibrateSetGain\r\n");
    	if (appEmtrCalibrateSetGain(calType, (float)gainVal) == ESP_OK) {
    		return 0;
    	}
    }

	return 1;
}


static int cmd_set_u_gain(int argc, char** argv)
{
	return _set_gain(emtrCalType_voltageGain, argc, argv);
}


static int cmd_set_i_gain(int argc, char** argv)
{
	return _set_gain(emtrCalType_currentGain, argc, argv);
}


#if 0
static int cmd_triac_on(int argc, char** argv)
{
	wwPumpDrvFactoryTest(wwPumpTestId_triacOn, 60);
	return 0;
}


static int cmd_line_relay_on(int argc, char** argv)
{
	wwPumpDrvFactoryTest(wwPumpTestId_lineRelayOn, 60);
	return 0;
}


static int cmd_neutral_relay_off(int argc, char** argv)
{
	wwPumpDrvFactoryTest(wwPumpTestId_neutralRelayOff, 60);
	return 0;
}
#endif


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
#if 0
	{
		.command = "EMTR-TRIAC-ON",
		.help = "Turn on the triac.",
		.hint = NULL,
		.func = cmd_triac_on
	},
	{
		.command = "EMTR-LINE-RELAY-ON",
		.help = "Turn on the line relay.",
		.hint = NULL,
		.func = cmd_line_relay_on
	},
	{
		.command = "EMTR-READ-ALARMS",
		.help     = "Read fault flags.",
		.hint     = NULL,
		.func     = cmd_read_alarms
	}
#endif
};
static const int cmdTabSz = sizeof(cmdTab) / sizeof(esp_console_cmd_t);


void register_emtr(void)
{
	int							i;
	const esp_console_cmd_t *	cmd;

	gain_args.gain = arg_dbl1(NULL, NULL, "<gain>", "floating point value 0.0 to 0.999(9)");
	gain_args.end  = arg_end(1);

	for (i = 0, cmd = cmdTab; i < cmdTabSz; i++, cmd++) {
	    ESP_ERROR_CHECK( esp_console_cmd_register(cmd) );
	}
}
