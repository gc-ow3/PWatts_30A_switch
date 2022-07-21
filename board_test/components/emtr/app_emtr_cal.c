/*
 * app_emtr_cal.c
 *
 *  Created on: Feb 21, 2020
 *      Author: wesd
 */
#include <esp_err.h>
#include "esp_log.h"

#include "app_emtr_cal.h"

#define	CS_SLEEP_MS(t)			vTaskDelay(pdMS_TO_TICKS(t))

// Read number of seconds since boot
#define	TIME_SEC()				(esp_timer_get_time()/1000000)
#define	TIME_MS()				(esp_timer_get_time()/1000)

static const char TAG[] = {"emtr_cal"};


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


esp_err_t appEmtrCalLeakStart(void)
{
	esp_err_t	status;

	// Start the leak measurement cycle
	// This can take several seconds to complete
	// The pump will enter the factory test cycle
	if ((status = csEmtrDrvCommand(0x40, NULL, NULL, NULL)) != ESP_OK) {
		ESP_LOGE(TAG, "Error %x starting current leak measurement", status);
		return status;
	}

	return ESP_OK;
}


esp_err_t appEmtrCalLeakRead(uint32_t * ret)
{
	esp_err_t	status;
	uint8_t		resp[4];
	int			respLen = sizeof(resp);

	if ((status = csEmtrDrvCommand(0x43, NULL, resp, &respLen)) == ESP_OK) {
		*ret =
			(((uint32_t)resp[0]) << 24) |
			(((uint32_t)resp[1]) << 16) |
			(((uint32_t)resp[2]) <<  8) |
			(((uint32_t)resp[3]) <<  0);
	} else {
		ESP_LOGE(TAG, "Error %x reading current leak measurement result", status);
	}

	return status;
}


static esp_err_t setLeakCal(uint8_t cmd, uint32_t value)
{
	uint8_t	payload[4] = {
		(uint8_t)(value >> 24),
		(uint8_t)(value >> 16),
		(uint8_t)(value >>  8),
		(uint8_t)(value >>  0)
	};

	return csEmtrDrvCommand(cmd, payload, NULL, NULL);
}


// Set GFCI leak calibration for 0.1 MA
esp_err_t appEmtrCalibrationSetLeakLo(uint32_t value)
{
	return setLeakCal(0x41, value);
}


// Set GFCI leak calibration for 1.0 MA
esp_err_t appEmtrCalibrationSetLeakHi(uint32_t value)
{
	return setLeakCal(0x42, value);
}


esp_err_t appEmtrSystestEnable(bool enable)
{
	uint8_t		payload[4] = {0, 0, 0, 0};
	uint8_t		ret[4];
	int			retLen;
	esp_err_t	status;

	if (enable) {
		payload[0] = 0xDA;
	}

#if 1
	printf("\nSend command 0x51: %02X %02X %02X %02X\n",
		payload[0], payload[1], payload[2], payload[3]
	);
#endif

	retLen = sizeof(ret);
	if ((status = csEmtrDrvCommand(0x51, payload, ret, &retLen)) != ESP_OK) {
		return status;
	}

	printf("\n%02X\n", ret[0]);

	uint8_t	calData[32];
	int		calLen = sizeof(calData);

	if ((status = appEmtrCalibrationDataSave(calData, &calLen)) != ESP_OK) {
		return status;
	}

	retLen = sizeof(ret);
	if ((status = csEmtrDrvCommand(0x50, payload, ret, &retLen)) != ESP_OK) {
		return status;
	}

	printf("%02X\n", ret[0]);

	return ESP_OK;
}


esp_err_t appEmtrCalibrationDataRead(uint8_t * buf, int * len)
{
	return csEmtrDrvCommand(0x0D, NULL, buf, len);
}


esp_err_t appEmtrCalibrationDataSave(uint8_t * buf, int * len)
{
	return csEmtrDrvCommand(0x0E, NULL, buf, len);
}
