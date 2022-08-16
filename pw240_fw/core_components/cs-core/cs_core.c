/**
 * \file cs_core.c
 *
 * \brief Start the application
 *
 */

#include <string.h>

#include <esp_err.h>
#include <esp_event.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_log.h>
#include <esp_flash_encrypt.h>
#include <nvs_flash.h>
#include <esp32/rom/ets_sys.h>

#include <esp_ota_ops.h>

#include "lwip/err.h"
#include "lwip/sys.h"

#include "mbedtls/platform.h"
#include "cJSON.h"

#include "cs_common.h"
#include "cs_heap.h"
#include "cs_binhex.h"
#include "mfg_data.h"
#include "param_mgr.h"
#include "time_mgr.h"
#include "event_callback.h"
#include "cs_framework.h"
#include "cs_control.h"
#include "cs_prov_support.h"
#include "cs_self_test.h"

// Comment out the MOD_NAME line to disable debug prints from this file
#define MOD_NAME	"cs_core"
#include "mod_debug.h"

csCoreConf_t	csCoreConf;

/**
 * \brief Low-level core initialization
 */
esp_err_t csCoreInit0(csCoreInit0_t * info)
{
	csCoreConf.info = *info;

#if CONFIG_NVS_ENCRYPTION
	//gc_dbg("Initialize secure NVS");

	// Check for presence of NVS encryption keys partition
	const esp_partition_t *	nvsKeysPart;
	nvsKeysPart = esp_partition_find_first(
		ESP_PARTITION_TYPE_DATA,
		ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS,
		NULL
	);

	// Load the NVS keys
	ESP_ERROR_CHECK(nvs_flash_read_security_cfg(nvsKeysPart, &csCoreConf.nvsKeys));

	//gc_dbg("Initialize secure NVS");
	ESP_ERROR_CHECK(nvs_flash_secure_init(&csCoreConf.nvsKeys));

#else

	//gc_dbg("Initialize NVS");
	ESP_ERROR_CHECK(nvs_flash_init());
#endif

	//gc_dbg("Load core mfg_data");
	ESP_ERROR_CHECK(mfgDataInit());

	// If ESP32 is configured for 4 universal MAC addresses, use the MAC address
	// stored in the ESP32 efuse
	// Otherwise use ConnectSense MAC stored in manufacturing data
	uint8_t	mac[6];
	if (UNIVERSAL_MAC_ADDR_NUM == FOUR_UNIVERSAL_MAC_ADDR) {
		ESP_ERROR_CHECK(esp_efuse_mac_get_default(mac));
	} else {
		memcpy(mac, coreMfgData.macAddrBase, 6);
	}
	esp_base_mac_addr_set(mac);

	// Direct cJSON to use SPIRAM heap
	cJSON_Hooks	hooks = {
		.malloc_fn = cs_heap_malloc,
		.free_fn   = cs_heap_free
	};
	cJSON_InitHooks(&hooks);

	// Direct mbedTLS to use heap from SPIRAM via our memory heap interface
	mbedtls_platform_set_calloc_free(cs_heap_calloc, cs_heap_free);

	// Initialize and start the parameter manager and load parameters
	//gc_dbg("paramMgrInit");
	ESP_ERROR_CHECK(paramMgrInit());
	//gc_dbg("paramMgrStart");
	ESP_ERROR_CHECK(paramMgrStart());

	if(!coreMfgData.isValid)//if we have no manufacturer data in flash, enable self test
	{
		csSelfTestEnable();
	}

	//gc_dbg("Core init0 done");
	return ESP_OK;
}


/**
 * \brief Initialize common ConnectSense drivers, modules, etc
 */
esp_err_t csCoreInit1(csCoreInit1_t * params)
{
	// Copy this model's information
	csCoreConf.params = *params;

	int	len;

	// Build string for base MAC address
	uint8_t	mac[6];
	esp_base_mac_addr_get(mac);
	len = snprintf(
		csCoreConf.baseMacStr, sizeof(csCoreConf.baseMacStr),
		"%02X:%02X:%02X:%02X:%02X:%02X",
		mac[0], mac[1], mac[2], mac[3],  mac[4], mac[5]
	);
	ESP_ERROR_CHECK(len >= sizeof(csCoreConf.baseMacStr));

	// Time manager
	gc_dbg("Initialize timeMgr");
	ESP_ERROR_CHECK(timeMgrInit());

	gc_dbg("Initialize csControl");
	ESP_ERROR_CHECK(csControlInit(&params->controlConf));

	gc_dbg("Initialize framework");
	ESP_ERROR_CHECK(csFrameworkInit(&csCoreConf));

	gc_dbg("exit csCoreInit");
	return ESP_OK;
}


/**
 * \brief Start common ConnectSense drivers, modules, etc
 */
esp_err_t csCoreStart(csCoreStartParams_t * params)
{
	esp_err_t		status;

	////////////////////////////////////////////////////////////////
	// Start core modules
	////////////////////////////////////////////////////////////////

	ESP_ERROR_CHECK(timeMgrStart());

	ESP_ERROR_CHECK(csControlStart());

	if ((status = csFrameworkStart(params)) != ESP_OK)
		return status;

	return ESP_OK;
}
