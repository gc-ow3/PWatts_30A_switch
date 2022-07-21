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
#include <ctype.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>

#include "driver/i2c.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "esp_err.h"
#include "basic/atca_basic.h"
#include "cs_binhex.h"
#include "cs_packer.h"
#include "ww_pump_drv.h"
#include "app_emtr_cal.h"
#include "cmd_emtr.h"

// Sleep for a number of milliseconds
#define	SLEEP_MS(t)			vTaskDelay(pdMS_TO_TICKS(t))

// Read number of seconds since boot
#define	TIME_SEC()			(esp_timer_get_time()/1000000)
#define	TIME_MS()			(esp_timer_get_time()/1000)


static int	emtrIsInitialized;


void initialize_emtr(void)
{
	if (wwPumpDrvInit() == ESP_OK) {
		if (wwPumpDrvStart() == ESP_OK) {
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


static int cmd_read_test(int argc, char** argv)
{
	printf("%s\n", emtrIsInitialized ? "PASS" : "FAIL");
	return 0;
}


static int cmd_read_emtr_ver(int argc, char** argv)
{
	if (!emtrIsInitialized)
		return 1;

	const char *	bl = csEmtrDrvBlVersion();
	const char *	fw = csEmtrDrvFwVersion();

	printf("BL:%s,FW:%s\n", bl, fw);
	return 0;
}


static int cmd_read_energy(int argc, char** argv)
{
	if (!emtrIsInitialized)
		return 1;

	wwPumpInstant_t		inst;
	if (wwPumpDrvGetInstValues(&inst) == ESP_OK) {
		printf(
			"%.2f, %.3f, %.3f, %.3f, %u, %u\n",
			((float)inst.dVolts)/10.0,
			((float)inst.mAmps)/1000.0,
			((float)inst.dWatts)/10.0,
			((float)inst.pFactor)/100.0,
			inst.powerOnSecs,
			inst.pumpOnSecs
		);
		return 0;
	}

	return 1;
}



typedef struct {
	float		uGain;
	float		iGain;
	uint32_t	ilkHi;
	uint32_t	ilkLo;
	uint8_t		hcci;
	uint16_t	atmoMv;
} calData_t;


static esp_err_t unPackCalData(uint8_t * inp, int inpLen, calData_t * cal)
{
	csPacker_t	pack;
	uint32_t	temp32;

	csPackInit(&pack, inp, inpLen);

	// Bytes 0-3: U-Gain
	csUnpackBEU32(&pack, &temp32);
	cal->uGain = (float)temp32 / (float)0x7fffffff;
	// Bytes 4-7: I-Gain
	csUnpackBEU32(&pack, &temp32);
	cal->iGain = (float)temp32 / (float)0x7fffffff;
	// Bytes 8-11: Current leak detection hi threshold
	csUnpackBEU32(&pack, &cal->ilkHi);
	// Bytes 12-15: Current leak detection hi threshold
	csUnpackBEU32(&pack, &cal->ilkLo);
	// Byte 16 HCCI flag
	csUnpackU8(&pack, &cal->hcci);
	// Byte 17-18 atmospheric pressure mV
	csUnpackBEU16(&pack, &cal->atmoMv);

	return csPackStatus(&pack);
}


static esp_err_t decodeCalData(uint8_t * buf, int bufSz)
{
	calData_t	cal;
	esp_err_t	status;

	if ((status = unPackCalData(buf, bufSz, &cal)) != ESP_OK) {
		return status;
	}

	printf(
		"%.4f, %.4f, %u, %u, %u, %u\n",
		cal.uGain, cal.iGain, cal.ilkHi, cal.ilkLo, cal.hcci, cal.atmoMv
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


static struct {
	struct arg_int	*value;
	struct arg_end	*end;
} leak_args;


static int _setLeak(int type, int argc, char** argv)
{
	if (!emtrIsInitialized)
		return 1;

	int nerrors = arg_parse(argc, argv, (void **) &leak_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, gain_args.end, argv[0]);
        return 1;
    }

    if (leak_args.value->count > 0) {
    	uint32_t	value = (uint32_t)leak_args.value->ival[0];

    	if (0 == type) {
    		if (appEmtrCalibrationSetLeakLo(value) == ESP_OK) {
    			return 0;
    		}
    	} else if (1 == type) {
    		if (appEmtrCalibrationSetLeakHi(value) == ESP_OK) {
    			return 0;
    		}
    	}
    }

	return 1;
}


static int cmd_set_ilk_lo(int argc, char** argv)
{
	return _setLeak(0, argc, argv);
}


static int cmd_set_ilk_hi(int argc, char** argv)
{
	return _setLeak(1, argc, argv);
}


static int cmd_factory_test_term(int argc, char** argv)
{
	wwPumpDrvFactoryTest(wwPumpTestId_term, 1);
	return 0;
}


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


static int cmd_read_sig(int argc, char** argv)
{
	// Print at most 64 bytes of data preceded by the header
	pwrSigReadHex(64);
	return 0;
}


static int cmd_stop_comm(int argc, char** argv)
{
	csEmtrDrvStop();
	return 0;
}


static int cmd_test_leds(int argc, char** argv)
{
	uint8_t		duration = 1;
	esp_err_t	status;
	int			i;

	for (i = 0; i < 5; i++) {
		if ((status = wwPumpDrvFactoryTest(wwPumpTestId_led + i, duration)) != ESP_OK) {
			break;
		}
		SLEEP_MS((duration * 1000) + 300);
	}

	return 0;
}


static struct {
	struct arg_int	*ledNum;
	struct arg_int	*secs;
	struct arg_end	*end;
} led_args;


static int cmd_led_on(int argc, char ** argv)
{
	if (!emtrIsInitialized)
		return 1;

	int nerrors = arg_parse(argc, argv, (void **) &led_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, led_args.end, argv[0]);
        return 1;
    }

    uint8_t	ledNum;
   	uint8_t	secs;

    if (led_args.ledNum->count > 0) {
    	ledNum = (uint8_t)led_args.ledNum->ival[0];
    } else {
    	return 1;
    }

    if (led_args.secs->count > 0) {
    	secs = (uint8_t)led_args.secs->ival[0];
    } else {
    	return 1;
    }

    wwPumpTestId_t	testId = (wwPumpTestId_t)(wwPumpTestId_led + ledNum);

    printf("testId: 0x%02x, duration: %u\n", testId, secs);

    if (wwPumpDrvFactoryTest(testId, secs) == ESP_OK) {
		return 0;
	}

	return 1;
}


static struct {
	struct arg_int	*secs;
	struct arg_end	*end;
} siren_args;


static int cmd_test_siren(int argc, char** argv)
{
	int nerrors = arg_parse(argc, argv, (void **) &siren_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, siren_args.end, argv[0]);
        return 1;
    }

   	uint8_t	secs;

    if (siren_args.secs->count > 0) {
    	secs = (uint8_t)siren_args.secs->ival[0];
    } else {
    	secs = 2;
    }

    if (secs > 0) {
    	wwPumpDrvFactoryTest(wwPumpTestId_buzzerOn, secs);
    } else {
    	wwPumpDrvSirenSet(false);
    }

	return 0;
}


static int cmd_ilk_start(int argc, char** argv)
{
	if (!emtrIsInitialized)
		return ESP_ERR_INVALID_STATE;

	if (appEmtrCalLeakStart() == ESP_OK) {
		return 0;
	}

	return 1;
}


static int cmd_ilk_read(int argc, char** argv)
{
	uint32_t	value;

	if (appEmtrCalLeakRead(&value) == ESP_OK) {
		printf("%u\n", value);
		return 0;
	}

	return 1;
}


static int cmd_cycle_read(int argc, char** argv)
{
	wwPumpCycleType_t	value;
	const char *	str = NULL;

	if (appEmtrCalGetCycle(&value) == ESP_OK) {
		switch (value)
		{
		case wwPumpCycleType_null:
			str = "null\n";
			break;

		case wwPumpCycleType_regular:
			str = "regular\n";
			break;

		case wwPumpCycleType_systemTest:
			str = "systemTest\n";
			break;

		case wwPumpCycleType_factoryTest:
			str = "factoryTest\n";
			break;

		default:
			break;
		}
	}

	if (str) {
		printf("%s", str);
		return 0;
	} else {
		return 1;
	}
}


static int cmd_pwr_sig_test(int argc, char** argv)
{
	uint32_t	sigCount1;

	// Read current count of power signatures received
	if (pwrSigCount(&sigCount1) != ESP_OK) {
		return 1;
	}

	// ToDo Induce an immediate signature read (waiting for EMTR support for that)

	// Read new count of power signatures received
	uint32_t	sigCount2;
	if (pwrSigCount(&sigCount2) != ESP_OK) {
		return 1;
	}

	// The count should have incremented
	if ((sigCount1 + 1) != sigCount2) {
		return 1;
	}

	return 0;
}


static struct {
	struct arg_int	*vOff;
	struct arg_end	*end;
} atmo_cal_args;

static int cmd_atmo_set(int argc, char** argv)
{
	if (!emtrIsInitialized) {
		return ESP_ERR_INVALID_STATE;
	}

	int nerrors = arg_parse(argc, argv, (void **) &atmo_cal_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, atmo_cal_args.end, argv[0]);
        return 1;
    }

   	int	vOff;

    if (atmo_cal_args.vOff->count > 0) {
    	vOff = atmo_cal_args.vOff->ival[0];
    	if (vOff < 0 ||vOff > 255) {
    		return 1;
    	}
    } else {
    	vOff = 0;
    }

	calAtmoData_t	calData;

	if (appEmtrCalibrationAtmoSet((uint8_t)vOff, &calData) == ESP_OK) {
		printf("{\"atmo_mv\":%u, \"wlevel_mv\":%u}\n", calData.amtoPressMv, calData.wLevelMv);
		return 0;
	}

	return 1;
}


static int cmd_atmo_read(int argc, char** argv)
{
	if (!emtrIsInitialized) {
		return ESP_ERR_INVALID_STATE;
	}

	calAtmoData_t	calData;

	if (appEmtrCalibrationAtmoRead(&calData) == ESP_OK) {
		printf("{\"atmo_mv\":%u, \"wlevel_mv\":%u}\n", calData.amtoPressMv, calData.wLevelMv);
		return 0;
	}

	return 1;
}


static struct {
	struct arg_int	*enable;
	struct arg_end	*end;
} systest_args;

static int cmd_systest_ena(int argc, char** argv)
{
	if (!emtrIsInitialized) {
		return ESP_ERR_INVALID_STATE;
	}

	int nerrors = arg_parse(argc, argv, (void **) &systest_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, systest_args.end, argv[0]);
        return 1;
    }

    bool	enable = (systest_args.enable->ival[0] == 0) ? false : true;

    if (appEmtrSystestEnable(enable) == ESP_OK) {
    	return 0;
    } else {
    	return 1;
    }
}


