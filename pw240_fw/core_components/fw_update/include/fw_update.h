/*
 * fw_update.h
 *
 *  Created on: Jul 31, 2018
 *      Author: wesd
 */

#ifndef FW_UPDATE_FW_UPDATE_H_
#define FW_UPDATE_FW_UPDATE_H_

#include "cs_common.h"
#include "cs_heap.h"
#include "esp_http_client.h"

#ifdef __cplusplus
extern "C" {
#endif


esp_err_t csFwUpdate(esp_http_client_config_t * configIn);
const char * csFwUpdateFailReason(esp_err_t errCode);

#ifdef __cplusplus
}
#endif

#endif /* FW_UPDATE_FW_UPDATE_H_ */
