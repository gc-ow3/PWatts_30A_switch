/*
 * app_self_test.h
 *
 *  Created on: Apr 3, 2019
 *      Author: wesd
 */

#ifndef COMPONENTS_SELF_TEST_INCLUDE_APP_SELF_TEST_H_
#define COMPONENTS_SELF_TEST_INCLUDE_APP_SELF_TEST_H_

#include "cs_platform.h"
#include "cs_heap.h"
#include "cJSON.h"
#include "esp_http_server.h"
#include "cs_self_test.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	httpd_handle_t		httpHandle;
} appSelfTestCfg_t;


esp_err_t appSelfTestInit(appSelfTestCfg_t * cfg);

esp_err_t appSelfTestTerm(void);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_SELF_TEST_INCLUDE_APP_SELF_TEST_H_ */
