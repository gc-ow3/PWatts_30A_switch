/*
 * app_control.h
 *
 *  Created on: Feb 18, 2019
 *      Author: wesd
 */

#ifndef MAIN_INCLUDE_APP_CONTROL_H_
#define MAIN_INCLUDE_APP_CONTROL_H_

#include "event_callback.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	appCtrlEvtCode_null = 0,
	appCtrlEvtCode_start,
	appCtrlEvtCode_buttonEnable,
	appCtrlEvtCode_sendOnline
} appCtrlEvtCode_t;

typedef	union {
	bool			enabled;
	const char *	str;
} appCtrlEvtData_t;


esp_err_t appControlInit(void);

esp_err_t appControlStart(void);

esp_err_t appControlCallbackRegister(eventCbFunc_t cbFunc, uint32_t cbData);

esp_err_t appControlCallbackUnregister(eventCbFunc_t cbFunc);

esp_err_t appControlButtonEnable(bool value, callCtx_t ctx);

esp_err_t appControlReportOnline(callCtx_t ctx);

esp_err_t appControlSetOutletName(int oNum, const char * value, callCtx_t ctx);

esp_err_t appControlSetOutletType(int oNum, uint32_t value, callCtx_t ctx);


typedef enum {
	appCtrlSignal_null = 0,
} appCtrlSignal_t;

esp_err_t appControlSignal(appCtrlSignal_t signal);


#ifdef __cplusplus
}
#endif

#endif /* MAIN_INCLUDE_APP_CONTROL_H_ */
