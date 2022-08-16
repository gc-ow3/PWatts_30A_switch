/*
 * cs_lr_prov.h
 *
 *  Created on: June 15, 2022
 *      Author: jonw
 */

#ifndef COMPONENTS_CS_AP_PROV_INCLUDE_CS_AP_PROV_H_
#define COMPONENTS_CS_AP_PROV_INCLUDE_CS_AP_PROV_H_


#ifdef __cplusplus
extern "C" {
#endif

#include "esp_wifi.h"
#include "esp_http_server.h"
#include "cs_common.h"

typedef struct {
	const char *	ssidFilter;
	
} csLrProvConf_t;


typedef enum {
	csLrProvState_null = 0,
	csLrProvState_initial,
	csLrProvState_unprovisioned,
	csLrProvState_scanning,
	csLrProvState_wifiConnecting,
	csLrProvState_wifiConnected,
	csLrProvState_provisioning,
	csLrProvState_provisioned,
	csLrProvState_provisionFailed
} csLrProvState_t;


/**
 * \brief Start LR provisioning
 *
 * \param [in] conf Pointer to the configuration structure
 *
 * \return ESP_OK On success
 * \return other On failure
 *
 */
esp_err_t csLrProvStart(csLrProvConf_t * conf);


#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_CS_AP_PROV_INCLUDE_CS_AP_PROV_H_ */
