/*
 * inp_drv.h
 *
 *  Created on: Jul 19, 2022
 *      Author: wesd
 */

#ifndef COMPONENTS_CS_DRIVERS_INCLUDE_INP_DRV_H_
#define COMPONENTS_CS_DRIVERS_INCLUDE_INP_DRV_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	inpId_null = 0,
	inpId_button1,
	inpId_button2,
	inpId_switch1,
} inpId_t;

typedef void (*inpCbFunc_t)(inpId_t inpId, bool active, void* cbData);

esp_err_t inpDrvInit(inpCbFunc_t cbFunc, void* cbData);

esp_err_t inpDrvStart(void);

esp_err_t inpDrvRead(inpId_t id, bool* active);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_CS_DRIVERS_INCLUDE_INP_DRV_H_ */
