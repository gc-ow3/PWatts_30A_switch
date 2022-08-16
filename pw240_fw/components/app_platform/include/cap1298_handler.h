/*
 * cap1298_handle.h
 *
 *  Created on: Apr 3, 2019
 *      Author: wesd
 */

#ifndef COMPONENTS_APP_PLATFORM_INCLUDE_CAP1298_HANDLER_H_
#define COMPONENTS_APP_PLATFORM_INCLUDE_CAP1298_HANDLER_H_


#include "cap1298_drv.h"

typedef enum {
	cap1298Btn_undef = 0,
	cap1298Btn_outlet1,
	cap1298Btn_outlet2,
	cap1298Btn_center
} cap1298Btn_id;

typedef struct {
	cap1298Btn_id	id;
} cap1298EvtData_t;


esp_err_t cap1298HandlerInit(const cap1298DrvConf_t * conf);

esp_err_t cap1298HandlerStart(void);

#endif /* COMPONENTS_APP_PLATFORM_INCLUDE_CAP1298_HANDLER_H_ */
