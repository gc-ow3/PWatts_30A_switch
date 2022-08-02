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
