/*
 * cs_events.h
 *
 *  Created on: Oct 8, 2019
 *      Author: wesd
 */

#ifndef COMPONENTS_CS_CORE_INCLUDE_CS_EVENTS_H_
#define COMPONENTS_CS_CORE_INCLUDE_CS_EVENTS_H_

#include <stdint.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

// The number of event IDs reserved for each module
#define CS_EVT_EVT_ID_BITS				(10)	// 1024 events per module
#define CS_EVT_MOD_NUM_IDS				((uint32_t)(1 << CS_EVT_EVT_ID_BITS))
#define CS_EVT_MAX_EVT_ID				((uint32_t)(CS_EVT_MOD_NUM_IDS - 1))

// Extract the event ID from the event code
#define CS_EVT_EVT_ID(eCode)			((csEvtEvtId_t)(eCode & CS_EVT_MAX_EVT_ID))

// Extract the module ID from the event code
#define CS_EVT_MOD_ID(eCode)			((csEvtModId_t)((eCode >> CS_EVT_EVT_ID_BITS)))

// Define the event ID base for a given module id
#define CS_EVT_CODE_BASE(eMod)			((csEvtCode_t)eMod << CS_EVT_EVT_ID_BITS)

// Construct an event code from a module id and event id
#define CS_DEF_EVT_CODE(eMod, eCode)	((csEvtCode_t)(CS_EVT_CODE_BASE(eMod) + eCode))

// Define the Core module IDs
#define CS_EVT_MOD_ID_CS_CORE			((csEvtModId_t)(  0))
#define CS_EVT_MOD_ID_CS_FRAMEWORK		((csEvtModId_t)(  1))
#define CS_EVT_MOD_ID_CS_LOCAL_API		((csEvtModId_t)(  4))

// Reserve the first 100 module ids for core
// Application-specific events begin here
#define CS_EVT_MOD_APP_BASE			((uint32_t)(100))

/**
 * \brief Macro for application subsystems to define event modules
 */
#define CS_EVT_MOD_APP(idx)			(CS_EVT_MOD_APP_BASE + ((idx * CS_EVT_MOD_SIZE)))


/**
 * \brief Event sources
 */
typedef enum {
	csEvtSrc_null = 0,	// Should never happen, ignore events from this source
	csEvtSrc_user,		// User interaction (e.g. button press)
	csEvtSrc_internal,	// Internal (core, framework, etc)
	csEvtSrc_localApi,	// Local API
	csEvtSrc_external	// External source other than Local API
} csEvtSrc_t;


typedef void *		csEvtHandle_t;
typedef uint32_t	csEvtModId_t;
typedef uint32_t	csEvtEvtId_t;
typedef uint32_t	csEvtCode_t;
typedef void *		csEvtData_t;


/**
 * \brief Form of an event handler function
 *
 * \param [in] cbData Callback data provided by registration
 * \param [in] evtSource Source of the event
 * \param [in] evtCode Event code
 * \param [in] evtData Event-specific data
 *
 */
typedef void (* csEvtHandler_t)(
	uint32_t	cbData,
	csEvtSrc_t	evtSource,
	csEvtCode_t	evtCode,
	csEvtData_t	evtData
);


/**
 * \brief Initialize event support
 *
 * \return ESP_OK Success
 * \return ESP_ERR_NO_MEM Insufficient memory
 * \return ESP_FAIL Other error
 */
esp_err_t csEventInit(void);

/**
 * \brief Create an event for a module, obtain a handle for reporting events
 *
 * \param [out] handle Pointer to where to store the handle
 * \param [in] modId Module id
 * \param [in] pointer to name string for module, used for debug and error reporting
 *
 * The handle ensures that only the module that "owns" a set of
 * events is able to send notifications of events from that set
 *
 * \return ESP_OK Success
 * \return ESP_ERR_INVALID_ARG NULL passed for handle
 * \return ESP_ERR_INVALID_STATE Event support was not initialized
 * \return ESP_ERR_NO_MEM Insufficient memory
 * \return ESP_FAIL The event set is already owned by another subsystem
 */
esp_err_t csEventCreate(csEvtHandle_t * handle, csEvtModId_t modId, const char * modName);

/**
 * \brief Register to receive events from one or more modules
 *
 * \param [in] evtModList Pointer to array of one or more module ids
 * \param [in] evtModListSz Size of the module array
 * \param [in] cbName Name of callback, for debug reporting
 * \param [in] cbFunc Pointer to the event handler function
 * \param [in] cbData Data to be passed to the event handler
 *
 * \return ESP_OK success
 * \return ESP_ERR_INVALID_ARG NULL passed for evtSetList or cbFunc
 * \return ESP_ERR_INVALID_STATE Event support was not initialized
 * \return ESP_ERR_NO_MEM Insufficient memory
 * \return ESP_FAIL Other error
 *
 */
esp_err_t csEventRegister(
	const csEvtModId_t *	evtModList,
	unsigned int			evtModListSz,
	const char *			cbName,
	csEvtHandler_t			cbFunc,
	uint32_t				cbData
);


/**
 * \brief Remove registration for one or more sources
 *
 * \param [in] evtModList Pointer to array of one or more module ids
 * \param [in] evtModListSz Size of the array
 * \param [in] cbFunc Pointer to the event handler function
 *
 * Passing NULL pointer for sourceList will remove cbFunc from all events
 *
 * \return ESP_OK success
 * \return ESP_ERR_INVALID_STATE Event support was not initialized
 * \return ESP_ERR_INVALID_ARG NULL passed for cbFunc
 * \return ESP_ERR_NOT_FOUND cbFunc was not registered
 * \return ESP_FAIL Other error
 *
 */
esp_err_t csEventUnregister(
	const csEvtModId_t *	evtModList,
	unsigned int			evtModListSz,
	csEvtHandler_t			cbFunc
);


/**
 * \brief Call all handlers registered for an event
 *
 * \param [in] handle Obtained from \ref csEventCreate
 * \param [in] evtSource The source of the event
 * \param [in] evtCode Identifies the event
 * \param [in] evtData Event-specific data
 */
void csEventNotify(
	csEvtHandle_t	handle,
	csEvtSrc_t		evtSource,
	csEvtCode_t		evtCode,
	csEvtData_t		evtData
);


/**
 * \brief For the given event code return the name of the related module
 */
const char * csEventModuleName(csEvtCode_t evtCode);


/**
 * \brief Provide a name string for the event source
 *
 * \param [in] evtSource Event source
 *
 * \return Pointer to string corresponding to the source
 * \return Pointer to the string "Undefined" if the source is not recognized
 *
 */
const char * csEventSourceName(csEvtSrc_t evtSource);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_CS_CORE_INCLUDE_CS_EVENTS_H_ */
