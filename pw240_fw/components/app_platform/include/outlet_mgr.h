/*
 * outlet_mgr.h
 *
 *  Created on: Feb 18, 2019
 *      Author: wesd
 */

#ifndef COMPONENTS_APP_PLATFORM_INCLUDE_OUTLET_MGR_H_
#define COMPONENTS_APP_PLATFORM_INCLUDE_OUTLET_MGR_H_

#include "cs_common.h"
#include "event_callback.h"
#include "emtr_drv.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief Events that may be reported by the outlet manager
 */
typedef enum {
	outletMgrEvt_setType,
	outletMgrEvt_setName,
	outletMgrEvt_setTargetState,
	outletMgrEvt_setDeltaSource
} outletMgrEvtCode_t;


/**
 * \brief Source of changes
 *
 * Changes to a device operation may be initiated via:
 *   voice    A voice command to a phone or smart speaker
 *   app      Selecting an app function
 *   user     A button press on the device itself
 *   internal An internal process such as a timer
 * This information is useful to the server. When a command is initiated via
 * voice or app, this is reflected as either "VI" or "AI". When the changed is
 * done by user or internally, the device will set source to "PI"
 *
 */
typedef enum {
	appDeltaSrc_null = 0,
	appDeltaSrc_voice,
	appDeltaSrc_app,
	appDeltaSrc_user,
	appDeltaSrc_internal
} appDeltaSrc_t;


/**
 * \brief Outlet manager event-specific data
 */
typedef struct {
	int				socketNum;
	union {
		const char *	name;
		uint32_t		type;
		bool			on;
	}	data;
} outletMgrEvtData_t;

esp_err_t outletMgrInit(void);

esp_err_t outletMgrStart(void);

esp_err_t outletMgrCallbackRegister(eventCbFunc_t cbFunc, uint32_t cbData);

esp_err_t outletMgrSetSocket(int sockNum, bool value, callCtx_t ctx, appDeltaSrc_t delta);

esp_err_t outletMgrSetType(int sockNum, uint32_t type, callCtx_t ctx);

esp_err_t outletMgrSetName(int sockNum, const char * name, callCtx_t ctx);

esp_err_t outletMgrSetSource(int sockNum, appDeltaSrc_t value, callCtx_t ctx);

esp_err_t outletMgrGetSource(int sockNum, appDeltaSrc_t * value);

const char * outletMgrDeltaSrcToStr(appDeltaSrc_t src);

const char * outletMgrGetSourceStr(int oNum);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_APP_PLATFORM_INCLUDE_OUTLET_MGR_H_ */
