/*
 * led_mgr.h
 *
 *  Created on: Feb 11, 2019
 *      Author: wesd
 */

#ifndef COMPONENTS_APP_PLATFORM_H_LED_MGR_H_
#define COMPONENTS_APP_PLATFORM_H_LED_MGR_H_

#include <stdint.h>
#include "app_led_drv.h"
#include "event_callback.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	ledMgrEvt_setBrightness
} ledMgrEvt_t;


int ledMgrInit(uint8_t brightness);

int ledMgrStart(void);

int ledMgrCallbackRegister(eventCbFunc_t cbFunc, uint32_t cbData);

int ledMgrTurnLedOn(csLedNum_t led);

int ledMgrTurnLedOff(csLedNum_t led);

esp_err_t ledMgrSetLedColor(csLedNum_t led, csLedColor_t color);

csLedColor_t ledMgrGetLedColor(csLedNum_t ledNum);

int ledMgrFlashLed(
	csLedNum_t	led,
	uint32_t	onTimeMs,
	uint32_t	offTimeMs,
	uint32_t	numCycles,
	uint32_t	repeatDelay
);

int ledMgrSetBrightness(int level, callCtx_t ctx);

void ledMgrFadeUp(uint32_t holdTime);

void ledMgrFadeDown(void);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_APP_PLATFORM_H_LED_MGR_H_ */
