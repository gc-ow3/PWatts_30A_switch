/*
 * wifi_framework.c
 *
 *  Created on: Jan 7, 2019
 *      Author: wesd
 */

#include "sdkconfig.h"

#include <esp_err.h>
#include <esp_event.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp32/rom/ets_sys.h>
#include <esp_log.h>
#include <string.h>
#include <nvs_flash.h>
#include <esp_netif.h>
#include <mdns.h>

#include "cs_common.h"
#include "cs_heap.h"
#include "mfg_data.h"
#include "param_mgr.h"
#include "cs_control.h"
#include "cs_framework.h"
#include "cs_self_test.h"

#include "cs_prov_support.h"
#include "time_mgr.h"
#include "cs_lr_prov.h"

// Comment out the MOD_NAME line to disable debug prints from this file
#define MOD_NAME	"cs_frmwk"
#include "mod_debug.h"


/*
********************************************************************************
** Data types
********************************************************************************
*/

//! Framework control structure
typedef struct {
	bool			isStarted;
	bool			isConnected;
	bool			isIpAddrAssigned;
	cbHandle_t		cbHandle;
	wifiStaConf_t	staConf;
	esp_ip4_addr_t	ipAddr;
	uint32_t		timeOfConnect;
	esp_netif_t 	*sta_wifi_netif;
	esp_netif_t 	*ap_wifi_netif;
	char			hostname[33];
	TimerHandle_t	provTimer;
	csLrProvConf_t	provConf;
	httpd_handle_t	httpHandle;
} frmwkCtrl_t;


/*
********************************************************************************
** Forward function references
********************************************************************************
*/

static void startProv(frmwkCtrl_t * pCtrl);

static void frmwkNotify(
	frmwkCtrl_t *	pCtrl,
	frmwkEvtCode_t	evtCode,
	void *			evtData
);

static void handleEventWiFi(
	void *				evtArg,
	esp_event_base_t	evtBase,
	int					evtId,
	void *				evtData
);

static void handleEventIp(
		void *				evtArg,
		esp_event_base_t	evtBase,
		int					evtId,
		void *				evtData
);


/*
********************************************************************************
** Constant data
********************************************************************************
*/


/*
********************************************************************************
** Local data
********************************************************************************
*/

// Instantiate the control structure
static frmwkCtrl_t *	frmwkCtrl;


/*
********************************************************************************
********************************************************************************
* Public functions
********************************************************************************
********************************************************************************
*/


esp_err_t csFrameworkInit(csCoreConf_t * conf)
{
	frmwkCtrl_t *	pCtrl = frmwkCtrl;
	if (NULL != pCtrl)
		return ESP_OK;

	if ((pCtrl = cs_heap_calloc(1, sizeof(*pCtrl))) == NULL) {
		return ESP_ERR_NO_MEM;
	}

	esp_err_t	status;

	// Set up callback registration
	status = eventRegisterCreate(&pCtrl->cbHandle);
	if (ESP_OK != status) {
		return status;
	}

	int	len;

	esp_netif_init();

	ESP_ERROR_CHECK(esp_event_loop_create_default());

	// Build the hostname, to be used for Discovery
	len = snprintf(
		pCtrl->hostname, sizeof(pCtrl->hostname),
		"%s-%02X%02X%02X",
		conf->info.model,
		coreMfgData.macAddrBase[3],
		coreMfgData.macAddrBase[4],
		coreMfgData.macAddrBase[5]
	);
	if (len >= sizeof(pCtrl->hostname)) {
		gc_err("hostname too long");
		return ESP_FAIL;
	}

	pCtrl->sta_wifi_netif = esp_netif_create_default_wifi_sta();

	wifi_init_config_t	wifiInit = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&wifiInit));
	ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

	frmwkCtrl = pCtrl;
	return ESP_OK;
}


