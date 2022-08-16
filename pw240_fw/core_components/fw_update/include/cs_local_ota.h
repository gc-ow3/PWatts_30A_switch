/*
 * cs_local_ota.h
 *
 *  Created on: Jun 24, 2019
 *      Author: wesd
 */

#ifndef COMPONENTS_FW_UPDATE_INCLUDE_CS_LOCAL_OTA_H_
#define COMPONENTS_FW_UPDATE_INCLUDE_CS_LOCAL_OTA_H_

#include "cs_common.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t csLocalOtaInit(httpd_handle_t	httpHandle);

esp_err_t csLocalOtaTerm(void);



#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_FW_UPDATE_INCLUDE_CS_LOCAL_OTA_H_ */
