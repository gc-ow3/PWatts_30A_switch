/*
 * cs_i2c_bus.h
 *
 *  Created on: May 1, 2019
 *      Author: wesd
 */

#ifndef COMPONENTS_APP_DRIVERS_INCLUDE_LED_DRV_H_
#define COMPONENTS_APP_DRIVERS_INCLUDE_LED_DRV_H_

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	ledNum_system  = 0,
	ledNum_ble = 1
} ledNum_t;

typedef enum {
	ledMode_off,
	ledMode_red,
	ledMode_grn,
	ledMode_blu,
} ledMode_t;

/**
 * @brief Initialize the LED driver
 *
 * @return
 *    ESP_OK Success
 *    ESP_ERR_NO_MEM Not enough free memory
 */
esp_err_t ledDrvInit(void);


/**
 * @brief Start the LED driver
 *
 * @return
 *    ESP_OK on success
 *    ESP_ERR_INVALID_STATE driver not initialized
 */
esp_err_t ledDrvStart(void);


/**
 * @brief Set LED mode (Off, Red, Green, Blue)
 *
 * @param led Select LED
 * @param mode Mode
 *
 * @return
 *   ESP_OK Success
 *   ESP_INVALID_ARG led or mode invalid
 *
 */
esp_err_t ledDrvSetMode(ledNum_t led, ledMode_t mode);

#ifdef __cplusplus
}
#endif

#endif