esp_err_t csFrameworkStart(csCoreStartParams_t * params)
{
	frmwkCtrl_t *	pCtrl = frmwkCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	if (pCtrl->isStarted)
		return ESP_OK;

	// Check for optional parameter structure
	if (params) {
		pCtrl->provConf = params->provConf;
	}

	/* Let's find out if the device is in production line test mode */
	if(csSelfTestIsEnabled()){

		pCtrl->ap_wifi_netif = esp_netif_create_default_wifi_ap();

	    char apSSID[33]={0};
	    if(!coreMfgData.isValid){
			snprintf(
				apSSID, sizeof(apSSID),
				"PW-MFG-%02X%02X%02X", /*conf->info.model,*/
				coreMfgData.macAddrBase[3],
				coreMfgData.macAddrBase[4],
				coreMfgData.macAddrBase[5]
			);
	    }
	    else{
			snprintf(
				apSSID, sizeof(apSSID),
				"%s",
				pCtrl->hostname
			);
	    }

	    wifi_config_t wifi_config = {
	        .ap = {
	            .ssid_len = strlen(apSSID),
	            .authmode = WIFI_AUTH_OPEN,
				.channel        = 0,
				.max_connection = 2,
	        },
	    };
	    strlcpy((char *) wifi_config.ap.ssid, apSSID, sizeof(wifi_config.ap.ssid));
	    wifi_config.ap.ssid_len = strnlen((char *)wifi_config.ap.ssid, sizeof(wifi_config.ap.ssid));

	    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
		esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &handleEventWiFi, pCtrl);
	    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
	    ESP_ERROR_CHECK( esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N) );
	    ESP_ERROR_CHECK(esp_wifi_start());
	}
	else{
		ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

		/* Let's find out if the device is provisioned */
		csProvState_t	provState = csProvStateGet();
		/* If device is not yet provisioned start provisioning service */
		if (csProvState_provisioned == provState) {
			esp_err_t	status=csProvConfLoad(&pCtrl->staConf);

			if(status == ESP_OK){
				// Set Wi-Fi STA mode configuration
				wifi_config_t	wifiConf;
				memset(&wifiConf, 0, sizeof(wifiConf));

				memcpy(wifiConf.sta.ssid, pCtrl->staConf.ssid, pCtrl->staConf.ssidLen);
				ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifiConf));
			}
			else{
				gc_err("WiFi credentials did not load properly, we will factory reset");
				csControlFactoryReset();
			}
		}

		esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &handleEventWiFi, pCtrl);
		esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &handleEventIp, pCtrl);

		ESP_ERROR_CHECK( esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_LR) );
		ESP_ERROR_CHECK(esp_wifi_start());
	}

	gc_dbg("csFrameworkStart finished");
	// The handler for SYSTEM_EVENT_STA_START will do the rest...
	pCtrl->isStarted = true;
	return ESP_OK;
}


esp_err_t csFrameworkCallbackRegister(eventCbFunc_t cbFunc, uint32_t cbData)
{
	frmwkCtrl_t *	pCtrl = frmwkCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	return eventRegisterCallback(pCtrl->cbHandle, cbFunc, cbData);
}


esp_err_t csFrameworkCallbackUnregister(eventCbFunc_t cbFunc)
{
	frmwkCtrl_t *	pCtrl = frmwkCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	return eventUnregisterCallback(pCtrl->cbHandle, cbFunc);
}


bool csFrameworkIsConnected(void)
{
	frmwkCtrl_t *	pCtrl = frmwkCtrl;
	if (NULL == pCtrl)
		return false;

	return (pCtrl->isConnected && pCtrl->isIpAddrAssigned) ? true : false;
}


esp_ip4_addr_t csFrameworkIpAddress(void)
{
	esp_ip4_addr_t	ipZero = {.addr = 0};

	frmwkCtrl_t *	pCtrl = frmwkCtrl;
	if (NULL == pCtrl) {
		return ipZero;
	}

	return (pCtrl->isConnected && pCtrl->isIpAddrAssigned) ? pCtrl->ipAddr : ipZero;
}


void csFrameworkEventNotify(frmwkEvtCode_t evtCode, void * evtData)
{
	frmwkCtrl_t *	pCtrl = frmwkCtrl;
	if (NULL == pCtrl)
		return;

	eventNotify(
		pCtrl->cbHandle,
		callCtx_local,
		(uint32_t)evtCode,
		CS_PTR2ADR(evtData)
	);
}


/**
 * \brief Number of elapsed seconds since most recent connection
 */
uint32_t csFrameworkTimeConnected(void)
{
	return timeMgrGetUptime() - frmwkCtrl->timeOfConnect;
}


