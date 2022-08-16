/*
 * cs_prov_support.h
 *
 *  Created on: Jan 16, 2019
 *      Author: wesd
 */

#ifndef COMPONENTS_PROVISION_INCLUDE_CS_PROV_SUPPORT_H_
#define COMPONENTS_PROVISION_INCLUDE_CS_PROV_SUPPORT_H_

#include "event_callback.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	bool		ssidIsSet;
	uint8_t		ssid[32];
	int			ssidLen;
	bool		passwordIsSet;
	uint8_t		password[64];
	int			passwordLen;
} wifiStaConf_t;


typedef enum {
	csProvState_unprovisioned = 0,
	csProvState_provisioning,
	csProvState_provisioned,
	csProvState_internalError
} csProvState_t;

esp_err_t csProvStateSet(csProvState_t provState);

csProvState_t csProvStateGet(void);

const char * csProvStateStr(csProvState_t provState);

void csProvProgressSet(const char * progress);

const char * csProvProgressGet(void);

esp_err_t csProvConfLoad(wifiStaConf_t * conf);

esp_err_t csProvConfStore(wifiStaConf_t * conf);

esp_err_t csProvConfErase(void);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_PROVISION_INCLUDE_CS_PROV_SUPPORT_H_ */
