/*
 * app_emtr_cal.c
 *
 *  Created on: Feb 21, 2020
 *      Author: wesd
 */
#include <esp_err.h>
#include "app_emtr_cal.h"

//#include <esp_log.h>
//static const char TAG[] = {"emtr_cal"};


esp_err_t appEmtrCalibrateSetGain(emtrCalType_t calType, float gain)
{
	uint8_t		cmdCode;

	switch (calType)
	{
	case emtrCalType_voltageGain:
		cmdCode = 0x1A;
		break;

	case emtrCalType_currentGain:
		cmdCode = 0x1B;
		break;

	default:
		return ESP_ERR_INVALID_ARG;
	}

	if (gain < 0.0 || gain >= 1.0) {
		return ESP_ERR_INVALID_ARG;
	}

	uint32_t	param = (uint32_t)(gain * (float)0x7fffffff);

	uint8_t	payload[4] = {
		(uint8_t)(param >> 24),
		(uint8_t)(param >> 16),
		(uint8_t)(param >>  8),
		(uint8_t)(param >>  0)
	};

	return csEmtrDrvCommand(cmdCode, payload, NULL, NULL);
}


esp_err_t appEmtrCalibrationDataRead(uint8_t * buf, int * len)
{
	return csEmtrDrvCommand(0x0D, NULL, buf, len);
}


esp_err_t appEmtrCalibrationDataSave(uint8_t * buf, int * len)
{
	return csEmtrDrvCommand(0x0E, NULL, buf, len);
}


esp_err_t appEmtrCalibrationUnpack(uint8_t * inp, int inpLen, appEmtrCalData_t * cal)
{
#if 0	// ToDo - update for PW
	csPacker_t	pack;
	uint32_t	temp32;

	csPackInit(&pack, inp, inpLen);

	// Bytes 0-3: U-Gain
	csUnpackBEU32(&pack, &temp32);
	cal->uGain = (float)temp32 / (float)0x7fffffff;
	// Bytes 4-7: I-Gain
	csUnpackBEU32(&pack, &temp32);
	cal->iGain = (float)temp32 / (float)0x7fffffff;
	// Byte 16 HCCI flag
	csUnpackU8(&pack, &cal->hcci);
	// Byte 17-18 atmospheric pressure mV
	csUnpackBEU16(&pack, &cal->atmoMv);

	return csPackStatus(&pack);
#else
	return ESP_ERR_INVALID_STATE;
#endif
}
