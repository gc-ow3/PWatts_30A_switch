/**
 *
 * Summary:
 * Read data from the manufacturing partition.
 *
 * Description:
 *
 */

#include "cs_common.h"
#include "cs_heap.h"
#include "cs_binhex.h"
#include "mfg_data.h"
#include "esp_event.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp32/rom/ets_sys.h"
#include "esp_log.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_flash_encrypt.h"

#include "cs_str_utils.h"

// Comment out the MOD_NAME line to disable debug prints from this file
#define MOD_NAME	"cs_mfg_data"
#include "mod_debug.h"

// Acquire and release mutex
SemaphoreHandle_t	mfg_mutex;
#define MFG_MUTEX_GET(mutex)		xSemaphoreTake(mutex, portMAX_DELAY)
#define MFG_MUTEX_PUT(mutex)		xSemaphoreGive(mutex)

/*
********************************************************************************
* Private functions
********************************************************************************
*/
static esp_err_t load(mfgDataTable_t * tab, int tabSz);
static esp_err_t save(mfgDataTable_t * tab, int tabSz);

/*
********************************************************************************
* Constant data
********************************************************************************
*/
static const char	mfgPartition[]     = {"mfg_data"};
static const char	mfgNameSpace[]     = {"pw_mfg"};
static const char	mfgLegacyNameSpace[] = {"mfg"};

//this key disables the ability to read the mfg_data and the firmware will act as if it doesn't exist yet
static const char	mfgKey_disableData[]   = {"disable"};
/**
 * This table describes the information expected to be found in the
 * manufacturing data partition and where it is to be loaded
 */
static mfgDataTable_t	mfgDataTable[] = {
	{
		.key    = "serial_num",
		.type   = mfgDataType_str,
		.value  = coreMfgData.serialNum,
		.maxLen = sizeof(coreMfgData.serialNum),
		.alloc  = false,
		.defVal = "1000000000"
	},
	{
		.key    = "hw_version",
		.type   = mfgDataType_str,
		.value  = coreMfgData.hwVersion,
		.maxLen = sizeof(coreMfgData.hwVersion),
		.alloc  = false,
		.defVal = "1.0"
	},
	{
		.key    = "mac_addr_base",
		.type   = mfgDataType_bin,
		.value  = coreMfgData.macAddrBase,
		.maxLen = sizeof(coreMfgData.macAddrBase),
		.alloc  = false
	},
	{
		.key    = "tls_key_b64",
		.type   = mfgDataType_str,
		.value  = &coreMfgData.tlsKeyB64,
		.maxLen = 0,
		.alloc  = true,
		.defVal = "\0",
	},
	{
		.key    = "tls_cert",
		.type   = mfgDataType_str,
		.value  = &coreMfgData.tlsCertB64,
		.maxLen = 0,
		.alloc  = true,
		.defVal = "\0",
	},
	{
		.key    = "prod_metadata",
		.type   = mfgDataType_str,
		.value  = &coreMfgData.prod_metadata,
		.maxLen = 0,
		.alloc  = true,
		.defVal = "\0",
	},
};
static const int mfgDataTableSz = (sizeof(mfgDataTable) / sizeof(mfgDataTable_t));


// Global manufacturing data structure
coreMfgData_t 	coreMfgData = {
		.isValid=false,
		.serialNum = {0},
		.hwVersion = {0},
		.macAddrBase = {0},
		.tlsKeyB64 = NULL,
		.tlsCertB64 = NULL,
		.prod_metadata = NULL
};


/*
********************************************************************************
* Private data
********************************************************************************
*/
static bool		isInitialized;

bool mfgDataIsEnabled(void)
{

	MFG_MUTEX_GET(mfg_mutex);
	// Open the PW mfg namespace
	nvs_handle	nvs;
	if (nvs_open_from_partition(mfgPartition, mfgNameSpace, NVS_READONLY, &nvs) != ESP_OK) {
		MFG_MUTEX_PUT(mfg_mutex);
		return false;
	}

	// Check for the mfg disable flag
	// Value doesn't matter, testing for the presence
	esp_err_t	status;
	int8_t		value;
	status = nvs_get_i8(nvs, mfgKey_disableData, &value);
	nvs_close(nvs);
	MFG_MUTEX_PUT(mfg_mutex);
	return (ESP_OK == status) ? false : true;
}

