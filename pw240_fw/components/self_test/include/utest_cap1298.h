/*
 * utest_cap1298.h
 *
 *  Created on: Jan 24, 2019
 *      Author: wesd
 */

#ifndef COMPONENTS_SELF_TEST_INCLUDE_UTEST_CAP1298_H_
#define COMPONENTS_SELF_TEST_INCLUDE_UTEST_CAP1298_H_

#include "cs_common.h"
#include "cap1298_drv.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t utestCap1298(const cap1298DrvConf_t * conf);

#ifdef __cplusplus
extern "C" {
#endif

#endif /* COMPONENTS_SELF_TEST_INCLUDE_UTEST_CAP1298_H_ */
