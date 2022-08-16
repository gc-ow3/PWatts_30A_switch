/*! \file param_mgr.c
 *
 * Get/Set parameters
 *
 */
#include "cs_common.h"
#include "cs_heap.h"
#include "include/param_mgr.h"
#include "esp_system.h"
#include "assert.h"
#include "nvs.h"
#include "nvs_flash.h"

// Comment out the MOD_NAME line to disable debug prints from this file
#define MOD_NAME	"cs_param_mgr"
#include "mod_debug.h"


//******************************************************************************
// Defines
//******************************************************************************

// Apply a holdoff writing to flash when parameters change
#define FLASH_UPDATE_TIMER_MS		(500)


//******************************************************************************
// type definitions
//******************************************************************************

// For constructing a linked list of parameter table entries
typedef struct tableListEntry_s {
	struct tableListEntry_s *	next;
	const char *				tabName;
	csParamTab_t *				table;
	int							tableSz;
	// Variable number of flags allocated, per table size
	bool						ischanged[];
} tableListEntry_t;


/**
* \brief The task control structure
*/
typedef struct {
	bool				isRunning;
	SemaphoreHandle_t	mutex;
	TaskHandle_t		taskHandle;
	TimerHandle_t		timer;
	nvs_handle			nvStandard;
	nvs_handle			nvSticky;
	tableListEntry_t *	listHead;
	tableListEntry_t *	listTail;
} taskCtrl_t;


//******************************************************************************
// Local functions
//******************************************************************************
static esp_err_t findEntry(
	tableListEntry_t *	pList,
	const char *		key,
	csParamTab_t **		tab,
	bool **				isChanged
);
static esp_err_t resetConfig(taskCtrl_t * pCtrl);
static esp_err_t loadParamTable(taskCtrl_t * pCtrl, csParamTab_t * pTab, int tabSz);
static esp_err_t initParam(taskCtrl_t * pCtrl, csParamTab_t * pTab);

static nvs_handle csNvHandle(taskCtrl_t * pCtrl, csParamTab_t * pItem);

// Parameter validation functions
static esp_err_t checkU8Range(csParamTab_t * pTab, void * pValue);
static esp_err_t checkI32Range(csParamTab_t * pTab, void * pValue);
static esp_err_t checkU32Range(csParamTab_t * pTab, void * pValue);
static esp_err_t checkStrLen(csParamTab_t * pTab, void * pValue);
//static esp_err_t checkStrNum(gcParamTab_t * pTab, void * pValue);

static void nvTask(void * taskParam);
static void timerCallback(TimerHandle_t tmr);


//******************************************************************************
// Constant data
//******************************************************************************

// Namespace in nvs for ConnectSense parameters
static const char	nvSpaceStandard[] = {"cs_param"};
static const char	nvSpaceSticky[]   = {"cs_sticky"};


//******************************************************************************
// Local data
//******************************************************************************

static taskCtrl_t *	taskCtrl;


/*!
 * \brief Initialize configuration manager
 *
 * \return pdPASS On success
 * \return (other) On error
 *
 */
esp_err_t paramMgrInit(void)
{
	taskCtrl_t *	pCtrl = taskCtrl;
	if (NULL != pCtrl)
		return ESP_OK;

	esp_err_t		status;

	if ((pCtrl = cs_heap_calloc(1, sizeof(*pCtrl))) == NULL)
		return ESP_ERR_NO_MEM;

	// Open the name spaces for parameters

	// Standard space
	status = nvs_open(nvSpaceStandard, NVS_READWRITE, &pCtrl->nvStandard);
	if (ESP_OK != status) {
		gc_err("nvs_open \"%s\" failed", nvSpaceStandard);
		return status;
	}

	// Sticky space - persists over factory resets
	status = nvs_open(nvSpaceSticky, NVS_READWRITE, &pCtrl->nvSticky);
	if (ESP_OK != status) {
		gc_err("nvs_open \"%s\" failed", nvSpaceSticky);
		return status;
	}

	// Create the mutex
	if ((pCtrl->mutex = xSemaphoreCreateMutex()) == NULL) {
		gc_err("Mutex create failed");
		return ESP_FAIL;
	}

	// Create the one-shot update timer
	pCtrl->timer = xTimerCreate(
		"paramTimer",
		pdMS_TO_TICKS(FLASH_UPDATE_TIMER_MS),
		pdFALSE,
		(void *)pCtrl,
		timerCallback
	);
	if (NULL == pCtrl->timer) {
		gc_err("Timer create failed");
		return ESP_FAIL;
	}

	// Create the update task
	status = xTaskCreate(
		nvTask,
		"param_mgr",
		1400,
		(void *)pCtrl,
		CS_TASK_PRIO_PARAM_MGR,
		&pCtrl->taskHandle
	);
	if (pdPASS != status) {
		gc_err("Task create failed");
		return ESP_FAIL;
	}

	taskCtrl = pCtrl;
	return ESP_OK;
}