esp_err_t mfgDataDisable(void)
{
	// Open the PW mfg namespace
	MFG_MUTEX_GET(mfg_mutex);
	nvs_handle	nvs;
	if (nvs_open_from_partition(mfgPartition, mfgNameSpace, NVS_READONLY, &nvs) != ESP_OK) {
		MFG_MUTEX_PUT(mfg_mutex);
		return ESP_OK;
	}
	nvs_close(nvs);
	if (nvs_open_from_partition(mfgPartition, mfgNameSpace, NVS_READWRITE, &nvs) != ESP_OK) {
		gc_err("nvs_open failed");
		MFG_MUTEX_PUT(mfg_mutex);
		return ESP_FAIL;
	}

	// Check for the mfg disable flag
	// Value doesn't matter, testing for the presence
	int8_t	value;
	if (nvs_get_i8(nvs, mfgKey_disableData, &value) != ESP_OK) {
		// Set the flag
		value = 1;
		(void)nvs_set_i8(nvs, mfgKey_disableData, value);
		nvs_commit(nvs);
	}

	nvs_close(nvs);
	MFG_MUTEX_PUT(mfg_mutex);
	return ESP_OK;
}

esp_err_t mfgDataEnable(void)
{
	esp_err_t status;
	// Open the PW mfg namespace
	MFG_MUTEX_GET(mfg_mutex);
	nvs_handle	nvs;
	if (nvs_open_from_partition(mfgPartition, mfgNameSpace, NVS_READONLY, &nvs) != ESP_OK) {
		MFG_MUTEX_PUT(mfg_mutex);
		return ESP_OK;
	}
	nvs_close(nvs);
	if (nvs_open_from_partition(mfgPartition, mfgNameSpace, NVS_READWRITE, &nvs) != ESP_OK) {
		gc_err("nvs_open failed");
		MFG_MUTEX_PUT(mfg_mutex);
		return ESP_FAIL;
	}

	// Delete the factory test disable flag
	// Value doesn't matter, testing for the presence
	status = nvs_erase_key(nvs, mfgKey_disableData);
	if(status == ESP_ERR_NVS_NOT_FOUND)
		status = ESP_OK;

	nvs_close(nvs);
	MFG_MUTEX_PUT(mfg_mutex);
	return status;
}

void mfgSetDefaultMac(){
	//only set the default mac the first time. After that allow config changes
	static bool first_load = true;
	if (!first_load){
		return;
	}
	else{
		first_load = false;
	}
	esp_err_t	status;
	nvs_handle	nvs;


	MFG_MUTEX_GET(mfg_mutex);
	status = nvs_open_from_partition(mfgPartition, mfgLegacyNameSpace, NVS_READONLY, &nvs);
	if (status == ESP_OK){
		size_t iLen = sizeof(coreMfgData.macAddrBase);
		status = nvs_get_blob(nvs, "mac_addr_base", coreMfgData.macAddrBase, &iLen);
		if (ESP_OK != status) {
			esp_read_mac(coreMfgData.macAddrBase, ESP_MAC_WIFI_STA);
		}
		nvs_close(nvs);
	}
	else{
		gc_err("legacy namespace cant open- defaulting to espressif mac");
		esp_read_mac(coreMfgData.macAddrBase, ESP_MAC_WIFI_STA);
	}
	MFG_MUTEX_PUT(mfg_mutex);
}

/**
 * \brief Initialize manufacturing data support
 */
esp_err_t mfgDataInit()
{
	esp_err_t	status;

	if (isInitialized)
		return ESP_OK;

	coreMfgData.isValid = false;

#if CONFIG_NVS_ENCRYPTION
	status = nvs_flash_secure_init_partition(mfgPartition, &csCoreConf.nvsKeys);
#else
	status = nvs_flash_init_partition(mfgPartition);
#endif

	if (ESP_OK != status) {
		gc_err("Failed to initialize partition \"%s\"", mfgPartition);
		return status;
	}
	else{
		if ((mfg_mutex = xSemaphoreCreateMutex()) == NULL) {
			gc_err("Mutex create failed");
			return ESP_FAIL;
		}
	}

	mfgDataLoadCore();

	isInitialized = true;
	return ESP_OK;
}


esp_err_t mfgDataDeinit(void)
{
	// Really should free any allocated mfg item memory here,
	// but that may cause more problems than it's worth

	(void)nvs_flash_deinit_partition(mfgPartition);
	isInitialized = false;
	return ESP_OK;
}


