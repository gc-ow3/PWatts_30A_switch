/*
 * cs_ota_rollback.c
 *
 *  Created on: Feb 17, 2020
 *      Author: wesd
 */

#include "sdkconfig.h"
#include <esp_err.h>
#include <esp_ota_ops.h>
#include <esp_flash_partitions.h>
#include <esp_partition.h>
#include "cs_common.h"
#include "cs_ota_rollback.h"

// Comment out the MOD_NAME line to disable debug prints from this file
#define MOD_NAME	"cs_ota_rollback"
#include "mod_debug.h"

#ifdef CONFIG_APP_ROLLBACK_ENABLE

bool csOtaUpdateIsPresent(csOtaType_t * otaType)
{
	bool	ret;

	// Check if OTA update was performed
	esp_ota_img_states_t	otaState;
	esp_ota_get_state_partition(esp_ota_get_running_partition(), &otaState);

	ret = (ESP_OTA_IMG_PENDING_VERIFY == otaState) ? true : false;

	// optionally pass back the OTA type
	if (otaType) {
		*otaType = ret ? csOtaTypeGet() : csOtaType_null;
	}

	return ret;
}


void csOtaUpdateAccept(void)
{
	if (csOtaUpdateIsPresent(NULL)) {
		gc_dbg("OTA update detected  marking the new version as valid");

		csOtaTypeSet(csOtaType_null);
		esp_ota_mark_app_valid_cancel_rollback();

	}
}


void csOtaUpdateReject(void)
{
	if (csOtaUpdateIsPresent(NULL)) {
		gc_dbg("Marking OTA update as invalid");

		csOtaTypeSet(csOtaType_null);
		esp_ota_mark_app_invalid_rollback_and_reboot();
	}
}


static const char	nvsNamespace_csOta[] = {"cs-ota"};
static const char	nvsKey_csOtaType[]   = {"ota-type"};


void csOtaTypeSet(csOtaType_t otaType)
{
	esp_err_t	status;
	nvs_handle	nvs;

	if ((status = nvs_open(nvsNamespace_csOta, NVS_READWRITE, &nvs)) == ESP_OK) {
		nvs_set_u8(nvs, nvsKey_csOtaType, (uint8_t)otaType);
		nvs_close(nvs);
	}
}


csOtaType_t csOtaTypeGet(void)
{
	csOtaType_t	ret = csOtaType_null;
	esp_err_t	status;
	nvs_handle	nvs;

	if ((status = nvs_open(nvsNamespace_csOta, NVS_READWRITE, &nvs)) == ESP_OK) {
		uint8_t	temp;

		if (nvs_get_u8(nvs, nvsKey_csOtaType, &temp) == ESP_OK) {
			ret = (csOtaType_t)temp;
		}

		nvs_close(nvs);
	}

	return ret;
}


#else

bool csOtaUpdateIsPresent(csOtaType_t * otaType)
{
	return false;
}


void csOtaUpdateAccept(void)
{
	return;
}


void csOtaUpdateReject(void)
{
	return;
}


void csOtaTypeSet(csOtaType_t otaType)
{
	(void)otaType;
}


csOtaType_t csOtaTypeGet(void)
{
	return csOtaType_null;
}


#endif	// CONFIG_APP_ROLLBACK_ENABLE