/**
 * \brief Add one or more parameters to the parameter manager
 *
 * Must be called after \ref paramMgrInit and before \ref paramMgrStart
 *
 * \param [in] tabName Name the table e.g. "core", "app"
 * \param [in] pTab pointer to table of parameters to be added
 * \param [in] tabSz Number of entries in the table
 *
 * \return ESP_OK on success
 * \return ESP_FAIL There is a conflicting parameter name already registered
 * \return ESP_ERR_INVALID_ARG NULL table pointer or invalid size
 * \return ESP_ERR_NO_MEM Unable to allocate memory to store table info
 */
esp_err_t paramMgrParamsAdd(const char * tabName, csParamTab_t * pTab, int tabSz)
{
	taskCtrl_t *	pCtrl = taskCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	esp_err_t		status;
	csParamTab_t *	check;
	int				i;

	if (!pCtrl->isRunning)
		return ESP_FAIL;

	if (NULL == pTab || tabSz < 0 || NULL == tabName)
		return ESP_ERR_INVALID_ARG;

	//gc_dbg("lock mutex");
	if (xSemaphoreTake(pCtrl->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
		return ESP_ERR_TIMEOUT;
	}

	// Make sure no conflicting parameter names in the existing list
	//gc_dbg("check for conflicts");
	for (i = 0, check = pTab; i < tabSz; i++, check++) {
		// Check existing entries for match of check->nvKey
		status = findEntry(pCtrl->listHead, check->nvKey, NULL, NULL);
		if (ESP_OK == status) {
			gc_err("Conflicting parameter \"%s\" found", check->nvKey);
			status = ESP_FAIL;
			goto exitMutex;
		} else if (ESP_ERR_NOT_FOUND != status) {
			gc_err("findEntry failed");
			status = ESP_FAIL;
			goto exitMutex;
		}
	}

	// Allocate a table entry structure
	int					memSz;
	tableListEntry_t *	entry;

	// Allocate space for the entry
	memSz = sizeof(tableListEntry_t) + (sizeof(entry->ischanged[0]) * tabSz);
	//gc_dbg("Allocate %d bytes", memSz);
	if ((entry = cs_heap_calloc(1, memSz)) == NULL) {
		status = ESP_ERR_NO_MEM;
		goto exitMutex;
	}
	//gc_dbg("Allocated memory at %08x", entry);

	// Attach the parameter table to the LL entry
	entry->tabName = tabName;
	entry->table   = pTab;
	entry->tableSz = tabSz;

	// Add the entry to the tail of linked list
	if (NULL == pCtrl->listHead) {
		// First entry
		pCtrl->listHead = entry;
	} else {
		// New last entry
		pCtrl->listTail->next = entry;
	}
	pCtrl->listTail = entry;

	// Load the parameters to memory
	status = loadParamTable(pCtrl, pTab, tabSz);

exitMutex:
	//gc_dbg("release mutex");
	xSemaphoreGive(pCtrl->mutex);
	return status;
}


/**
 * \brief Return pointer to table entry of named parameter
 */
const csParamTab_t * paramMgrLookupParam(const char * name)
{
	taskCtrl_t *	pCtrl = taskCtrl;
	if (NULL == pCtrl)
		return NULL;

	csParamTab_t *	entry;

	if (findEntry(pCtrl->listHead, name, &entry, NULL) == ESP_OK)
		return entry;

	return NULL;
}


/**
* \brief Start the parameter manager
*/
esp_err_t paramMgrStart(void)
{
	taskCtrl_t *	pCtrl = taskCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	if (pCtrl->isRunning) {
		return ESP_OK;
	}

	pCtrl->isRunning = true;
	return ESP_OK;
}


/**
 * \brief Print parameter settings to debug output
 */
void paramMgrSettingsDump(void)
{
#if CONFIG_IOT8020_DEBUG
	taskCtrl_t *	pCtrl = taskCtrl;
	if (NULL == pCtrl)
		return;

	if (!pCtrl->isRunning) {
		gc_err("Parameter manager not running");
		return;
	}

	tableListEntry_t *	pList;

	gc_dbg("Settings:");
	for (pList = pCtrl->listHead; NULL != pList; pList = pList->next) {
		csParamTab_t *	pTab = pList->table;
		int				i;

		gc_dbg("  Parameter set: \"%s\"", pList->tabName);

		for (i = 0, pTab = pList->table; i < pList->tableSz; i++, pTab++) {
			switch (pTab->objTyp)
			{
			case objtyp_bool:
				gc_dbg("    %s = %s", pTab->nvKey, *(bool *)pTab->pVar ? "true" : "false");
				break;

			case objtyp_u8:
				gc_dbg("    %s = %u", pTab->nvKey, *(uint8_t *)pTab->pVar);
				break;

			case objtyp_i32:
				gc_dbg("    %s = %ld", pTab->nvKey, *(int32_t *)pTab->pVar);
				break;

			case objtyp_u32:
				gc_dbg("    %s = %lu", pTab->nvKey, *(uint32_t *)pTab->pVar);
				break;

			case objtyp_str:
				gc_dbg("    %s = %s", pTab->nvKey, (char *)pTab->pVar);
				break;

			default:
				gc_err("    %s - unsupported type (%d)", pTab->objTyp);
				break;
			}
		}
	}
#endif
}


/*!
 * \brief Reset the configuration to factory defaults
 *
 * \return WM_SUCCESS Success
 * \return (other) Error code
 *
 * This uses the application framework's PSM handle
 *
 */
esp_err_t paramMgrReset(void)
{
	taskCtrl_t *	pCtrl = taskCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	esp_err_t		status;

	if (pdPASS == xSemaphoreTake(pCtrl->mutex, pdMS_TO_TICKS(100))) {
		status = resetConfig(pCtrl);
		xSemaphoreGive(pCtrl->mutex);
	} else {
		status = ESP_FAIL;
	}

	return status;
}


#if 0
/**
 * \brief Reset a specific parameter to its default value
 *
 * \param [in] pKey pointer to the name of the parameter
 *
 * \return WM_SUCCESS Success
 * \return (other) Error code
 *
 */
esp_err_t paramMgrResetParam(const char * pKey)
{
	taskCtrl_t *	pCtrl = taskCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	esp_err_t		status;
	csParamTab_t *	pTab;

	status = findEntry(pCtrl->listHead, pKey, &pTab, NULL);
	if (ESP_OK != status) {
		return ESP_FAIL;
	}

	if (pdPASS == xSemaphoreTake(pCtrl->mutex, pdMS_TO_TICKS(100))) {
		status = initParam(pCtrl, pTab);
		xSemaphoreGive(pCtrl->mutex);
	} else {
		status = ESP_FAIL;
	}

	return status;
}
#endif


/*!
 * \brief Set the value of a single named boolean parameter
 *
 * \param [in] pKey pointer to the name of the parameter
 * \param [in] value : true (1) or false (0)
 *
 * \return WM_SUCCESS Success
 * \return (other) Error code
 *
 */
esp_err_t paramMgrSetBool(const char * pKey, bool value)
{
	taskCtrl_t *	pCtrl = taskCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	csParamTab_t *	pTab;
	esp_err_t		status;
	bool *			isChanged;
	int8_t			storedVal;

	if (NULL == pKey) {
		return ESP_ERR_INVALID_ARG;
	}

	status = findEntry(pCtrl->listHead, pKey, &pTab, &isChanged);
	if (ESP_OK != status) {
		return status;
	}

	// Make sure the parameter is registered as type bool
	if (objtyp_bool != pTab->objTyp) {
		gc_err("Attempted to set %s as boolean", pKey);
		return ESP_ERR_INVALID_ARG;
	}

	// Validate the value
	if (value != true && value != false) {
		return ESP_ERR_INVALID_ARG;
	}

	if (xSemaphoreTake(pCtrl->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
		gc_err("Could not acquire mutex");
		return ESP_FAIL;
	}

	// Apply the new value to the RAM copy
	*(bool *)pTab->pVar = (bool)value;

	// Compare the new value with that stored in flash
	status = nvs_get_i8(csNvHandle(pCtrl, pTab), pTab->nvKey, &storedVal);
	if (ESP_OK == status && storedVal == value) {
		// No change, do not schedule a flash write
		*isChanged = false;
	} else {
		// Schedule a write to flash
		*isChanged = true;
		xTimerReset(pCtrl->timer, 0);
	}

	xSemaphoreGive(pCtrl->mutex);
	return ESP_OK;
}


/*!
 * \brief Set the value of a single named unsigned 8-bit parameter
 *
 * \param [in] pKey pointer to the name of the parameter
 * \param [in] value
 *
 * \return ESP_OK Success
 * \return (other) Error code
 *
 */
esp_err_t paramMgrSetU8(const char * pKey, uint8_t value)
{
	taskCtrl_t *	pCtrl = taskCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	esp_err_t		status;
	csParamTab_t *	pTab;
	bool *			isChanged;
	uint8_t			storedVal;

	if (NULL == pKey) {
		return ESP_ERR_INVALID_ARG;
	}

	status = findEntry(pCtrl->listHead, pKey, &pTab, &isChanged);
	if (ESP_OK != status) {
		return status;
	}

	// Name matches, see if the type matches
	if (objtyp_u8 != pTab->objTyp) {
		gc_err("Attempted to set %s as u8", pKey);
		return ESP_ERR_INVALID_ARG;
	}

	// Validate the value
	status = (pTab->check) ? pTab->check(pTab, &value) : checkU8Range(pTab, &value);
	if (ESP_OK != status) {
		return ESP_ERR_INVALID_ARG;
	}

	// Lock access to the parameter block
	if (pdTRUE != xSemaphoreTake(pCtrl->mutex, pdMS_TO_TICKS(100))) {
		gc_err("Could not acquire mutex");
		return ESP_FAIL;
	}

	// Apply the new value to the RAM copy
	*(uint8_t *)pTab->pVar = value;

	// Compare the new value with that stored in flash
	status = nvs_get_u8(csNvHandle(pCtrl, pTab), pTab->nvKey, &storedVal);
	if (ESP_OK == status && storedVal == value) {
		*isChanged = false;
	} else {
		// Schedule a write to flash
		*isChanged = true;
		xTimerReset(pCtrl->timer, 0);
	}

	xSemaphoreGive(pCtrl->mutex);
	return ESP_OK;
}


/*!
 * \brief Set the value of a single named signed 32-bit parameter
 *
 * \param [in] pKey pointer to the name of the parameter
 * \param [in] value
 *
 * \return ESP_OK Success
 * \return (other) Error code
 *
 */
esp_err_t paramMgrSetI32(const char * pKey, int32_t value)
{
	taskCtrl_t *	pCtrl = taskCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	esp_err_t		status;
	csParamTab_t *	pTab;
	bool *			isChanged;
	int32_t			storedVal;

	if (NULL == pKey) {
		return ESP_ERR_INVALID_ARG;
	}

	status = findEntry(pCtrl->listHead, pKey, &pTab, &isChanged);
	if (ESP_OK != status) {
		return status;
	}

	// Name matches, see if the type matches
	if (objtyp_i32 != pTab->objTyp) {
		gc_err("Attempted to set %s as i32", pKey);
		return ESP_ERR_INVALID_ARG;
	}

	// Validate the value
	status = (pTab->check) ? pTab->check(pTab, &value) : checkI32Range(pTab, &value);
	if (ESP_OK != status) {
		return ESP_ERR_INVALID_ARG;
	}

	// Lock access to the parameter block
	if (pdTRUE != xSemaphoreTake(pCtrl->mutex, pdMS_TO_TICKS(100))) {
		gc_err("Could not acquire mutex");
		return ESP_FAIL;
	}

	// Apply the new value to the RAM copy
	*(int32_t *)pTab->pVar = value;

	// Compare the new value with that stored in flash
	status = nvs_get_i32(csNvHandle(pCtrl, pTab), pTab->nvKey, &storedVal);
	if (ESP_OK == status && storedVal == value) {
		*isChanged = false;
	} else {
		// Schedule a write to flash
		*isChanged = true;
		xTimerReset(pCtrl->timer, 0);
	}

	xSemaphoreGive(pCtrl->mutex);
	return ESP_OK;
}


/*!
 * \brief Set the value of a single named unsigned 32-bit parameter
 *
 * \param [in] pKey pointer to the name of the parameter
 * \param [in] value
 *
 * \return ESP_OK Success
 * \return (other) Error code
 *
 */
esp_err_t paramMgrSetU32(const char * pKey, uint32_t value)
{
	taskCtrl_t *	pCtrl = taskCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	esp_err_t		status;
	csParamTab_t *	pTab;
	bool *			isChanged;
	uint32_t		storedVal;

	if (NULL == pKey) {
		return ESP_ERR_INVALID_ARG;
	}

	status = findEntry(pCtrl->listHead, pKey, &pTab, &isChanged);
	if (ESP_OK != status) {
		return status;
	}

	// Name matches, see if the type matches
	if (objtyp_u32 != pTab->objTyp) {
		gc_err("Attempted to set %s as u32", pKey);
		return ESP_ERR_INVALID_ARG;
	}

	// Validate the value
	status = (pTab->check) ? pTab->check(pTab, &value) : checkU32Range(pTab, &value);
	if (ESP_OK != status) {
		return ESP_ERR_INVALID_ARG;
	}

	// Lock access to the parameter block
	if (pdTRUE != xSemaphoreTake(pCtrl->mutex, pdMS_TO_TICKS(100))) {
		gc_err("Could not acquire mutex");
		return ESP_FAIL;
	}

	// Apply the new value to the RAM copy
	*(uint32_t *)pTab->pVar = value;

	// Compare the new value with that stored in flash
	status = nvs_get_u32(csNvHandle(pCtrl, pTab), pTab->nvKey, &storedVal);
	if (ESP_OK == status && storedVal == value) {
		*isChanged = false;
	} else {
		// Schedule a write to flash
		*isChanged = true;
		xTimerReset(pCtrl->timer, 0);
	}

	xSemaphoreGive(pCtrl->mutex);
	return ESP_OK;
}


/*!
 * \brief Set the value of a single named string parameter
 *
 * \param [in] pKey pointer to the name of the parameter
 * \param [in] value
 *
 * \return WM_SUCCESS Success
 * \return (other) Error code
 *
 */
esp_err_t paramMgrSetStr(const char * pKey, const char * value)
{
	taskCtrl_t *	pCtrl = taskCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	esp_err_t		status;
	csParamTab_t *	pTab;
	bool *			isChanged;
	size_t			bufLen;

	if (NULL == pKey) {
		return ESP_ERR_INVALID_ARG;
	}

	status = findEntry(pCtrl->listHead, pKey, &pTab, &isChanged);
	if (ESP_OK != status) {
		return status;
	}

	// Name matches, see if the type matches
	if (objtyp_str != pTab->objTyp) {
		gc_err("Attempted to set %s as string", pKey);
		return ESP_ERR_INVALID_ARG;
	}

	// Validate the value
	if (pTab->check)
		status = pTab->check(pTab, (void *)value);
	else
		status = checkStrLen(pTab, (void *)value);
	if (ESP_OK != status) {
		return ESP_ERR_INVALID_ARG;
	}

	// Lock access to the parameter block
	if (pdTRUE != xSemaphoreTake(pCtrl->mutex, pdMS_TO_TICKS(100))) {
		gc_err("Could not acquire mutex");
		return ESP_FAIL;
	}

	// Apply the new value to the RAM copy
	strlcpy((char *)pTab->pVar, value, pTab->maxVal);

	// Compare the new value with that stored in flash
	char	buf[100];

	bufLen = sizeof(buf);
	status = nvs_get_str(csNvHandle(pCtrl, pTab), pTab->nvKey, buf, &bufLen);
	if (ESP_OK == status && strcmp(buf, (char *)pTab->pVar) == 0) {
		// No change to the stored value
		*isChanged = false;
	} else {
		// Schedule a write to flash
		*isChanged = true;
		xTimerReset(pCtrl->timer, 0);
	}

	xSemaphoreGive(pCtrl->mutex);
	return ESP_OK;
}


/*!
 * \brief Write a chunk of binary data
 *
 * This differs from the other paramMgr Set functions because the data is written
 * immediately, not in a deferred manner. Also, the data is not loaded automatically
 * by the parameter manager at start up.
 *
 * \param [in] pKey pointer to the name of the parameter
 * \param [in] value pointer to the data
 * \param [in] len length of the data
 *
 * \return WM_SUCCESS Success
 * \return (other) Error code
 *
 */
esp_err_t paramMgrSetBlob(const char * pKey, void * value, size_t len)
{
	taskCtrl_t *	pCtrl = taskCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	esp_err_t		status;

	if (NULL == pKey) {
		return ESP_ERR_INVALID_ARG;
	}

	// Lock access to the parameter block
	if (pdTRUE != xSemaphoreTake(pCtrl->mutex, pdMS_TO_TICKS(100))) {
		gc_err("Could not acquire mutex");
		return ESP_FAIL;
	}

	nvs_handle	nvsHandle = pCtrl->nvStandard;

	status = nvs_set_blob(nvsHandle, pKey, value, len);
	if (ESP_OK == status)
		status = nvs_commit(nvsHandle);

	xSemaphoreGive(pCtrl->mutex);
	return status;
}


/*!
 * \brief Read a chunk of binary data
 *
 * This differs from the other paramMgr Get functions because the data is not loaded
 * automatically at start time.
 *
 * \param [in] pKey pointer to the name of the parameter
 * \param [in] value pointer to the buffer to receive the data
 * \param [in] len pointer to data length
 * On entry contains the size of the buffer, on exit the number of byte read
 *
 * \return WM_SUCCESS Success
 * \return (other) Error code
 *
 */
esp_err_t paramMgrGetBlob(const char * pKey, void * value, size_t * len)
{
	taskCtrl_t *	pCtrl = taskCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	esp_err_t		status;

	if (NULL == pKey || NULL == len) {
		return ESP_ERR_INVALID_ARG;
	}

	// Lock access to the parameter block
	if (pdTRUE != xSemaphoreTake(pCtrl->mutex, pdMS_TO_TICKS(100))) {
		gc_err("Could not acquire mutex");
		return ESP_FAIL;
	}

	status = nvs_get_blob(pCtrl->nvStandard, pKey, value, len);

	xSemaphoreGive(pCtrl->mutex);
	return status;
}


/*!
 * \brief Return the size of a data blob
 *
 * \param [in] pKey pointer to the name of the parameter
 * \param [in] len pointer to variable to receive the blob size
 *
 * \return WM_SUCCESS Success
 * \return (other) Error code
 *
 */
esp_err_t paramMgrGetBlobSize(const char * pKey, size_t * len)
{
	taskCtrl_t *	pCtrl = taskCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	esp_err_t		status;

	if (NULL == pKey || NULL == len) {
		return ESP_ERR_INVALID_ARG;
	}

	// Lock access to the parameter block
	if (pdTRUE != xSemaphoreTake(pCtrl->mutex, pdMS_TO_TICKS(100))) {
		gc_err("Could not acquire mutex");
		return ESP_FAIL;
	}

	// Read the blob size
	status = nvs_get_blob(pCtrl->nvStandard, pKey, NULL, len);

	xSemaphoreGive(pCtrl->mutex);
	return status;
}


/*!
 * \brief Delete a blob from the parameter namespace
 *
 * \param [in] pKey pointer to the name of the parameter
 *
 * \return WM_SUCCESS Success
 * \return (other) Error code
 *
 */
esp_err_t paramMgrDeleteBlob(const char * pKey)
{
	taskCtrl_t *	pCtrl = taskCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	esp_err_t		status;

	if (NULL == pKey) {
		return ESP_ERR_INVALID_ARG;
	}

	// Lock access to the parameter block
	if (pdTRUE != xSemaphoreTake(pCtrl->mutex, pdMS_TO_TICKS(100))) {
		gc_err("Could not acquire mutex");
		return ESP_FAIL;
	}

	status = nvs_erase_key(pCtrl->nvStandard, pKey);
	if (ESP_OK == status) {
		status = nvs_commit(pCtrl->nvStandard);
	} else if (ESP_ERR_NVS_NOT_FOUND == status) {
		status = ESP_OK;
	}

	xSemaphoreGive(pCtrl->mutex);
	return status;
}


////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////


/**
* \brief Find named parameter in the table
*
*/
static esp_err_t findEntry(
	tableListEntry_t *	pList,
	const char *		key,
	csParamTab_t **		tab,
	bool **				isChanged
)
{
	csParamTab_t *		pTab;
	int					idx;

	if (NULL == key) {
		return ESP_ERR_INVALID_ARG;
	}

	// Step through the linked list of parameter tables
	for (; NULL != pList; pList = pList->next) {
		pTab = pList->table;

		for (idx = 0; idx < pList->tableSz; idx++, pTab++) {
			if (strcmp(key, pTab->nvKey) == 0) {
				// Found a match
				if (NULL != tab) {
					// Caller is requesting the reference to the table entry
					*tab = pTab;
				}
				if (NULL != isChanged) {
					// Caller is requesting the reference to isChanged flag
					*isChanged = &pList->ischanged[idx];
				}
				return ESP_OK;
			}
		}
	}

	return ESP_ERR_NOT_FOUND;
}


#if 0
static void setFlag(taskCtrl_t * pCtrl, const char * name, bool value)
{
	int8_t		i8Val;
	esp_err_t	status;

	// Read stored value
	status = nvs_get_i8(pCtrl->nvsHandle, name, &i8Val);
	if (ESP_OK != status || (i8Val ? true : false) != value) {
		// Stored value either not found or not the same as the desired value
		(void)nvs_set_i8(pCtrl->nvsHandle, name, value ? 1 : 0);
		(void)nvs_commit(pCtrl->nvsHandle);
	}
}


static bool getFlag(taskCtrl_t * pCtrl, const char * name)
{
	int8_t		i8Val;
	esp_err_t	status;

	// Read stored value
	status = nvs_get_i8(pCtrl->nvsHandle, name, &i8Val);
	if (ESP_OK == status) {
		return i8Val ? true : false;
	}

	return false;
}
#endif


/*!
 * \brief Set a parameter to its default value
 *
 * \param [in] pCtrl Pointer to the control structure
 * \param [in] pTab Pointer to table entry for parameter
 *
 */
static esp_err_t initParam(taskCtrl_t * pCtrl, csParamTab_t * pTab)
{
	// Shorthand reference to NVS handle for this item
	nvs_handle	nvs = csNvHandle(pCtrl, pTab);

	esp_err_t	status;
	uint8_t		i8Val;
	uint8_t		u8Val;
	uint32_t	u32Val;
	int32_t		i32Val;
	char *		next;

	if (pTab->init) {
		// Call the custom initialization function
		status = pTab->init(nvs, pTab);

	} else if (pTab->defVal) {
		//gc_dbg("Initialize %s to %s", pTab->nvKey, pTab->defVal);

		switch (pTab->objTyp)
		{
		case objtyp_bool:
			i8Val = strcmp(pTab->defVal, "0") == 0 ? 0 : 1;
			status = nvs_set_i8(nvs, pTab->nvKey, i8Val);
			if (ESP_OK == status) {
				// Update RAM copy
				*(bool *)pTab->pVar = i8Val ? true : false;
			}
			break;

		case objtyp_u8:
			u8Val = (uint8_t)strtoul(pTab->defVal, &next, 10);
			status = nvs_set_u8(nvs, pTab->nvKey, u8Val);
			if (ESP_OK == status) {
				// Update RAM copy
				*(uint8_t *)pTab->pVar = u8Val;
			}
			break;

		case objtyp_i32:
			i32Val = strtol(pTab->defVal, &next, 10);
			status = nvs_set_i32(nvs, pTab->nvKey, i32Val);
			if (ESP_OK == status) {
				// Update RAM copy
				*(int32_t *)pTab->pVar = i32Val;
			}
			break;

		case objtyp_u32:
			u32Val = strtoul(pTab->defVal, &next, 10);
			status = nvs_set_u32(nvs, pTab->nvKey, u32Val);
			if (ESP_OK == status) {
				// Update RAM copy
				*(uint32_t *)pTab->pVar = u32Val;
			}
			break;

		case objtyp_str:
			status = nvs_set_str(nvs, pTab->nvKey, pTab->defVal);
			break;

		default:
			status = ESP_FAIL;
			break;
		}

		if (ESP_OK == status) {
			status = nvs_commit(nvs);
			if (ESP_OK != status) {
				gc_err("Failed to commit change to %s", pTab->nvKey);
			}
		} else {
			gc_err("Failed to initialize %s", pTab->nvKey);
		}
	} else {
		gc_err("No initializer for %s", pTab->nvKey);
		status = ESP_FAIL;
	}

	return status;
}


/*!
 * \brief Reset the configuration to factory defaults
 *
 * \param [in] psmHandle Handle to the initialized PSM module
 *
 * \return WM_SUCCESS Success
 * \return (other) Error code
 *
 * For each configurable parameter, apply its default value
 *
 */
static esp_err_t resetConfig(taskCtrl_t * pCtrl)
{
	// Erase the param storage block
	nvs_erase_all(pCtrl->nvStandard);
	nvs_commit(pCtrl->nvStandard);

	// Set default values for parameters
	int					errCount = 0;
	tableListEntry_t *	pList;

	for (pList = pCtrl->listHead; NULL != pList; pList = pList->next) {
		unsigned int	idx;
		csParamTab_t *	pTab;

		for (idx = 0, pTab = pList->table; idx < pList->tableSz; idx++, pTab++) {
			if (csNvSpace_sticky == pTab->nvSpace)
				continue;

			if (initParam(pCtrl, pTab) != ESP_OK) {
				errCount += 1;
			}
		}
	}

	return (0 == errCount) ? ESP_OK : ESP_FAIL;
}


static esp_err_t loadParamTable(taskCtrl_t * pCtrl, csParamTab_t * pTab, int tabSz)
{
	int			idx;
	esp_err_t	status;
	int8_t		i8Val;
	uint8_t		u8Val;
	int32_t		i32Val;
	uint32_t	u32Val;
	size_t		bufLen;
	int			errCt  = 0;

	for (idx = 0; idx < tabSz; idx++, pTab++) {
		// Get the namespace handle for this parameter
		nvs_handle	nvs = csNvHandle(pCtrl, pTab);

		switch (pTab->objTyp)
		{
		case objtyp_bool:
			status = nvs_get_i8(nvs, pTab->nvKey, &i8Val);
			if (ESP_OK == status) {
				*(bool *)pTab->pVar = i8Val ? true : false;
				//gc_dbg("Param: %s = %s", pTab->nvKey, i8Val ? "true" : "false");
			}
			break;

		case objtyp_u8:
			status = nvs_get_u8(nvs, pTab->nvKey, &u8Val);
			if (ESP_OK == status) {
				*(uint8_t *)pTab->pVar = u8Val;
				//gc_dbg("Param: %s = %u", pTab->nvKey, u8Val);
			}
			break;

		case objtyp_i32:
			status = nvs_get_i32(nvs, pTab->nvKey, &i32Val);
			if (ESP_OK == status) {
				*(int32_t *)pTab->pVar = i32Val;
				//gc_dbg("Param: %s = %ld", pTab->nvKey, i32Val);
			}
			break;

		case objtyp_u32:
			status = nvs_get_u32(nvs, pTab->nvKey, &u32Val);
			if (ESP_OK == status) {
				*(uint32_t *)pTab->pVar = u32Val;
				//gc_dbg("Param: %s = %lu", pTab->nvKey, u32Val);
			}
			break;

		case objtyp_str:
			bufLen = pTab->maxVal;
			status = nvs_get_str(nvs, pTab->nvKey, (char *)pTab->pVar, &bufLen);
			if (ESP_OK == status) {
				//gc_dbg("Param: %s = %s", pTab->nvKey, (char *)pTab->pVar);
			}
			break;

		default:
			gc_err("Object type %d not supported for %s", pTab->objTyp, pTab->nvKey);
			status = ESP_OK;
			break;
		}

		if (ESP_ERR_NVS_NOT_FOUND == status) {
			// Initialize this parameter
			if (initParam(pCtrl, pTab) != ESP_OK) {
				errCt++;
			}
		} else if (ESP_OK != status) {
			gc_err("Error %d reading parameter: %s", status, pTab->nvKey);
			errCt++;
		}
	}

	return (errCt == 0) ? ESP_OK : ESP_FAIL;
}


static nvs_handle csNvHandle(taskCtrl_t * pCtrl, csParamTab_t * pItem)
{
	switch (pItem->nvSpace)
	{
	case csNvSpace_sticky:
		return pCtrl->nvSticky;
	default:
		return pCtrl->nvStandard;
	}
}


#if 0
static bool itemExists(taskCtrl_t * pCtrl, csParamTab_t * pItem)
{
	esp_err_t	status;
	size_t		len = 0;

	// Do a dummy read of the item to check for presence
	status = nvs_get_blob(csNvHandle(pCtrl, pItem), pItem->nvKey, NULL, &len);

	return (ESP_OK == status) ? true : false;
}
#endif


/*!
 * \brief Validate the range of an 8-bit integer value
 *
 * \param [in]  pTab    pointer to the table entry of the parameter
 * \param [in]  pValue  pointer to the value to be checked
 *
 * \return 0 Value is within range
 * \return -1 Value is out of range
 *
 * Treat the referenced value as an int and make sure it falls within the
 * min and max values for the parameter
 *
 */
static esp_err_t checkU8Range(csParamTab_t * pTab, void * pValue)
{
	int32_t		iVal = (int32_t)(*(uint8_t *)pValue);

	if (iVal < pTab->minVal || iVal > pTab->maxVal) {
		gc_err("%s value is out of bounds", pTab->nvKey);
		return ESP_FAIL;
	}

	return ESP_OK;
}


/*!
 * \brief Validate the range of a 32-bit signed integer value
 *
 * \param [in]  pTab    pointer to the table entry of the parameter
 * \param [in]  pValue  pointer to the value to be checked
 *
 * \return 0 Value is within range
 * \return -1 Value is out of range
 *
 * Treat the referenced value as an int and make sure it falls within the
 * min and max values for the parameter
 *
 */
static esp_err_t checkI32Range(csParamTab_t * pTab, void * pValue)
{
	int32_t	iValue = *(int32_t *)pValue;

	if (iValue < pTab->minVal || iValue > pTab->maxVal) {
		gc_err("%s value is out of bounds", pTab->nvKey);
		return ESP_FAIL;
	}

	return ESP_OK;
}


/*!
 * \brief Validate the range of a 32-bit integer value
 *
 * \param [in]  pTab    pointer to the table entry of the parameter
 * \param [in]  pValue  pointer to the value to be checked
 *
 * \return 0 Value is within range
 * \return -1 Value is out of range
 * (time zone, etc)
 * Treat the referenced value as an int and make sure it falls within the
 * min and max values for the parameter
 *
 */
static esp_err_t checkU32Range(csParamTab_t * pTab, void * pValue)
{
	uint32_t	iValue = *(uint32_t *)pValue;

	if (iValue < pTab->minVal || iValue > pTab->maxVal) {
		gc_err("%s value is out of bounds", pTab->nvKey);
		return ESP_FAIL;
	}

	return ESP_OK;
}


/*!
 * \brief Validate the length of a string value
 *
 * \param [in]  pTab    pointer to the table entry of the parameter
 * \param [in]  pValue  pointer to the value to be checked
 *
 * \return 0 String is within length limits
 * \return -1 String is too long
 *
 * Treat the referenced value as string, make sure its length falls within
 * the min/max range
 *
 */
static esp_err_t checkStrLen(csParamTab_t * pTab, void * pValue)
{
	int		len = strlen( (char *)pValue );

	if (len < pTab->minVal || len >= pTab->maxVal) {
		gc_err("%s length (%d) is out of bounds", pTab->nvKey, len);
		return ESP_FAIL;
	}

	return ESP_OK;
}


static void nvTask(void * taskParam)
{
	taskCtrl_t *		pCtrl = (taskCtrl_t *)taskParam;
	esp_err_t			status;
	int					i;
	tableListEntry_t *	pList;
	csParamTab_t *		pTab;

	while (1)
	{
		// Wait for signal to update flash
		(void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

		// Step through the linked list of tables
		for (pList = pCtrl->listHead; NULL != pList; pList = pList->next) {

			// Step through the entries in the current table
			for (i = 0, pTab = pList->table; i < pList->tableSz; i++, pTab++) {

				// Skip over unchanged entries
				if (!pList->ischanged[i])
					continue;

				// Get the namespace handle for this item
				nvs_handle	nvs = csNvHandle(pCtrl, pTab);

				xSemaphoreTake(pCtrl->mutex, portMAX_DELAY);

				// Write the object to flash
				switch (pTab->objTyp)
				{
				case objtyp_bool:
					// Translate bool to int8_t having either 0 or 1
					//gc_dbg("Flash update \"%s\": %s", pTab->nvKey, *(bool *)pTab->pVar ? "true" : "false");
					status = nvs_set_i8(nvs, pTab->nvKey, *(bool *)pTab->pVar ? 1 : 0);
					break;

				case objtyp_u8:
					//gc_dbg("Flash update \"%s\": %u", pTab->nvKey, *(uint8)t *)pTab->pVar);
					status = nvs_set_u8(nvs, pTab->nvKey, *(uint8_t *)pTab->pVar);
					break;

				case objtyp_i32:
					//gc_dbg("Flash update \"%s\" %d:", pTab->nvKey, *(int32)t *)pTab->pVar);
					status = nvs_set_i32(nvs, pTab->nvKey, *(int32_t *)pTab->pVar);
					break;

				case objtyp_u32:
					//gc_dbg("Flash update \"%s\": %u", pTab->nvKey, *(uint32)t *)pTab->pVar);
					status = nvs_set_u32(nvs, pTab->nvKey, *(uint32_t *)pTab->pVar);
					break;

				case objtyp_str:
					//gc_dbg("Flash update \"%s\": %s", pTab->nvKey, (char *)pTab->pVar);
					status = nvs_set_str(nvs, pTab->nvKey, (const char *)pTab->pVar);
					break;

				default:
					gc_err("Unsupported data type for %s", pTab->nvKey);
					status = ESP_FAIL;
					break;
				}

				// Clear the 'changed' flag
				pList->ischanged[i] = false;

				if (ESP_OK == status) {
					// Commit this change
					status = nvs_commit(nvs);
					if (ESP_OK != status) {
						gc_err("Failed to commit change to %s", pTab->nvKey);
					}
				} else {
					gc_err("Failed to update %s", pTab->nvKey);
				}

				xSemaphoreGive(pCtrl->mutex);
			}
		}
	}
}


/**
 * \brief Signal task to update parameter changes to flash
 */
static void timerCallback(TimerHandle_t tmr)
{
	taskCtrl_t *	pCtrl = (taskCtrl_t *)pvTimerGetTimerID(tmr);

	// Notify the task to update storage
	xTaskNotifyGive(pCtrl->taskHandle);
}