esp_err_t mfgDataSave(){
	esp_err_t status;
	nvs_handle	nvs;
	status = nvs_open_from_partition(mfgPartition, mfgLegacyNameSpace, NVS_READWRITE, &nvs);
	if (status == ESP_OK){
		nvs_erase_all(nvs);
		nvs_commit(nvs);
		nvs_close(nvs);
	}
	status = save(mfgDataTable, mfgDataTableSz);
	mfgDataLoadCore();
	return status;
}

esp_err_t mfgDataLoadCore(){
	esp_err_t	status;
	memset(&coreMfgData,0,sizeof(coreMfgData_t));
	coreMfgData.isValid = false;

	if ((status = load(mfgDataTable, mfgDataTableSz)) == ESP_OK && mfgDataIsEnabled()) {
		coreMfgData.isValid = true;
	} else {
		gc_err("PW MFG Core Data invalid, or forced in mfg mode");
		// Default values were set for everything else,
		// for MAC address read it from the IWO legacy NVS namespace or the ESP eFuse
		mfgSetDefaultMac();
		status = ESP_FAIL;
	}
	return status;
}

/*!
 * \brief Load manufacturing data per the passed table reference
 *
 * \return status
 */
esp_err_t mfgDataLoad(mfgDataTable_t * tab, int tabSz)
{
	esp_err_t		status;

	if (!isInitialized)
		return ESP_FAIL;

	if (NULL == tab || tabSz < 0)
		return ESP_ERR_INVALID_ARG;

	if (0 == tabSz)
		return ESP_OK;

	status = load(tab, tabSz);
	if (ESP_OK != status) {
		gc_err("load error %d", status);
		return status;
	}

	return ESP_OK;
}


static esp_err_t partWrite(
	esp_partition_subtype_t	subType,
	const char *			label,
	const uint8_t *			data,
	int						dataLen
)
{
	esp_err_t	status;

	const esp_partition_t * part;

	part = esp_partition_find_first(
		ESP_PARTITION_TYPE_DATA,
		subType,
		label
	);
	if (NULL == part) {
		gc_err("Partition \"%s\" not defined", label);
		return ESP_FAIL;
	}

	if (part->size < dataLen) {
		gc_err("Write size (%d) exceeds partition size (%u)", dataLen, part->size);
		return ESP_ERR_INVALID_ARG;
	}

	status = esp_partition_erase_range(part, 0, part->size);
	if (ESP_OK != status) {
		gc_err("Partition erase failed");
		return status;
	}

	status = esp_partition_write(part, 0, data, dataLen);
	if (ESP_OK != status) {
		gc_err("Partition write failed");
		return status;
	}

	return ESP_OK;
}


/**
 * \brief For production support, write to the mfg_data partition
 */
esp_err_t mfgDataStore(const uint8_t * data, int dataLen)
{
	return partWrite(ESP_PARTITION_SUBTYPE_DATA_NVS, mfgPartition, data, dataLen);
}


static const char	nvsKeysPartition[] = {"nvs_keys"};

/**
 * \brief For production support, write to the nvs_keys partition
 */
esp_err_t mfgNvsKeysStore(const uint8_t * data, int dataLen)
{
	return partWrite(ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS, nvsKeysPartition, data, dataLen);
}


static void _mfgDataPrint(mfgDataTable_t * tab, int tabSz)
{
	esp_err_t	status;
	nvs_handle	nvs;

	MFG_MUTEX_GET(mfg_mutex);
	// Open NVS manufacturing namespace
	status = nvs_open_from_partition(mfgPartition, mfgNameSpace, NVS_READONLY, &nvs);
	if (ESP_OK != status) {
		gc_err("Failed to open NVS namespace %s", mfgNameSpace);
		MFG_MUTEX_PUT(mfg_mutex);
		return;
	}

	int		i;
	size_t	ioLen;

	for (i = 0; i < tabSz; i++, tab++) {
		switch (tab->type)
		{
		case mfgDataType_u8:
			gc_dbg("  %s: %u", tab->key, *(uint8_t *)tab->value);
			break;

		case mfgDataType_bin:
			status = nvs_get_blob(nvs, tab->key, NULL, &ioLen);
			if (ESP_OK == status) {
				gc_dbg("  %s", tab->key);
				if (tab->alloc) {
					gc_hexDump(*(uint8_t **)tab->value, ioLen);

				} else {
					gc_hexDump((uint8_t *)tab->value, ioLen);
				}
			}
			break;

		case mfgDataType_str:
			gc_dbg("  %s", tab->key);
			if (tab->alloc) {
				gc_textDump(NULL, *(const char **)tab->value, -1);
			} else {
				gc_textDump(NULL, (const char *)tab->value, -1);
			}
			break;

		default:
			gc_err("  %s: Unsupported type (%d)", tab->key, tab->type);
			status = ESP_FAIL;
			break;
		}
	}

	nvs_close(nvs);
	MFG_MUTEX_PUT(mfg_mutex);
}


