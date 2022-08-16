/*
 * cap1298_drv.h
 *
 *  Created on: Jan 24, 2019
 *      Author: wesd
 */
#ifndef COMPONENTS_APP_DRIVER_INCLUDE_CAP1298_DRV_H_
#define COMPONENTS_APP_DRIVER_INCLUDE_CAP1298_DRV_H_

#include "driver/i2c.h"
#include "cs_common.h"
#include "cs_i2c_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CAP_TOUCH_PAD1		(1 << 0)	// Not used
#define CAP_TOUCH_PAD2		(1 << 1)	// Outlet 1
#define CAP_TOUCH_PAD3		(1 << 2)	// No connection
#define CAP_TOUCH_PAD4		(1 << 3)	// Center
#define CAP_TOUCH_PAD5		(1 << 4)	// No connection
#define CAP_TOUCH_PAD6		(1 << 5)	// No connection
#define CAP_TOUCH_PAD7		(1 << 6)	// Not used
#define CAP_TOUCH_PAD8		(1 << 7)	// Outlet 2

typedef struct {
	UBaseType_t			taskPriority;
	uint8_t				i2cAddr;
	csI2cBusConf_t *	i2cBusConf;
} cap1298DrvConf_t;


//! Events that may be reported by the driver
typedef enum {
	capDrvEvt_null = 0,
	capDrvEvt_press,
	capDrvEvt_release
} capDrvEvt_t;

typedef enum {
	capDrvSource_null = 0,
	capDrvSource_A,
	capDrvSource_B,
	capDrvSource_center
} capDrvSource_t;


// Form of the CAP driver callback function
typedef	void	(* capDrvCallback_t)(
	uint32_t		cbData,
	capDrvEvt_t		evtCode,
	capDrvSource_t	evtSource
);


// Structure of registers read from controller during touch test
typedef struct {
	uint8_t		deltaCount;
	uint8_t		baseCount;
} touchReg_t;


// The CAP driver initialization function
esp_err_t cap1298DrvInit(const cap1298DrvConf_t * conf);

esp_err_t cap1298DrvStart(capDrvCallback_t cbFunc, uint32_t cbData);

esp_err_t cap1298DrvCalibrate(void);

uint8_t cap1298DrvActiveSensors(void);

esp_err_t cap1298ReadTouchRegs(uint8_t padNum, touchReg_t * regs);


#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_APP_DRIVER_INCLUDE_CAP1298_DRV_H_ */
