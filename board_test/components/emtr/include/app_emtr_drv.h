/*
 * ww_pump_drv.h
 *
 *  Created on: Jan 18, 2019
 *      Author: wesd
 */

#ifndef COMPONENTS_APP_DRIVER_INCLUDE_APP_EMTR_DRV_H_
#define COMPONENTS_APP_DRIVER_INCLUDE_APP_EMTR_DRV_H_

#include "driver/uart.h"
#include "cJSON.h"

#include "cs_emtr_drv.h"

#ifdef __cplusplus
extern "C" {
#endif


typedef union {
	uint8_t		mask;
	struct {
		uint8_t	resvd_0    : 1;
		uint8_t	resvd_1    : 1;
		uint8_t	acLine     : 1;
		uint8_t	resvd_3    : 1;
		uint8_t	highTemp   : 1;
		uint8_t	overload   : 1;
		uint8_t	underload  : 1;
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
	uint8_t				tempC;
	struct {
		appEmtrState_t	value;
		const char *	str;
	}					pumpState;
	struct {
		appEmtrAlarm_t	flags;
	}					alarm;
	bool				pwrSigUpdated;
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
	uint32_t	relayOnSecs;
} appEmtrInstant_t;

typedef enum {
	appEmtrEvtCode_null = 0,
	appEmtrEvtCode_state,
	appEmtrEvtCode_temperature,
	appEmtrEvtCode_pwrSig,
	appEmtrEvtCode_dVolts,
} appEmtrEvtCode_t;

typedef union {
	struct {
		appEmtrState_t	value;
		const char *	str;
		appEmtrTotals_t	totals;
	} state;
	struct {
		pwrSigMeta_t *	pMeta;
	} pwrSig;
	struct {
		uint8_t		value;
	} temperature;
	struct {
		uint16_t			value;
	} dVolts;
} appEmtrEvtData_t;


esp_err_t appEmtrDrvInit(void);

esp_err_t appEmtrDrvStart(void);

esp_err_t appEmtrDrvGetInstValues(appEmtrInstant_t * ret);

esp_err_t appEmtrDrvGetTotals(appEmtrTotals_t * ret);

esp_err_t appEmtrDrvGetSignature(uint8_t * buf, int * ioLen);

const char * appEmtrDrvStateStr(appEmtrState_t value);

// The values here correspond to EMTR command codes
// Do not change them
typedef enum {
	appEmtrTestId_lineRelayOn     = 0x03,
	appEmtrTestId_term            = 0xff,
} appEmtrTestId_t;

esp_err_t appEmtrDrvFactoryTest(appEmtrTestId_t testId, uint8_t duration);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_APP_DRIVER_INCLUDE_APP_EMTR_DRV_H_ */
