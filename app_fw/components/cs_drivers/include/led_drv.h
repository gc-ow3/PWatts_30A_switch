/*
 * led_drv.h
 *
 */

#ifndef COMPONENTS_APP_DRIVER_INCLUDE_LED_DRV_H_
#define COMPONENTS_APP_DRIVER_INCLUDE_LED_DRV_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Define LED ID numbers
// Don't change these value as they reference positions in the LED
// driver control structures
typedef enum {
	ledId_null    = 0,
	ledId_system  = 1,
	ledId_ble     = 2
} ledId_t;


typedef enum {
	ledMode_off = 0,
	ledMode_red,
	ledMode_grn,
	ledMode_blu
} ledMode_t;


// LED control functions
esp_err_t ledDrvInit(int percent);

esp_err_t ledDrvStart(void);

esp_err_t ledDrvSetMode(ledId_t led, ledMode_t mode);

esp_err_t ledDrvSetBrightness(int percent);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_APP_DRIVER_INCLUDE_LED_DRV_H_ */
