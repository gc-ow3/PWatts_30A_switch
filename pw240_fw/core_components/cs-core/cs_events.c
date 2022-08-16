/*
 * cs_events.c
 *
 *  Created on: Oct 8, 2019
 *      Author: wesd
 */

#include "cs_common.h"
#include "cs_heap.h"
#include "cs_events.h"

// Comment out the MOD_NAME line to disable debug prints from this file
#define MOD_NAME	"cs_events"
#include "mod_debug.h"


//------------------------------------------------------------------------------
// Data type definitions
//------------------------------------------------------------------------------

// Forward structure type references
typedef struct evtModule_s		evtModule_t;

typedef struct evtCbHandler_s	evtCbHandler_t;


/**
 * \brief structure for building linked list of callback handlers
 */
struct evtCbHandler_s {
	evtCbHandler_t *	next;		// Next handler
	const char *		cbName;		// Name of callback
	csEvtHandler_t		cbFunc;		// Handler function
	uint32_t			cbData;		// Data to be passed to the handler
};


/**
 * \brief structure for building linked list of event modules
 */
struct evtModule_s {
	evtModule_t *		next;			// Next module list entry
	csEvtModId_t		modId;			// Module ID
	const char *		modName;		// Module name
	evtCbHandler_t *	cbListHead;		// Head of linked list of handlers for this event set
	evtCbHandler_t *	cbListTail;		// Tail of linked list
};


/**
 * \brief Internal control structure
 */
typedef struct {
	SemaphoreHandle_t	mutex;
	evtModule_t *		modListHead;
	evtModule_t *		modListTail;
} evtControl_t;


//------------------------------------------------------------------------------
// Private function forward references
//------------------------------------------------------------------------------

static evtModule_t * findEvtMod(evtControl_t * pCtrl, csEvtModId_t modId);

static evtCbHandler_t * findEvtHandler(evtModule_t * module, csEvtHandler_t cbFunc);


//------------------------------------------------------------------------------
// Private data
//------------------------------------------------------------------------------
static evtControl_t *	evtControl;


esp_err_t csEventInit(void)
{
	evtControl_t *	pCtrl = evtControl;
	if (NULL != pCtrl) {
		return ESP_OK;
	}

	pCtrl = cs_heap_calloc(1, sizeof(*pCtrl));
	if (NULL == pCtrl) {
		return ESP_ERR_NO_MEM;
	}

	esp_err_t	status;

	pCtrl->mutex = xSemaphoreCreateMutex();
	if (NULL == pCtrl->mutex) {
		status = ESP_FAIL;
		goto exitMem;
	}

	evtControl = pCtrl;
	return ESP_OK;

exitMem:
	cs_heap_free(pCtrl);
	return status;
}


esp_err_t csEventCreate(csEvtHandle_t * handle, csEvtModId_t modId, const char * modName)
{
	*handle = NULL;

	evtControl_t *	pCtrl = evtControl;
	if (NULL == pCtrl) {
		return ESP_ERR_INVALID_STATE;
	}

	if (NULL == handle || NULL == modName) {
		return ESP_ERR_INVALID_ARG;
	}

	esp_err_t	status;

	xSemaphoreTake(pCtrl->mutex, portMAX_DELAY);

	evtModule_t *	module = findEvtMod(pCtrl, modId);
	if (NULL == module) {
		// Add module to the list
		if ((module = cs_heap_calloc(1, sizeof(*module))) != NULL) {

			// Set the module identification stuff
			module->modId   = modId;
			module->modName = modName;

			// Add it to the end of the linked list
			module->next = NULL;
			if (NULL == pCtrl->modListHead) {
				// First entry
				pCtrl->modListHead = module;
			} else {
				pCtrl->modListTail->next = module;
			}
			pCtrl->modListTail = module;
			*handle = (void *)module;
			status = ESP_OK;
		} else {
			status = ESP_ERR_NO_MEM;
		}
	} else {
		// Module already exists
		gc_err("Module (ID:%u) already installed", modId);
		status = ESP_FAIL;
	}

	xSemaphoreGive(pCtrl->mutex);
	return status;
}


esp_err_t csEventRegister(
	const csEvtModId_t *	evtModList,
	unsigned int			evtModListSz,
	const char *			cbName,
	csEvtHandler_t			cbFunc,
	uint32_t				cbData
)
{
	evtControl_t *	pCtrl = evtControl;
	if (NULL == pCtrl) {
		return ESP_ERR_INVALID_STATE;
	}

	if (NULL == evtModList || evtModListSz < 1 || NULL == cbFunc) {
		return ESP_ERR_INVALID_ARG;
	}

	if (NULL == cbName) {
		cbName = "undefined";
	}

	xSemaphoreTake(pCtrl->mutex, portMAX_DELAY);

	int			i;
	esp_err_t	status = ESP_OK;

	for (i = 0; i < evtModListSz; i++) {
		// For each module in the list, find its structure
		csEvtModId_t	modId = evtModList[i];

		evtModule_t *	module = findEvtMod(pCtrl, modId);
		if (NULL == module) {
			// Module is not installed
			gc_err("Module (ID:%u) for handler (%s) is not installed", modId, cbName);
			status = ESP_FAIL;
			break;
		}

		// Check if the handler is already installed
		evtCbHandler_t *	handler = findEvtHandler(module, cbFunc);
		if (handler) {
			// Handler is already installed
			gc_err("Module (ID:%u), handler (%s) already installed", modId, cbName);
			status = ESP_FAIL;
			break;
		}

		// Create the handler structure
		handler = cs_heap_calloc(1, sizeof(*handler));
		if (NULL == handler) {
			gc_err("Failed to allocate handler structure");
			status = ESP_ERR_NO_MEM;
			break;
		}

		handler->cbName = cbName;
		handler->cbFunc = cbFunc;
		handler->cbData = cbData;
		handler->next   = NULL;

		// Add the structure to the list
		if (NULL == module->cbListHead) {
			// First entry
			module->cbListHead = handler;
		} else {
			// Add to the end of the linked list
			module->cbListTail->next = handler;
		}
		module->cbListTail = handler;
	}

	xSemaphoreGive(pCtrl->mutex);
	return status;
}


