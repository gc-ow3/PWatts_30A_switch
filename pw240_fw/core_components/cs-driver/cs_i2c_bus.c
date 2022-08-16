/*
 * i2c_mutex.c
 *
 *  Created on: Apr 15, 2019
 *      Author: wesd
 */


#include "cs_i2c_bus.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t	mutex[2];


esp_err_t csI2cMutexCreate(int bus)
{
	if (bus < 0 || bus > 1)
		return ESP_ERR_INVALID_ARG;
	if (NULL != mutex[bus])
		return ESP_OK;

	if ((mutex[bus] = xSemaphoreCreateMutex()) == NULL)
		return ESP_FAIL;

	return ESP_OK;
}


esp_err_t csI2cMutexTake(int bus, uint32_t waitMs)
{
	if (bus < 0 || bus > 1)
		return ESP_ERR_INVALID_ARG;

	if (NULL == mutex[bus]) {
		return ESP_FAIL;
	}

	if (xSemaphoreTake(mutex[bus], pdMS_TO_TICKS(waitMs)) == pdTRUE) {
		return ESP_OK;
	} else {
		return ESP_FAIL;
	}
}


esp_err_t csI2cMutexGive(int bus)
{
	if (bus < 0 || bus > 1)
		return ESP_ERR_INVALID_ARG;
	if (NULL == mutex[bus])
		return ESP_FAIL;

	if (xSemaphoreGive(mutex[bus]) == pdTRUE)
		return ESP_OK;
	else
		return ESP_FAIL;
}