const static esp_console_cmd_t cmdTab[] = {
	{
		.command = "EMTR-TRIAC-ON",
		.help = "Turn on the triac.",
		.hint = NULL,
		.func = cmd_triac_on
	},
	{
		.command = "EMTR-TRIAC-OFF",
		.help = "Turn off the triac.",
		.hint = NULL,
		.func = cmd_factory_test_term
	},
	{
		.command = "EMTR-LINE-RELAY-ON",
		.help = "Turn on the line relay.",
		.hint = NULL,
		.func = cmd_line_relay_on
	},
	{
		.command = "EMTR-LINE-RELAY-OFF",
		.help = "Turn on the line relay.",
		.hint = NULL,
		.func = cmd_factory_test_term
	},
	{
		.command = "EMTR-NEUTRAL-RELAY-OFF",
		.help = "Turn off the neutral relay (normally on).",
		.hint = NULL,
		.func = cmd_neutral_relay_off
	},
	{
		.command = "EMTR-NEUTRAL-RELAY-ON",
		.help = "Turn on the line relay.",
		.hint = NULL,
		.func = cmd_factory_test_term
	},
	{
		.command = "EMTR-READ-STS",
		.help = "The UUT will respond with the EMTR test status.",
		.hint = NULL,
		.func = cmd_read_test,
	},
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
		.command = "EMTR-READ-ALARMS",
		.help     = "Read fault flags.",
		.hint     = NULL,
		.func     = cmd_read_alarms
	},
	{
		.command = "EMTR-SET-ILK-LO",
		.help     = "Set 0.1 MA calibration point for leak detect.",
		.hint     = NULL,
		.func     = cmd_set_ilk_lo,
		.argtable = &leak_args
	},
	{
		.command = "EMTR-SET-ILK-HI",
		.help     = "Set 1.0 MA calibration point for leak detect.",
		.hint     = NULL,
		.func     = cmd_set_ilk_hi,
		.argtable = &leak_args
	},
	{
		.command = "EMTR-READ-CAL",
		.help = "The UUT will respond with the EMTR calibration data.",
		.hint = NULL,
		.func = cmd_read_cal,
	},
	{
		.command = "EMTR-SAVE-CAL",
		.help = "Save EMTR calibration data.",
		.hint = NULL,
		.func = cmd_save_cal,
	},
	{
		.command  = "EMTR-COMM-STOP",
		.help     = "Stop communication with EMTR, it should reset the ESP32.",
		.hint     = NULL,
		.func     = cmd_stop_comm
	},
	{
		.command  = "EMTR-SIG-READ",
		.help     = "Read power signature.",
		.hint     = NULL,
		.func     = cmd_read_sig
	},
	{
		.command  = "EMTR-SIG-TEST",
		.help     = "Test reading the power signature.",
		.hint     = NULL,
		.func     = cmd_pwr_sig_test
	},
	{
		.command  = "EMTR-LEDS-TEST",
		.help     = "Turn on EMTR LEDS for 2 seconds.",
		.hint     = NULL,
		.func     = cmd_test_leds
	},
	{
		.command  = "EMTR-LED-ON",
		.help     = "Turn on EMTR LED 0..4 for 1..200 seconds",
		.hint     = NULL,
		.func     = cmd_led_on,
		.argtable = &led_args
	},
	{
		.command  = "EMTR-SIREN-TEST",
		.help     = "Sound siren for (n) seconds.",
		.hint     = NULL,
		.func     = cmd_test_siren,
		.argtable = &siren_args
	},
	{
		.command  = "EMTR-ILK-START",
		.help     = "Start current leak measurement",
		.hint     = NULL,
		.func     = cmd_ilk_start
	},
	{
		.command  = "EMTR-ILK-READ",
		.help     = "Read current leak measurement",
		.hint     = NULL,
		.func     = cmd_ilk_read
	},
	{
		.command  = "EMTR-CYCLE-READ",
		.help     = "Get pump cycle type",
		.hint     = NULL,
		.func     = cmd_cycle_read
	},
	{
		.command  = "EMTR-ATMO-SET",
		.help     = "Calibrate atmospheric pressure",
		.hint     = NULL,
		.func     = cmd_atmo_set,
		.argtable = &atmo_cal_args
	},
	{
		.command  = "EMTR-ATMO-READ",
		.help     = "Read atmospheric pressure calibration",
		.hint     = NULL,
		.func     = cmd_atmo_read
	},
	{
		.command  = "EMTR-SYSTEST-ENA",
		.help     = "Set system test enable state",
		.hint     = NULL,
		.func     = cmd_systest_ena,
		.argtable = &systest_args
	},
};
static const int cmdTabSz = sizeof(cmdTab) / sizeof(esp_console_cmd_t);


void register_emtr(void)
{
	int							i;
	const esp_console_cmd_t *	cmd;

	gain_args.gain = arg_dbl1(NULL, NULL, "<gain>", "floating point value 0.0 to 0.999(9)");
	gain_args.end  = arg_end(1);

	leak_args.value = arg_int1(NULL, NULL, "<leak>", "Leak calibration set point");
	leak_args.end   = arg_end(1);

	led_args.ledNum = arg_int1(NULL, NULL, "<led>", "number 0..4");
	led_args.secs   = arg_int1(NULL, NULL, "<duration>", "seconds 0..200");
	led_args.end    = arg_end(1);

	siren_args.secs   = arg_int1(NULL, NULL, "<duration>", "seconds 0..10");
	siren_args.end    = arg_end(1);

	atmo_cal_args.vOff = arg_int1(NULL, NULL, "<V offset>", "milliamps 0..255");
	atmo_cal_args.end  = arg_end(1);

	systest_args.enable = arg_int1(NULL, NULL, "<int>", "0|1");
	systest_args.end    = arg_end(1);

	for (i = 0, cmd = cmdTab; i < cmdTabSz; i++, cmd++) {
	    ESP_ERROR_CHECK( esp_console_cmd_register(cmd) );
	}
}
