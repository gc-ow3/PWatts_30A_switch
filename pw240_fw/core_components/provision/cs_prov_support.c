/*
 * cs_prov_support.c
 *
 *  Created on: Jan 16, 2019
 *      Author: wesd
 */

#include "include/cs_prov_support.h"

#include "cs_common.h"
#include "nvs.h"
#include "event_callback.h"

// Comment out the MOD_NAME line to disable debug prints from this file
#define MOD_NAME	"prov_supp"
#include "mod_debug.h"


////////////////////////////////////////////////////////////////////////////////
// Type definitions
////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////
// Local functions
////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////
// Constant data
////////////////////////////////////////////////////////////////////////////////
static const char	nvsNamespace_prov[]    = {"cs_prov"};
static const char	nvsKey_provSsid[]      = {"prov_ssid"};
static const char	nvsKey_provPass[]      = {"prov_pass"};
static const char	nvsKey_provState[]     = {"prov_state"};


////////////////////////////////////////////////////////////////////////////////
// Function forward references
////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////
// Public data
////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////
// Local data
////////////////////////////////////////////////////////////////////////////////
static const char *	provProgress;


////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////


esp_err_t csProvStateSet(csProvState_t provState)
{
	nvs_handle	nvs;
	esp_err_t	status = ESP_OK;

	status = nvs_open(nvsNamespace_prov, NVS_READWRITE, &nvs);
	if (ESP_OK != status) {
		gc_err("Failed to open NVS namespace %s", nvsNamespace_prov);
		return status;
	}

	// Read current value and check for value already being set
	int8_t	value;
	if (nvs_get_i8(nvs, nvsKey_provState, &value) == ESP_OK) {
		if ((csProvState_t)value == provState) {
			// No change in value
			goto exitNvs;
		}
	}

	gc_dbg("set provState <--- %s", csProvStateStr(provState));

	status = nvs_set_i8(nvs, nvsKey_provState, (int8_t)provState);
	if (ESP_OK == status) {
		status = nvs_commit(nvs);
	} else {
		gc_err("nvs_set_i8 error %d", status);
	}

exitNvs:
	(void)nvs_close(nvs);
	return status;
}


csProvState_t csProvStateGet(void)
{
	nvs_handle	nvs;
	esp_err_t	status;

	status = nvs_open(nvsNamespace_prov, NVS_READONLY, &nvs);
	if (ESP_OK != status) {
		gc_err("Failed to open NVS namespace %s", nvsNamespace_prov);
		return csProvState_internalError;
	}

	int8_t	value;
	status = nvs_get_i8(nvs, nvsKey_provState, &value);
	nvs_close(nvs);

	if (ESP_OK == status) {
		//gc_dbg("provState stored value == %d", value);
	} else if (ESP_ERR_NVS_NOT_FOUND == status) {
		value = (int8_t)csProvState_unprovisioned;
		gc_dbg("provState not stored, default value == %d", value);
	} else {
		gc_err("nvs_get_i8(provState) error %d", status);
		value = (int8_t)csProvState_internalError;
	}

	//gc_dbg("get provState ---> %s", csProvStateStr(value));

	return (csProvState_t)value;
}


const char * csProvStateStr(csProvState_t provState)
{
	switch(provState)
	{
	case csProvState_unprovisioned:
		return "notProvisioned";
	case csProvState_provisioning:
		return "provisioning";
	case csProvState_provisioned:
		return "provisioned";
	case csProvState_internalError:
		return "internalError";
	default:
		return "undefined";
	}
}


void csProvProgressSet(const char * progress)
{
	provProgress = progress;
}


const char * csProvProgressGet(void)
{
	return provProgress;
}


esp_err_t csProvConfLoad(wifiStaConf_t * conf)
{
	nvs_handle	nvs;
	size_t		len;
	int			errCt  = 0;
	esp_err_t	status = ESP_OK;

	if (NULL == conf) {
		return ESP_ERR_INVALID_ARG;
	}

	// Clear the configuration structure
	memset(conf, 0, sizeof(*conf));

	status = nvs_open(nvsNamespace_prov, NVS_READWRITE, &nvs);
	if (ESP_OK != status) {
		gc_err("Failed to open NVS namespace %s", nvsNamespace_prov);
		return status;
	}

	len = sizeof(conf->ssid);
	if (nvs_get_blob(nvs, nvsKey_provSsid, conf->ssid, &len) == ESP_OK) {
		conf->ssidLen   = len;
		conf->ssidIsSet = true;
	} else {
		//gc_dbg("Not found: %s", nvsKey_provSsid);
		errCt += 1;
	}

	len = sizeof(conf->password);
	if (nvs_get_blob(nvs, nvsKey_provPass, conf->password, &len) == ESP_OK) {
		conf->passwordLen   = len;
		conf->passwordIsSet = true;
	}
	else {//password not required
		//gc_dbg("Not found: %s", nvsKey_provPass);
		conf->passwordLen=0;
	}

	nvs_close(nvs);
	return (0 == errCt) ? ESP_OK : ESP_FAIL;
}


esp_err_t csProvConfStore(wifiStaConf_t * conf)
{
	nvs_handle	nvs;
	esp_err_t	status = ESP_OK;

	if (nvs_open(nvsNamespace_prov, NVS_READWRITE, &nvs) != ESP_OK) {
		gc_err("nvs_open failed");
		return ESP_FAIL;
	}

	if (conf->ssidIsSet) {
		status = nvs_set_blob(nvs, nvsKey_provSsid, conf->ssid, conf->ssidLen);
		if (ESP_OK != status) {
			gc_err("Failed to store %s", nvsKey_provSsid);
			goto exitNvs;
		}
	}

	if (conf->passwordIsSet) {
		status = nvs_set_blob(nvs, nvsKey_provPass, conf->password, conf->passwordLen);
		if (ESP_OK != status) {
			gc_err("Failed to store %s", nvsKey_provPass);
			goto exitNvs;
		}
	}

	nvs_commit(nvs);

exitNvs:
	nvs_close(nvs);
	return status;
}


esp_err_t csProvConfErase(void)
{
	nvs_handle	nvs;
	esp_err_t	status;

	if (nvs_open(nvsNamespace_prov, NVS_READWRITE, &nvs) != ESP_OK) {
		gc_err("nvs_open failed");
		return ESP_FAIL;
	}

	// Erase all provisioning information
	nvs_erase_key(nvs, nvsKey_provSsid);
	nvs_erase_key(nvs, nvsKey_provPass);
	status = nvs_set_i8(nvs, nvsKey_provState, (int8_t)csProvState_unprovisioned);

	nvs_commit(nvs);

	nvs_close(nvs);
	return status;
}
