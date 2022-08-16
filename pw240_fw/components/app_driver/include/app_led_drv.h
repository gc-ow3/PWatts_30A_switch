/*
 * led_drv.h
 *
 *  Created on: Aug 3, 2018
 *      Author: wesd
 */

#ifndef COMPONENTS_APP_DRIVER_INCLUDE_LED_DRV_H_
#define COMPONENTS_APP_DRIVER_INCLUDE_LED_DRV_H_

#include <stdint.h>
#include "event_callback.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NUM_LEDS			(4)

// Define LED ID numbers
// Don't change these value as they reference positions in the LED
// driver control structures
typedef enum {
	csLedNum_null    = 0,
	csLedNum_wifi    = 1,
	csLedNum_status  = 2,
	csLedNum_socket1 = 3,
	csLedNum_socket2 = 4
} csLedNum_t;


typedef enum {
	csLedColor_null = 0,
	csLedColor_red,
	csLedColor_green,
	csLedColor_blue,
	csLedColor_yellow
} csLedColor_t;


// LED control functions
esp_err_t appLedDrvInit(int pctBrightness);

esp_err_t appLedDrvStart(void);

esp_err_t appLedDrvSetBrightness(int value);

esp_err_t appLedDrvTurnOn(csLedNum_t led);

esp_err_t appLedDrvTurnOff(csLedNum_t led);

bool appLedDrvLedIsRGB(csLedNum_t led);

esp_err_t appLedDrvSetColor(csLedNum_t led, csLedColor_t color);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_APP_DRIVER_INCLUDE_LED_DRV_H_ */
