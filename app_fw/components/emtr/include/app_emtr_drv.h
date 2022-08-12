/*
 *
 *  Created on: Jan 18, 2019
 *      Author: wesd
 */

#ifndef COMPONENTS_APP_DRIVER_INCLUDE_APP_EMTR_DRV_H_
#define COMPONENTS_APP_DRIVER_INCLUDE_APP_EMTR_DRV_H_

#include <driver/uart.h>
#include <cJSON.h>

#include "cs_emtr_drv.h"

#ifdef __cplusplus
extern "C" {
#endif


typedef union {
	uint8_t		mask;
	struct {
		uint8_t	relayOff   : 1;	// output == 0 when relay == 1
		uint8_t	relayOn    : 1;	// output == 1 when relay == 0
		uint8_t	gfci       : 1;	// High leakage current
		uint8_t	temp       : 1;	// High temperature
		uint8_t	resvd_4    : 1;
		uint8_t	resvd_5    : 1;
		uint8_t	resvd_6    : 1;
		uint8_t	resvd_7    : 1;
	} item;
} appEmtrAlarm_t;


typedef enum {
	appEmtrState_null = 0,
	appEmtrState_on = 1,
	appEmtrState_off = 2
} appEmtrState_t;

/**
 * \brief Status
 *
 */
typedef struct {
	struct {
		appEmtrState_t	value;
		const char *	str;
	}	relayStatus;
	struct {
		appEmtrState_t	value;
		const char *	str;
	}	outputStatus;
	struct {
		appEmtrAlarm_t	flags;
	} alarm;
	uint8_t	tempC;
} appEmtrStatus_t;


/**
 * \brief Cumulative totals
 */
typedef struct {
	uint32_t	epoch;
	uint32_t	onDuration;
	uint32_t	relayCycles;
	uint32_t	dWattH;
} appEmtrTotals_t;


/**
 * \brief Instant values
 */
typedef struct {
	uint16_t	dVolts;
	uint16_t	mAmps;
	uint16_t	dWatts;
	uint16_t	pFactor;
	uint32_t	uptime;
	uint32_t	relayOnSecs;
} appEmtrInstant_t;

typedef enum {
	appEmtrEvtCode_null = 0,
	appEmtrEvtCode_relayState,
	appEmtrEvtCode_outputState,
	appEmtrEvtCode_temperature,
	appEmtrEvtCode_dVolts,
	appEmtrEvtCode_alarms
} appEmtrEvtCode_t;

typedef union {
	struct {
		appEmtrState_t	value;
		const char *	str;
	} state;
	struct {
		uint8_t		value;
	} temperature;
	struct {
		uint16_t	value;
	} dVolts;
	struct {
		appEmtrAlarm_t	flags;
	} alarms;
} appEmtrEvtData_t;

typedef void (*appEmtrDrvEventCb_t)(appEmtrEvtCode_t evtCode, appEmtrEvtData_t* evtData, void* cbData);


esp_err_t appEmtrDrvInit(void);

esp_err_t appEmtrDrvStart(void);

esp_err_t appEmtrDrvGetStatus(appEmtrStatus_t * ret);

esp_err_t appEmtrDrvGetTotals(appEmtrTotals_t * ret);

esp_err_t appEmtrDrvGetInstant(appEmtrInstant_t * ret);

const char * appEmtrDrvStateStr(appEmtrState_t value);

esp_err_t appEmtrDrvSetRelay(bool on);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_APP_DRIVER_INCLUDE_APP_EMTR_DRV_H_ */
