/*
 * event_callback.h
 *
 *  Created on: Jan 18, 2018
 *      Author: wesd
 */

#ifndef __CS_EVENT_CALLBACK_H__
#define __CS_EVENT_CALLBACK_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_system.h"

/**
 * \brief Context of a callback function
 * 
 * This identifies which context is driving a callback event
 *
 */
typedef enum {
	callCtx_null = 0,		//
	callCtx_self,			// Callback invoked by an API called by this task
	callCtx_local,			// Device's internal control processes
	callCtx_localApi		// Local API
} callCtx_t;

/**
 * \brief Form of an event callback function
 *
 * \param [in] cbData Data passed into \ref eventRegisterCallback
 * \param [in] ctx Context under which this function is called
 * \param [in] event Event code, specific to the calling process
 * \param [in] eventData Event data, specific to the caller and event code
 *
 */
typedef void (* eventCbFunc_t)(
	uint32_t	cbData,
	callCtx_t	ctx,
	uint32_t	evtCode,
	uint32_t	evtData
);

// Handle to a callback object
typedef uint32_t	cbHandle_t;


/**
 * \brief Create an event callback
 *
 * \param [out] cbHandle Pointer to the callback handle to be filled
 *
 * \return ESP_OK On Success
 * \return ESP_ERR_INVALID_ARG If cbHandle is NULL or tabSz is less than 1
 * \return ESP_ERR_NO_MEM If enough memory to allocate the event data structure
 *
 */
esp_err_t eventRegisterCreate(cbHandle_t * cbHandle);


/**
 * \brief Register a callback to be notified of events
 *
 * \param [in] cbHandle Handle previously allocated by \ref eventRegisterCreate
 * \param [in] cbFunc Reference to callback function (see \ref eventCbFunc_t)
 * \param [in] cbData Data to be passed to the callback function
 *
 * \return ESP_OK On Success
 * \return ESP_ERR_INVALID_ARG If cbHandle is 0 or if cbFunc is NULL
 * \return ESP_FAIL If the registration table is full or if cbFunc is already registered
 *
 */
esp_err_t eventRegisterCallback(
	cbHandle_t		cbHandle,
	eventCbFunc_t	cbFunc,
	uint32_t		cbData
);


/**
 * \brief Unregister a callback function
 *
 * \param [in] cbHandle Handle previously allocated by \ref eventRegisterCreate
 * \param [in] cbFunc Reference to callback function (see \ref eventCbFunc_t)
 *
 * \return ESP_OK On Success
 * \return ESP_ERR_INVALID_ARG If cbHandle is 0 or cbFunc is NULL
 * \return ESP_FAIL If the cbFunc not found in the registration table
 *
 */
esp_err_t eventUnregisterCallback(
	cbHandle_t		cbHandle,
	eventCbFunc_t	cbFunc
);

/**
 * \brief Pass event data to all registered callback functions
 *
 * \param [in] cbHandle Handle previously allocated by \ref eventRegisterCreate
 * \param [in] ctx Context that is invoking the callbacks
 * \param [in] evtCode Event code
 * \param [in] evtData Event data
 *
 */
void eventNotify(
	cbHandle_t		cbHandle,
	callCtx_t		ctx,
	uint32_t		evtCode,
	uint32_t		evtData
);

/**
 * \brief Provide a name string for the event context
 *
 * \param [in] ctx Calling context
 *
 * \return Pointer to string corresponding to the context
 * \return Pointer to the string "Undefined" if the passed context is
 * not recognized
 *
 */
const char * eventCtxName(callCtx_t ctx);

#ifdef __cplusplus
}
#endif

#endif /* __CS_EVENT_CALLBACK_H__ */
