/*
 * app_emtr_cal.h
 *
 *  Created on: Feb 21, 2020
 *      Author: wesd
 */

#ifndef COMPONENTS_APP_DRIVER_INCLUDE_APP_EMTR_CAL_H_
#define COMPONENTS_APP_DRIVER_INCLUDE_APP_EMTR_CAL_H_

#include <esp_err.h>

#include "cs_emtr_drv.h"
#include "app_emtr_drv.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	emtrCalType_voltageGain,
	emtrCalType_currentGain
} emtrCalType_t;


esp_err_t appEmtrCalibrateSetGain(emtrCalType_t calType, float gain);

esp_err_t appEmtrCalibrationDataRead(uint8_t * buf, int * len);

esp_err_t appEmtrCalibrationDataSave(uint8_t * buf, int * len);


#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_APP_DRIVER_INCLUDE_APP_EMTR_CAL_H_ */
