/*
 * cs_framework.h
 *
 *  Created on: Jan 7, 2019
 *      Author: wesd
 */

#ifndef COMPONENTS_CS_CORE_INCLUDE_CS_FRAMEWORK_H_
#define COMPONENTS_CS_CORE_INCLUDE_CS_FRAMEWORK_H_

#include "cs_common.h"
#include "event_callback.h"
#include "esp_event.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif


//! Events that may be reported by the framework
typedef enum {
	frmwkEvtCode_null = 0,
	frmwkEvtCode_provStart,
	frmwkEvtCode_provDone,
	frmwkEvtCode_provFail,
	frmwkEvtCode_started,
	frmwkEvtCode_staConnecting,
	frmwkEvtCode_staConnected,
	frmwkEvtCode_staDisconnect,
	frmwkEvtCode_staGotIp,
	frmwkEvtCode_staLostIp,
	frmwkEvtCode_sapStarted,
} frmwkEvtCode_t;


//! Framework event data
typedef union {
	struct {
		bool		changed;
		uint32_t	ipAddr;
		uint32_t	gwAddr;
		uint32_t	netmask;
	} gotIp;
	struct {
		httpd_handle_t	httpd;
	} sapStarted;
} frmwkEvtData_t;


/**
 * \brief Initialize ConnectSense Wi-Fi framework
 *
 * \note To be called during core initialization
 *
 * \return ESP_OK Success
 * \return ESP_FAIL Failed
 *
 */
esp_err_t csFrameworkInit(csCoreConf_t * conf);


/**
 * \brief Start ConnectSense Wi-Fi framework in Station mode
 *
 * \note To be called during core startup
 *
 * \return ESP_OK Success
 * \returm ESP_FAIL Failed
 *
 */
esp_err_t csFrameworkStart(csCoreStartParams_t * params);


/**
 * \brief A client task will register here to be notified of framework events
 *
 * \param [in] cbFunc Function to be called when a framework event fires
 * \param [in] cbData Opaque data to be passed to the callback function
 *
 * \return ESP_OK On success
 * \return ESP_ERR_INVALID_ARG Null value for cbFunc
 * \return ESP_FAIL Registration table full or cbFunc already registered
 *
 */
esp_err_t csFrameworkCallbackRegister(eventCbFunc_t cbFunc, uint32_t cbData);


/**
 * \brief Remove a previously registered callback from the framework
 *
 * \param [in] cbFunc Function previously registered via \ref csFrameworkCallbackRegister
 *
 * \return ESP_OK on success
 * \return ESP_FAIL function not registered
 * \return ESP_ERR_INVALID_ARG Null value for cbFunc
 *
 */
esp_err_t csFrameworkCallbackUnregister(eventCbFunc_t cbFunc);


/**
 * \brief Read Wi-Fi connection status
 *
 * \return true Connected to Wi-Fi access point and IP address is assigned
 * \return false Not connected, or IP address not assigned
 *
 */
bool csFrameworkIsConnected(void);


/**
 * \brief Return the assigned IP address
 *
 * \return non-zero : IP address
 * \return 0 : Not connected to Wi-Fi or address is not assigned
 *
 */
esp_ip4_addr_t csFrameworkIpAddress(void);

/**
 * \brief
 *
 */
esp_err_t csFrameworkEventHandler(void * ctx, system_event_t * event);


/**
 * \brief
 */
void csFrameworkEventNotify(frmwkEvtCode_t evtCode, void * evtData);


/**
 * \brief Number of seconds since most recent connection to wifi
 */
uint32_t csFrameworkTimeConnected(void);

/**
 * \brief Return a string for the given event code
 */
const char * csFrameworkEventStr(frmwkEvtCode_t evtCode);

/**
 * \brief Return pointer to the host name
 */
const char * csFrameworkHostname(void);

/**
 * \brief Return the handle to the HTTPD
 */
httpd_handle_t csFrameworkHttpdHandle(void);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_CS_CORE_INCLUDE_CS_FRAMEWORK_H_ */