void mfgDataCorePrint(void)
{
	_mfgDataPrint(mfgDataTable, mfgDataTableSz);
}


void mfgDataAppPrint(mfgDataTable_t * tab, int tabSz)
{
	_mfgDataPrint(tab, tabSz);
}


/*!
 * \brief Load all values from the manufacturing partition
 *
 * \return status
 *
 * Step through the loadList table and load each item from the manufacturing
 * partition in flash memory
 *
 */
static esp_err_t load(mfgDataTable_t * tab, int tabSz)
{
	size_t		ioLen;
	int			defCt = 0;
	int			errCt = 0;
	int			i;
	esp_err_t	status;
	nvs_handle	nvs;

	MFG_MUTEX_GET(mfg_mutex);
	// Open NVS manufacturing namespace
	status = nvs_open_from_partition(mfgPartition, mfgNameSpace, NVS_READWRITE, &nvs);
	if (ESP_OK != status) {
		gc_err("Failed to open NVS namespace %s", mfgNameSpace);
		MFG_MUTEX_PUT(mfg_mutex);
		return status;
	}

	for (i = 0; i < tabSz; i++, tab++) {
		void *		pOut;

		if (tab->alloc) {
			switch (tab->type)
			{
			case mfgDataType_bin:
				status = nvs_get_blob(nvs, tab->key, NULL, &ioLen);
				if (ESP_OK != status && NULL != tab->defVal) {
					// Default value is assumed to a hex string that will
					// be converted to binary
					ioLen = strlen(tab->defVal) / 2;
					status = ESP_OK;
					//gc_dbg("Default bin length = %d", ioLen);
				}
				break;

			case mfgDataType_str:
				status = nvs_get_str(nvs, tab->key, NULL, &ioLen);
				if (ESP_OK != status && NULL != tab->defVal) {
					ioLen = strlen(tab->defVal) + 1;
					status = ESP_OK;
					//gc_dbg("Default str length = %d", ioLen);
				}
				break;

			default:
				gc_err("Unsupported type (%d) for %s", tab->type, tab->key);
				nvs_close(nvs);
				MFG_MUTEX_PUT(mfg_mutex);
				return ESP_FAIL;
			}

			if (ESP_OK != status) {
				gc_err("Failed to get size for %s", tab->key);
				nvs_close(nvs);
				MFG_MUTEX_PUT(mfg_mutex);
				return status;
			}

			if ((pOut = cs_heap_malloc(ioLen)) == NULL) {
				gc_err("Failed to allocate %d bytes for %s", ioLen, tab->key);
				nvs_close(nvs);
				MFG_MUTEX_PUT(mfg_mutex);
				return ESP_ERR_NO_MEM;
			}

			//gc_dbg("Assigned memory: %08x", pOut);
			if(*(uint8_t **)tab->value)
				cs_heap_free(*(uint8_t **)tab->value);

			*(uint8_t **)tab->value = pOut;
		} else {
			pOut  = tab->value;
			ioLen = tab->maxLen;
		}

		switch (tab->type)
		{
		case mfgDataType_u8:
			status = nvs_get_u8(nvs, tab->key, (uint8_t *)pOut);
			if (ESP_OK == status) {
				//gc_dbg("Load %s: %u", tab->key, *(uint8_t *)tab->value);
			} else if (NULL != tab->defVal) {
				defCt += 1;
				char *	x;
				*(uint8_t *)pOut = (uint8_t)strtol(tab->defVal, &x, 10);
				gc_dbg("Set default for %s: %u", tab->key, *(uint8_t *)pOut);
				status = ESP_OK;
			}
			break;

		case mfgDataType_bin:
			status = nvs_get_blob(nvs, tab->key, pOut, &ioLen);
			if (ESP_OK == status) {
				//gc_dbg("Load %s", tab->key);
				//gc_hexDump(pOut, ioLen);
			} else if (NULL != tab->defVal) {
				defCt += 1;
				if (csHexToBin8(tab->defVal, pOut, ioLen) == ioLen) {
					gc_dbg("Set default for %s", tab->key);
					gc_hexDump(pOut, ioLen);
					status = ESP_OK;
				}
			}
			break;

		case mfgDataType_str:
			status = nvs_get_str(nvs, tab->key, (char *)pOut, &ioLen);
			if (ESP_OK == status) {
				//gc_dbg("Load %s", tab->key);
				//gc_textDump(NULL, (const char *)pOut, ioLen);
			} else if (NULL != tab->defVal) {
				defCt += 1;
				strlcpy(pOut, tab->defVal, ioLen);
				status = ESP_OK;
				gc_dbg("Set default for %s", tab->key);
				gc_textDump(NULL, pOut, ioLen);
			}
			break;

		default:
			gc_err("Unsupported type (%d) for %s", tab->type, tab->key);
			status = ESP_FAIL;
			break;
		}

		if (ESP_OK != status) {
			gc_err("Failed to load %s", tab->key);
			errCt += 1;
		}
	}

	nvs_close(nvs);
	MFG_MUTEX_PUT(mfg_mutex);
	return (0 == errCt) ? ESP_OK : ESP_FAIL;
}