const char * csFrameworkEventStr(frmwkEvtCode_t evtCode)
{
	switch (evtCode)
	{
	case frmwkEvtCode_null:
		return "null";
	case frmwkEvtCode_provStart:
		return "provStart";
	case frmwkEvtCode_provDone:
		return "provDone";
	case frmwkEvtCode_provFail:
		return "provFail";
	case frmwkEvtCode_started:
		return "started";
	case frmwkEvtCode_staConnecting:
		return "staConnecting";
	case frmwkEvtCode_staConnected:
		return "staConnected";
	case frmwkEvtCode_staDisconnect:
		return "staDisconnect";
	case frmwkEvtCode_staGotIp:
		return "staGotIp";
	case frmwkEvtCode_staLostIp:
		return "staLostIp";
	default:
		return "Undefined";
	}
}


const char * csFrameworkHostname(void)
{
	frmwkCtrl_t *	pCtrl = frmwkCtrl;
	if (NULL == pCtrl) {
		return "";
	}

	return (const char *)pCtrl->hostname;
}


httpd_handle_t csFrameworkHttpdHandle(void)
{
	frmwkCtrl_t *	pCtrl = frmwkCtrl;
	return pCtrl->httpHandle;
}


/*
********************************************************************************
********************************************************************************
* Private functions
********************************************************************************
********************************************************************************
*/



/**
 * \brief Send event notification to registered clients
 *
 * \param [in] pCtrl Pointer to the framework control structure
 * \param [in] evtCode Framework event code, one of frmwkEvtCode_XXXXX
 * \param [in] evtData (may be NULL) Pointer to frmwkEvtData_t structure holding
 * information (if any) specific to that event
 *
 */
static void frmwkNotify(
	frmwkCtrl_t *	pCtrl,
	frmwkEvtCode_t	evtCode,
	void *			evtData
)
{
	//gc_dbg("Event: %s", csFrameworkEventStr(evtCode));

	eventNotify(
		pCtrl->cbHandle,
		callCtx_local,
		(uint32_t)evtCode,
		CS_PTR2ADR(evtData)
	);
}


static void startProv(frmwkCtrl_t * pCtrl)
{
	if (!pCtrl) {
		gc_err("%s: NULL control structure passed", __FUNCTION__);
		return;
	}

	// Provisioning requires Wi-Fi STA mode
	wifi_mode_t mode;
    esp_wifi_get_mode(&mode);

    if (WIFI_MODE_STA != mode) {
        esp_wifi_set_mode(WIFI_MODE_STA);
    }

	csProvStateSet(csProvState_provisioning);
	gc_dbg("Start LR provisioning");
	csLrProvStart(&pCtrl->provConf);

}

static httpd_handle_t start_webserver(frmwkCtrl_t * pCtrl)
{
	pCtrl->httpHandle= NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    // Start the httpd server
    gc_dbg("Starting server on port: '%d'", config.server_port);
    if (httpd_start(&pCtrl->httpHandle, &config) == ESP_OK) {
        return pCtrl->httpHandle;
    }

    gc_dbg("Error starting server!");
    return NULL;
}

