/*
 * ww_pump_drv.h
 *
 *  Created on: Jan 18, 2019
 *      Author: wesd
 */

#ifndef COMPONENTS_APP_DRIVER_INCLUDE_EMTR_PWR_SIG_H_
#define COMPONENTS_APP_DRIVER_INCLUDE_EMTR_PWR_SIG_H_

#include <stdint.h>

#include "driver/uart.h"
#include "driver/gpio.h"
#include "cs_emtr_drv.h"

#ifdef __cplusplus
extern "C" {
#endif


// The power signature payload has a 15 byte header
//   1 byte reason code
//   4 bytes pump cycles
//   4 bytes epoch
//   4 bytes pump on time
//   1 byte  water level 0..5
//   1 byte  GFCI leak level 0..5
// There will be up to 2000 samples, each uses 4 bytes:
//   2 bytes for voltage
//   2 bytes for current
#define PWR_SIG_HDR_SZ		(15)
#define PWR_SIG_SMP_SZ		(4)
#define PWR_SIG_MAX_SAMPLES	(2000)
#define PWR_SIG_MAX_SIG_SZ	(PWR_SIG_MAX_SAMPLES * PWR_SIG_SMP_SZ)

/**
 * These correspond to values passed from EMTR, don't change them
 */
typedef enum {
	pwrSigReason_off    = 0,
	pwrSigReason_on     = 1,
	pwrSigReason_demand = 2,
} pwrSigReason_t;

const char* pwrSigReasonStr(pwrSigReason_t reason);


typedef struct {
	pwrSigReason_t	reason;
	uint16_t		numSamples;
	uint8_t			resolution;
	uint32_t		relayCycles;
	uint32_t		timePowered;
	uint32_t		timeRunning;
	uint32_t		cycleLength;
	uint16_t		mAmps;
	uint16_t		dVolts;
	uint8_t			pFactor;
	uint8_t			temperature;
	uint32_t		mAmpsInrush;
} pwrSigMeta_t;

typedef void (*pwrSigCallback_t)(
	pwrSigMeta_t*	meta,
	uint8_t*		data,
	int				dataLen,
	void*			cbData
);

typedef struct {
	uart_port_t			port;
	gpio_num_t			rxGpio;
	uint32_t			baudRate;
	pwrSigCallback_t	cbFunc;
	void*				cbData;
	UBaseType_t			taskPriority;
} pwrSigConf_t;


esp_err_t pwrSigInit(pwrSigConf_t* conf);

esp_err_t pwrSigStart(void);

esp_err_t pwrSigCount(uint32_t * ret);

void pwrSigReadHex(int maxLen);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_APP_DRIVER_INCLUDE_EMTR_PWR_SIG_H_ */
