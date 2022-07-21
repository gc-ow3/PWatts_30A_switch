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

// Espressif IDF headers
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <esp_err.h>
#include <esp_wifi.h>

// FreeRTOS headers
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>

// LwIP headers
#include <lwip/err.h>
#include <lwip/sys.h>


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

#define SLEEP_MS(ms)		vTaskDelay(pdMS_TO_TICKS(ms))

/**
 * \brief Convert between degrees Fahrenheit and Celsius
 */
#define CS_F2C(v)			((((float)(v)) - 32.0) / 1.8)
#define CS_C2F(v)			(((int)(((float)(v)) * 1.8)) + 32)

/**
 * \brief return absolute value of a float
 */
#define CS_FABS(fv)			((fv) >= 0.0 ? (fv) : -(fv))


#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_CS_CORE_INCLUDE_CS_COMMON_H_ */
