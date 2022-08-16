/*
 * app_control.h
 *
 *  Created on: Feb 7, 2019
 *      Author: wesd
 */

#ifndef COMPONENTS_CS_CORE_INCLUDE_CS_CONTROL_H_
#define COMPONENTS_CS_CORE_INCLUDE_CS_CONTROL_H_

#include "event_callback.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief core control event codes
 *
 */
typedef enum {
	csCtrlEvtCode_null = 0,
	csCtrlEvtCode_start,
	csCtrlEvtCode_rebootArmed,
	csCtrlEvtCode_rebooting,
	csCtrlEvtCode_resetProvision,
	csCtrlEvtCode_resetFactory,
	csCtrlEvtCode_identify,
	csCtrlEvtCode_fwUpgradeStart,
	csCtrlEvtCode_fwUpgradeSuccess,
	csCtrlEvtCode_fwUpgradeFail,
	csCtrlEvtCode_tzChange,
} csCtrlEvtCode_t;

/**
 * \brief Reboot reason codes
 *
 * NOTE: Do not change the order of existing codes because these are
 * stored in flash. Add new codes to the end
 */
typedef enum {
	csRebootReason_null = 0,
	csRebootReason_command,
	csRebootReason_user,
	csRebootReason_fwUpdate,
	csRebootReason_resetFactory,
	csRebootReason_recovery,
	csRebootReason_localApi,
	csRebootReason_firstBoot,
	csRebootReason_crash,
} csRebootReason_t;


typedef union {
	struct {
		csRebootReason_t	reason;
	} reboot;
	struct {
		const char *	version;
	} fwUpdate;
	struct {
		const char * reason;
	} fwUpgradeFail;
	struct {
		const char * str;
	} setTz;
} csCtrlEvtData_t;

typedef enum {
	csCtrlSignal_null = 0,
	csCtrlSignal_identify,
	csCtrlSignal_rebootArmed,
	csCtrlSignal_resetFactory,
	csCtrlSignal_tzOffsetRequest,
	csCtrlSignal_tzOffsetReceived,
	csCtrlSignal_fwUpgrade,
} csCtrlSignal_t;

typedef struct {
	uint32_t	wifiTimeoutMs;
} csControlConf_t;

esp_err_t csControlInit(csControlConf_t * conf);

esp_err_t csControlStart(void);

esp_err_t csControlCallbackRegister(eventCbFunc_t cbFunc, uint32_t cbData);

esp_err_t csControlCallbackUnregister(eventCbFunc_t cbFunc);

esp_err_t csControlReboot(uint32_t delayMs, csRebootReason_t reason);

esp_err_t csControlFactoryReset(void);

esp_err_t csControlIdentify(void);

esp_err_t csControlSignal(csCtrlSignal_t signal);

const char * csBootReasonStr(csRebootReason_t code);

void csBootReasonSet(csRebootReason_t reason);

const char * csBootReasonGet(void);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_CS_CORE_INCLUDE_CS_CONTROL_H_ */
