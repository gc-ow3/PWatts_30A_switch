/*
 * time_mgr.h
 *
 *  Created on: Jun 11, 2018
 *      Author: wesd
 */

#ifndef __CS_CORE_TIME_MGR_H__
#define __CS_CORE_TIME_MGR_H__

#include "cs_common.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t timeMgrInit(void);

esp_err_t timeMgrStart(void);

void timeMgrSetTimeZone(int tzHours);

void timeMgrSetTimeZoneSeconds(int32_t offSeconds);

bool timeMgrTimeZoneIsSet(void);

void timeMgrSetDstActive(bool active);

void timeMgrSetUtcTime(uint32_t timeVal);

uint32_t timeMgrGetUtcTime(void);

uint32_t timeMgrGetLocalTime(void);

bool timeMgrUtcIsSet(void);

int32_t timeMgrGetUtcAge(void);

uint32_t timeMgrGetUptime(void);

uint64_t timeMgrGetUptimeMs(void);

void printLocalTime(void);

#ifdef __cplusplus
}
#endif

#endif /* __CS_CORE_TIME_MGR_H__ */
