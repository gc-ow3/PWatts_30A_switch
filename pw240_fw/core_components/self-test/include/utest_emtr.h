/*
 * utest_emtr.h
 *
 *  Created on: Jan 24, 2019
 *      Author: wesd
 */

#ifndef COMPONENTS_SELF_TEST_INCLUDE_UTEST_EMTR_H_
#define COMPONENTS_SELF_TEST_INCLUDE_UTEST_EMTR_H_

#include "cs_common.h"
#include "emtr_drv.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t utestEmtr(const emtrDrvConf_t * conf);

#ifdef __cplusplus
extern "C" {
#endif

#endif /* COMPONENTS_SELF_TEST_INCLUDE_UTEST_EMTR_H_ */