static void handleEventWiFi(
	void *				evtArg,
	esp_event_base_t	evtBase,
	int					evtId,
	void *				evtData
)
{
	frmwkCtrl_t *	pCtrl = (frmwkCtrl_t *)evtArg;

/* WiFi Events*/
    if (evtBase == WIFI_EVENT && evtId == WIFI_EVENT_STA_START) {
		gc_dbg("SYSTEM_EVENT_STA_START");

		if (esp_netif_set_hostname(pCtrl->sta_wifi_netif, pCtrl->hostname) != ESP_OK) {
			gc_dbg("esp_netif_set_hostname(STA, \"%s\") failed", pCtrl->hostname);
		}

		frmwkNotify(pCtrl, frmwkEvtCode_started, NULL);

		csProvState_t	provState = csProvStateGet();

		if (csProvState_provisioned == provState) {
			frmwkNotify(frmwkCtrl, frmwkEvtCode_staConnecting, NULL);
			esp_wifi_connect();
		} else {
			// Start provisioning then notify framework of start
			startProv(pCtrl);
			frmwkNotify(frmwkCtrl, frmwkEvtCode_provStart, NULL);
		}
    } else if (evtBase == WIFI_EVENT && evtId == WIFI_EVENT_AP_START){
		start_webserver(pCtrl);
		frmwkNotify(pCtrl, frmwkEvtCode_sapStarted, NULL);
	} else if (evtBase == WIFI_EVENT && evtId ==  WIFI_EVENT_AP_STACONNECTED){
		gc_dbg("WIFI_EVENT_AP_STACONNECTED");
	} else if (evtBase == WIFI_EVENT && evtId == WIFI_EVENT_STA_CONNECTED) {
		gc_dbg("WIFI_EVENT_STA_CONNECTED");
		pCtrl->isConnected = true;

		frmwkNotify(pCtrl, frmwkEvtCode_staConnected, NULL);
    } else if (evtBase == WIFI_EVENT && evtId == WIFI_EVENT_STA_DISCONNECTED) {
		gc_dbg("WIFI_EVENT_STA_DISCONNECTED");
		frmwkNotify(pCtrl, frmwkEvtCode_staDisconnect, NULL);

		if (csProvStateGet() == csProvState_provisioning) {
			// Maybe was given bad SSID and/or password during provisioning
			//restart provisioning
			frmwkNotify(pCtrl, frmwkEvtCode_provStart, NULL);
		}
		else if(csProvStateGet() == csProvState_unprovisioned){
			//do nothing
			gc_dbg("WIFI_EVENT_STA_DISCONNECTED due to provFail, do nothing");
		}
		else {
			// Maybe AP went down, try reconnection
			frmwkNotify(pCtrl, frmwkEvtCode_staConnecting, NULL);
			esp_wifi_disconnect();
			vTaskDelay(pdMS_TO_TICKS(500));
			esp_wifi_connect();
		}
    } else if (evtBase == WIFI_EVENT && evtId ==  WIFI_EVENT_STA_AUTHMODE_CHANGE){
    	gc_dbg("WIFI_EVENT_STA_AUTHMODE_CHANGE");
    } else if (evtBase == WIFI_EVENT && evtId ==  WIFI_EVENT_SCAN_DONE){
		gc_dbg("WIFI_EVENT_SCAN_DONE");
	}
}


static void handleEventIp(
	void *				evtArg,
	esp_event_base_t	evtBase,
	int					evtId,
	void *				evtData
)
{
	frmwkCtrl_t *	pCtrl = (frmwkCtrl_t *)evtArg;
	frmwkEvtData_t	frmkEvtData;

	/* IP Events*/
	if (evtBase == IP_EVENT && evtId == IP_EVENT_STA_GOT_IP) {

		gc_dbg("IP_EVENT_STA_GOT_IP");

		ip_event_got_ip_t *	gotIp = (ip_event_got_ip_t*)evtData;

		// Record the time of connection
		pCtrl->timeOfConnect = timeMgrGetUptime();

#if MOD_DEBUG
		gc_dbg("  Changed   : %s", CS_BOOL2STR(gotIp->ip_changed));
		gc_dbg("  IP Address: " IPSTR, IP2STR(&gotIp->ip_info.ip));
		gc_dbg("  GW Address: " IPSTR, IP2STR(&gotIp->ip_info.gw));
		gc_dbg("  Net mask  : " IPSTR, IP2STR(&gotIp->ip_info.netmask));
#endif

		// Save it
		pCtrl->ipAddr.addr = gotIp->ip_info.ip.addr;

		// Signal any threads waiting for IP assignment
		frmkEvtData.gotIp.changed = gotIp->ip_changed;
		frmkEvtData.gotIp.ipAddr  = gotIp->ip_info.ip.addr;
		frmkEvtData.gotIp.gwAddr  = gotIp->ip_info.gw.addr;
		frmkEvtData.gotIp.netmask = gotIp->ip_info.netmask.addr;

		pCtrl->isIpAddrAssigned = true;

		frmwkNotify(pCtrl, frmwkEvtCode_staGotIp, (void *)&frmkEvtData);

	} else if (evtBase == IP_EVENT && evtId == IP_EVENT_GOT_IP6) {
		ip_event_got_ip6_t *event = (ip_event_got_ip6_t *)evtData;
		gc_dbg("Connected with IPv6 Address:" IPV6STR, IPV62STR(event->ip6_info.ip));
		(void)event;
	} else if (evtBase == IP_EVENT && evtId == IP_EVENT_AP_STAIPASSIGNED){
		gc_dbg("WIFI_EVENT_AP_STAIPASSIGNED");
	}
}