esp_err_t csEventUnregister(
	const csEvtModId_t *	evtModList,
	unsigned int			evtModListSz,
	csEvtHandler_t			cbFunc
)
{
	evtControl_t *	pCtrl = evtControl;
	if (NULL == pCtrl) {
		return ESP_ERR_INVALID_STATE;
	}

	if (NULL == evtModList || evtModListSz < 1 || NULL == cbFunc) {
		return ESP_ERR_INVALID_ARG;
	}

	esp_err_t	status = ESP_OK;

	xSemaphoreTake(pCtrl->mutex, portMAX_DELAY);

	// Step through the list of modules
	int		i;
	for (i = 0; i < evtModListSz; i++) {
		// For each module in the list, find its structure
		csEvtModId_t	modId = evtModList[i];

		evtModule_t *	module = findEvtMod(pCtrl, modId);
		if (NULL == module) {
			// Module is not installed, just ignore it
			continue;
		}

		// Step through the handlers attached to this module
		evtCbHandler_t *	handler;
		evtCbHandler_t *	prev    = NULL;
		for (handler = module->cbListHead; NULL != handler; handler = handler->next) {
			if (cbFunc == handler->cbFunc) {

				// Unlink the handler
				if (NULL == prev) {
					// Module is at the head of the list
					module->cbListHead = handler->next;
				} else {
					prev->next = handler->next;
				}

				if (handler == module->cbListTail) {
					module->cbListTail = prev;
				}

				// Release the handler memory
				cs_heap_free(handler);

				// Done with this module
				break;
			}

			prev = handler;
		}
	}

	xSemaphoreGive(pCtrl->mutex);
	return status;
}


void csEventNotify(
	csEvtHandle_t	handle,
	csEvtSrc_t		evtSource,
	csEvtCode_t		evtCode,
	csEvtData_t		evtData
)
{
	evtControl_t *	pCtrl = evtControl;
	if (NULL == pCtrl) {
		gc_err("Event notification not initialized");
		return;
	}

	if (NULL == handle) {
		gc_err("NULL handle passed");
		return;
	}

	// Extract and validate the event id from the event code
	csEvtEvtId_t	evtId = CS_EVT_EVT_ID(evtCode);
	if (evtId > CS_EVT_MAX_EVT_ID) {
		return;
	}

	// Extract the module ID from the event code
	csEvtModId_t	modId = CS_EVT_MOD_ID(evtCode);

	xSemaphoreTake(pCtrl->mutex, portMAX_DELAY);

	// Find the structure for this module
	evtModule_t * module = findEvtMod(pCtrl, modId);
	if (NULL != module) {
		// Verify the caller is authorized to send this event
		// (Caller must have the handle return from event creation)
		if (module == (evtModule_t *)handle) {
			// Call handlers registered with this module
			evtCbHandler_t *	handler;
			for (handler = module->cbListHead; NULL != handler; handler = handler->next) {
				handler->cbFunc(handler->cbData, evtSource, evtCode, evtData);
			}
		} else {
			gc_err("Caller does not own the event module (%s)", module->modName);
		}
	} else {
		gc_err("Module (ID:%u) event not installed", modId);
	}

	xSemaphoreGive(pCtrl->mutex);
}


const char * csEventModuleName(csEvtCode_t evtCode)
{
	evtControl_t *	pCtrl = evtControl;
	if (NULL == pCtrl) {
		return NULL;
	}

	uint32_t		modId = CS_EVT_MOD_ID(evtCode);
	const char *	ret   = NULL;

	xSemaphoreTake(pCtrl->mutex, portMAX_DELAY);

	// Find the structure for this module
	evtModule_t *	module = findEvtMod(pCtrl, modId);
	if (NULL != module) {
		ret = module->modName;
	}

	xSemaphoreGive(pCtrl->mutex);
	return ret;
}


const char * csEventSourceName(csEvtSrc_t evtSource)
{
	switch(evtSource)
	{
	case csEvtSrc_null:
		return "null";
	case csEvtSrc_user:
		return "user";
	case csEvtSrc_internal:
		return "internal";
	case csEvtSrc_localApi:
		return "localAPI";
	case csEvtSrc_external:
		return "external";
	default:
		return "Undefined";
	}
}


//------------------------------------------------------------------------------
// Private functions
//------------------------------------------------------------------------------


static evtModule_t * findEvtMod(evtControl_t * pCtrl, csEvtModId_t modId)
{
	evtModule_t *	ret;

	for (ret = pCtrl->modListHead; NULL != ret; ret = ret->next) {
		if (modId == ret->modId) {
			// Match found
			return ret;
		}
	}

	// No match found
	return NULL;
}


static evtCbHandler_t * findEvtHandler(evtModule_t * module, csEvtHandler_t cbFunc)
{
	evtCbHandler_t *	ret;

	for (ret = module->cbListHead; NULL != ret; ret = ret->next) {
		if (cbFunc == ret->cbFunc) {
			return ret;
		}
	}
	// No match found
	return NULL;
}
