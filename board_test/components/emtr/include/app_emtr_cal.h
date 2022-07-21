/*
 * app_emtr_cal.h
 *
 *  Created on: Feb 21, 2020
 *      Author: wesd
 */

#ifndef COMPONENTS_APP_DRIVER_INCLUDE_APP_EMTR_CAL_H_
#define COMPONENTS_APP_DRIVER_INCLUDE_APP_EMTR_CAL_H_

#include "cs_emtr_drv.h"
#include "ww_pump_drv.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	emtrCalType_voltageGain,
	emtrCalType_currentGain
} emtrCalType_t;


esp_err_t appEmtrCalibrateSetGain(emtrCalType_t calType, float gain);

//esp_err_t appEmtrCalibrationReadLeak(uint32_t * ret, uint64_t timeLimitMs);

esp_err_t appEmtrCalLeakStart(void);

esp_err_t appEmtrCalLeakRead(uint32_t * ret);

esp_err_t appEmtrCalGetCycle(wwPumpCycleType_t * cycle);

esp_err_t appEmtrCalibrationSetLeakLo(uint32_t value);

esp_err_t appEmtrCalibrationSetLeakHi(uint32_t value);

typedef struct {
	uint16_t	amtoPressMv;
	uint16_t	wLevelMv;
} calAtmoData_t;

esp_err_t appEmtrCalibrationAtmoSet(uint8_t vOff, calAtmoData_t *retCal);

esp_err_t appEmtrCalibrationAtmoRead(calAtmoData_t *retCal);

esp_err_t appEmtrSystestEnable(bool enable);

esp_err_t appEmtrCalibrationDataRead(uint8_t * buf, int * len);

esp_err_t appEmtrCalibrationDataSave(uint8_t * buf, int * len);


#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_APP_DRIVER_INCLUDE_APP_EMTR_CAL_H_ */
