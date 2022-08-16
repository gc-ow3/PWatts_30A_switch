/*
 * cs_i2c_bus.h
 *
 *  Created on: May 1, 2019
 *      Author: wesd
 */

#ifndef COMPONENTS_CS_DRIVER_INCLUDE_CS_I2C_BUS_H_
#define COMPONENTS_CS_DRIVER_INCLUDE_CS_I2C_BUS_H_

#include "freertos/FreeRTOS.h"
#include "driver/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	i2c_port_t				i2cPort;	// I2C port
	const i2c_config_t *	i2cConf;	// I2C driver configuration
	bool					busIsShared;
	esp_err_t				(*busLock)(int bus, uint32_t waitMs);
	esp_err_t				(*busUnlock)(int bus);
} const csI2cBusConf_t;


esp_err_t csI2cMutexCreate(int bus);

esp_err_t csI2cMutexTake(int bus, uint32_t waitMs);

esp_err_t csI2cMutexGive(int bus);

#ifdef __cplusplus
extern "C" {
#endif

#endif /* COMPONENTS_CS_DRIVER_INCLUDE_CS_I2C_BUS_H_ */
