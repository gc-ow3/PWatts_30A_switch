/*
 * cs_common.h
 *
 *  Created on: Dec 13, 2018
 *      Author: wesd
 */

#ifndef COMPONENTS_CS_CORE_INCLUDE_CS_COMMON_H_
#define COMPONENTS_CS_CORE_INCLUDE_CS_COMMON_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "sdkconfig.h"

// Standard C headers
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>

// FreeRTOS headers
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>

// Espressif IDF headers
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <esp_wifi.h>
#include <esp_err.h>

// LwIP headers
#include <lwip/err.h>
#include <lwip/sys.h>

#include "cs_control.h"
#include "cs_lr_prov.h"

#ifndef CONFIG_LOG_DEFAULT_LEVEL
#define CONFIG_LOG_DEFAULT_LEVEL	(0)
#endif

#ifndef bool

#define bool	int8_t
#define false	0
#define true	1

#endif


/*
********************************************************************************
* Helper macros
********************************************************************************
*/
#define CS_PTR2ADR(p)		((uint32_t)(p))
#define CS_ADR2PTR(a)		((void *)(a))
#define CS_BOOL2STR(b)		((b) ? "true" : "false")

/**
 * \brief Convert between degrees Fahrenheit and Celsius
 */
#define CS_F2C(v)			((((float)(v)) - 32.0) / 1.8)
#define CS_C2F(v)			(((int)(((float)(v)) * 1.8)) + 32)

/**
 * \brief return absolute value of a float
 */
#define CS_FABS(fv)			((fv) >= 0.0 ? (fv) : -(fv))


/*
********************************************************************************
* Core task priorities
********************************************************************************
*/
#define CS_TASK_PRIO_CONTROL		(tskIDLE_PRIORITY + 14)
#define CS_TASK_PRIO_PARAM_MGR		(tskIDLE_PRIORITY + 11)
#define CS_TASK_PRIO_PROVISION		(tskIDLE_PRIORITY + 11)


/*
********************************************************************************
* type definitions
********************************************************************************
*/

typedef enum {
	csWifiMode_disable = 0,
	csWifiMode_ap,
	csWifiMode_sta
} csWifiMode_t;

typedef struct {
	const char *	manufacturer;
	const char *	product;
	const char *	model;
	const char *	fwVersion;
} csCoreInit0_t;


typedef struct {
	csControlConf_t 	controlConf;
} csCoreInit1_t;


typedef struct {
	csCoreInit0_t	info;
	csCoreInit1_t	params;
	char			baseMacStr[20];
#if CONFIG_NVS_ENCRYPTION
	nvs_sec_cfg_t	nvsKeys;
#endif
} csCoreConf_t;


typedef struct {
	csLrProvConf_t		provConf;
} csCoreStartParams_t;


/*
********************************************************************************
* Global core data
********************************************************************************
*/

extern csCoreConf_t		csCoreConf;
extern const char		csCoreVersion[];


/*
********************************************************************************
* API functions
********************************************************************************
*/
esp_err_t csCoreInit0(csCoreInit0_t * info);

esp_err_t csCoreInit1(csCoreInit1_t * params);

esp_err_t csCoreStart(csCoreStartParams_t * params);


#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_CS_CORE_INCLUDE_CS_COMMON_H_ */
