/*
 * event_callback.c
 *
 */

#include "cs_common.h"
#include "cs_heap.h"
#include "event_callback.h"

// Comment out the MOD_NAME line to disable debug prints from this file
#define MOD_NAME	"cs_evt_callb"
#include "mod_debug.h"

typedef struct cbTab_s	cbTab_t;

// Structure for storing registered callback functions
struct cbTab_s {
	cbTab_t *		next;
	eventCbFunc_t	cbFunc;		// Function to be called
	uint32_t		cbData;		// Data to pass to the called function
};


// Callback control structure
typedef struct {
	SemaphoreHandle_t	tabMutex;	// Table access mutex
	cbTab_t *			head;		// Linked list head
	cbTab_t *			tail;		// Linked list tail
} cbCtrl_t;

static cbTab_t * findEntry(cbTab_t * head, eventCbFunc_t func);


esp_err_t eventRegisterCreate(cbHandle_t * cbHandle)
{
	int			status;
	cbCtrl_t *	pCtrl;

	if (NULL == cbHandle) {
		return ESP_ERR_INVALID_ARG;
	}

	// Allocate memory to hold the control structure
	pCtrl = (cbCtrl_t *)cs_heap_calloc(1, sizeof(*pCtrl));
	if (NULL == pCtrl) {
		return ESP_ERR_NO_MEM;
	}

	// Allocate mutex for table access
	pCtrl->tabMutex = xSemaphoreCreateMutex();
	if (NULL == pCtrl->tabMutex) {
		status = ESP_FAIL;
		goto exitFail;
	}

	// Pass back the pointer to the control structure as a handle
	*cbHandle = (cbHandle_t)pCtrl;
	return ESP_OK;

exitFail:
	*cbHandle = (cbHandle_t)0;
	cs_heap_free(pCtrl);
	return status;
}


esp_err_t eventRegisterCallback(
	cbHandle_t		cbHandle,
	eventCbFunc_t	cbFunc,
	uint32_t		cbData
)
{
	if (0 == cbHandle) {
		return ESP_ERR_INVALID_ARG;
	}

	cbCtrl_t *	pCtrl  = (cbCtrl_t *)cbHandle;
	esp_err_t	status = ESP_OK;

	if (NULL == pCtrl || NULL == cbFunc) {
		gc_err("NULL parameter passed");
		return ESP_ERR_INVALID_ARG;
	}

	xSemaphoreTake(pCtrl->tabMutex, portMAX_DELAY);

	if (findEntry(pCtrl->head, cbFunc) != NULL) {
		// The function is already registered
		gc_err("Function already registered");
		status = ESP_FAIL;
		goto exitMutex;
	}

	// Allocate memory to hold the entry
	cbTab_t *	entry;
	if ((entry = cs_heap_calloc(1, sizeof(*entry))) == NULL) {
		gc_err("Failed to allocate memory");
		status = ESP_ERR_NO_MEM;
		goto exitMutex;
	}

	entry->cbFunc = cbFunc;
	entry->cbData = cbData;
	entry->next   = NULL;

	// Add the function to the tail of the linked list
	if (NULL == pCtrl->head) {
		// This is the first addition to the list
		pCtrl->head = entry;
	} else {
		// Adding to the tail
		pCtrl->tail->next = entry;
	}
	pCtrl->tail = entry;

	status = ESP_OK;

exitMutex:
	xSemaphoreGive(pCtrl->tabMutex);
	return status;
}


esp_err_t eventUnregisterCallback(cbHandle_t cbHandle, eventCbFunc_t cbFunc)
{
	cbCtrl_t *	pCtrl  = (cbCtrl_t *)cbHandle;

	if (NULL == pCtrl || NULL == cbFunc) {
		return ESP_ERR_INVALID_ARG;
	}

	xSemaphoreTake(pCtrl->tabMutex, portMAX_DELAY);

	esp_err_t	status = ESP_FAIL;

	cbTab_t *	entry;
	cbTab_t *	prev = NULL;

	for (entry = pCtrl->head; NULL != entry; entry = entry->next) {
		if (cbFunc == entry->cbFunc) {
			if (NULL == prev) {
				// Remove from the head of the list
				pCtrl->head = entry->next;
			} else {
				prev->next = entry->next;
			}

			// Check if the tail need to be adjusted
			if (pCtrl->tail == entry) {
				pCtrl->tail = prev;
			}

			// Success
			cs_heap_free(entry);
			status = ESP_OK;
			break;
		}

		prev = entry;
	}

	xSemaphoreGive(pCtrl->tabMutex);
	return status;
}


void eventNotify(
	cbHandle_t		cbHandle,
	callCtx_t		ctx,
	uint32_t		evtCode,
	uint32_t		evtData
)
{
	cbCtrl_t *	pCtrl = (cbCtrl_t *)cbHandle;
	if (NULL == pCtrl) {
		return;
	}
	xSemaphoreTake(pCtrl->tabMutex, portMAX_DELAY);

	cbTab_t *	entry;
	for (entry = pCtrl->head; NULL != entry; entry = entry->next) {
		entry->cbFunc(entry->cbData, ctx, evtCode, evtData);
	}
	xSemaphoreGive(pCtrl->tabMutex);
}


const char * eventCtxName(callCtx_t ctx)
{
	switch (ctx)
	{
	case callCtx_null:
		return "null";
	case callCtx_self:
		return "self";
	case callCtx_local:
		return "local";
	case callCtx_localApi:
		return "localApi";
	default:
		return "Undefined";
	}
}


/**
 * \brief Find the table entry for the given callback function
 */
static cbTab_t * findEntry(cbTab_t * head, eventCbFunc_t func)
{
	cbTab_t *	slot;

	for (slot = head; NULL != slot; slot = slot->next) {
		if (func == slot->cbFunc)
			return slot;
	}

	return NULL;
}