/*!
 * \brief Save all values to the manufacturing partition
 *
 * \return status
 *
 * Step through the loadList table and save each item to the manufacturing
 * partition in flash memory
 *
 */
static esp_err_t save(mfgDataTable_t * tab, int tabSz)
{
	int			errCt = 0;
	int			i;
	esp_err_t	status;
	nvs_handle	nvs;
	size_t inLen=0;

	// Open NVS manufacturing namespace
	MFG_MUTEX_GET(mfg_mutex);
	status = nvs_open_from_partition(mfgPartition, mfgNameSpace, NVS_READWRITE, &nvs);
	if (ESP_OK != status) {
		gc_err("Failed to open NVS namespace %s", mfgNameSpace);
		MFG_MUTEX_PUT(mfg_mutex);
		return status;
	}

	for (i = 0; i < tabSz; i++, tab++) {

		switch (tab->type)
		{
		case mfgDataType_u8:
			status = nvs_set_u8(nvs, tab->key, *(uint8_t *)tab->value);
			if (ESP_OK == status) {
				nvs_commit(nvs);
				gc_dbg("Saved %s to NVS", tab->key);
			} else {
				gc_dbg("Error (%d) saving %s to NVS", status, tab->key);
			}
			break;

		case mfgDataType_bin:
			inLen = tab->maxLen;
			if(ESP_OK == nvs_erase_key(nvs, tab->key)){
				nvs_commit(nvs);
			}
			status = nvs_set_blob(nvs, tab->key, tab->value, inLen);
			if (ESP_OK == status) {
				nvs_commit(nvs);
				gc_dbg("Saved %s to NVS", tab->key);
			} else {
				gc_dbg("Error (%d) saving %s to NVS", status, tab->key);
			}
			break;

		case mfgDataType_str:
			if(ESP_OK == nvs_erase_key(nvs, tab->key)){
				nvs_commit(nvs);
			}
			if(tab->alloc){
				status = nvs_set_str(nvs, tab->key, *(char **)tab->value);
			}
			else{
				status = nvs_set_str(nvs, tab->key, (char *)tab->value);
			}
			if (ESP_OK == status) {
				nvs_commit(nvs);
				gc_dbg("Saved %s to NVS", tab->key);
			} else {
				gc_dbg("Error (%d) saving %s to NVS", status, tab->key);
			}
			break;

		default:
			gc_err("Unsupported type (%d) for %s", tab->type, tab->key);
			status = ESP_FAIL;
			break;
		}

		if (ESP_OK != status) {
			gc_err("Failed to load %s", tab->key);
			errCt += 1;
		}
	}

	nvs_close(nvs);
	MFG_MUTEX_PUT(mfg_mutex);
	return (0 == errCt) ? ESP_OK : ESP_FAIL;

}
