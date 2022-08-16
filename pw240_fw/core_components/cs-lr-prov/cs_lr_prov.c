/*
 * cs_lr_prov.c
 *
 *  Created on: June 15, 2022
 *      Author: jonw
 */

#include "sdkconfig.h"

#include "esp32/rom/crc.h"
#include "esp_http_client.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha1.h"
#include "mbedtls/base64.h"
#include "mbedtls/platform.h"
#include "mbedtls/config.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509.h"
#include "mbedtls/error.h"
#include "mbedtls/md.h"
#include "mbedtls/entropy.h"
#include "mbedtls/bignum.h"
#include "app_pw_api.h"

#include "cs_heap.h"
#include "mfg_data.h"
#include "time_mgr.h"
#include "cs_wifi_utils.h"
#include "cs_framework.h"
#include "cs_prov_support.h"
#include "cs_lr_prov.h"
#include "cs_binhex.h"
#include "param_mgr.h"

// Comment out the MOD_NAME line to disable debug prints from this file
#define MOD_NAME	"cs_lr_prov"
#include "mod_debug.h"


#define	MUTEX_CREATE()	xSemaphoreCreateMutex()
#define	MUTEX_GET(x)	xSemaphoreTake((x)->mutex, portMAX_DELAY)
#define	MUTEX_PUT(x)	xSemaphoreGive((x)->mutex)
#define	TASK_CYCLE_TIME_MS 100

////////////////////////////////////////////////////////////////////////////////
// Data types
////////////////////////////////////////////////////////////////////////////////

// Define the SSID blacklist
#define SSID_BLACKLIST_SZ		(10)
#define SSID_BLACKLIST_TIMEOUT_S (60) //1 minute
typedef struct {
	uint8_t		ssid[32];
	int			ssidLen;
	uint32_t	expirationUpTime;
} ssidBlocked_t;

typedef struct {
	csLrProvState_t	state;
	uint32_t			timeMs;
} provEvent_t;

// Define the provisioning event queue
#define PROV_EVT_Q_SZ		(10)
typedef struct {
	int			put;
	int			get;
	int			count;
	provEvent_t	evt[PROV_EVT_Q_SZ];
} provEvtQue_t;


typedef struct {
	SemaphoreHandle_t	mutex;
	wifiStaConf_t wifiData;
	csLrProvConf_t	conf;
	TimerHandle_t		provTimer;
	provEvtQue_t		provEvtQue;
	ssidBlocked_t ssidBlacklist[SSID_BLACKLIST_SZ];
} taskCtrl_t;


////////////////////////////////////////////////////////////////////////////////
// Private function forward references
////////////////////////////////////////////////////////////////////////////////

static void frmwkEventCallback(
	uint32_t	cbData,
	callCtx_t	ctx,
	uint32_t	evtCode,
	uint32_t	evtData
);

//static void timerCallback(TimerHandle_t tmr);

static void provEvtPut(taskCtrl_t * pCtrl, provEvent_t * evt);

static void provEvtGet(taskCtrl_t * pCtrl, provEvent_t * evt);

static void setProvState(taskCtrl_t * pCtrl, csLrProvState_t state);

static void provTask(void * param);

static bool startNetworkScan(taskCtrl_t * pCtrl);

////////////////////////////////////////////////////////////////////////////////
// Constant data
////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////
// Local data
////////////////////////////////////////////////////////////////////////////////

static taskCtrl_t *	taskCtrl;


////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////


esp_err_t csLrProvStart(csLrProvConf_t * conf)
{
	if (!conf) {
		return ESP_ERR_INVALID_ARG;
	}

	taskCtrl_t *	pCtrl = taskCtrl;
	if (pCtrl) {
		return ESP_OK;
	}

	pCtrl = cs_heap_calloc(1, sizeof(*pCtrl));
	if (!pCtrl) {
		return ESP_ERR_NO_MEM;
	}

	// Store configuration
	pCtrl->conf  = *conf;

	pCtrl->mutex = MUTEX_CREATE();

	// Set initial state
	setProvState(pCtrl, csLrProvState_initial);

	csFrameworkCallbackRegister(frmwkEventCallback, CS_PTR2ADR(pCtrl));


	// Shut self down in 15 minutes if provisioning is not done by then
//	pCtrl->provTimer = xTimerCreate(
//		"sapTimer",
//		pdMS_TO_TICKS(15UL * 60UL * 1000UL),
//		pdTRUE,
//		(void *)pCtrl,
//		timerCallback
//	);
//	if (pCtrl->provTimer) {
//		xTimerStart(pCtrl->provTimer, pdMS_TO_TICKS(10));
//	}

	TaskHandle_t	taskHandle;
	UBaseType_t		ret;

	ret = xTaskCreate(
		provTask,
		"provTask",
		8000,
		(void *)pCtrl,
		CS_TASK_PRIO_PROVISION,
		&taskHandle
	);
	if (pdTRUE != ret) {
		gc_err("Failed to start LR provision task");
		cs_heap_free(pCtrl);
		return ESP_FAIL;
	}

	taskCtrl = pCtrl;
	return ESP_OK;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

static void post_to_register(taskCtrl_t * pCtrl)
{
    switch (appPWApiRegister()){
    case 200:
    	setProvState(pCtrl, csLrProvState_provisioned);
    	break;
    case 403:
    	gc_err("HTTP POST has 403 response code, blacklisting this gateway and disconnecting");
    	wifiStaConf_t * wifiData = &pCtrl->wifiData;
    	ssidBlocked_t * blackListEntry = pCtrl->ssidBlacklist;
    	for(int i=0; i<SSID_BLACKLIST_SZ; i++){
    		if(blackListEntry[i].ssid[0]==0 || blackListEntry[i].ssidLen==0){//if ssid is empty, use this slot
    			blackListEntry[i].ssidLen = wifiData->ssidLen;
    			memcpy(blackListEntry[i].ssid, wifiData->ssid, wifiData->ssidLen);
    			gc_dbg("UPTIME %d sec",timeMgrGetUptime());
    			blackListEntry[i].expirationUpTime = timeMgrGetUptime() + SSID_BLACKLIST_TIMEOUT_S;
    			gc_dbg("<LR Provisioning> SSID %s blacklisted until uptime of %d sec", blackListEntry[i].ssid, blackListEntry[i].expirationUpTime);
    			break;
    		}
    		else if(i >= SSID_BLACKLIST_SZ-1){
    	    	gc_err("<LR Provisioning> Couldn't blacklist - out of space");
    		}
    	}
    	setProvState(pCtrl, csLrProvState_provisionFailed);
    	break;
    case 504:
    	gc_err("HTTP Post has 504 response code, disconnecting and trying again later");
    	setProvState(pCtrl, csLrProvState_provisionFailed);
    	break;
    default:
    	setProvState(pCtrl, csLrProvState_provisionFailed);
    	break;
    }

}


static void provTask(void * param)
{
	taskCtrl_t *			pCtrl = param;
	static csLrProvState_t	last_state = csLrProvState_null;

    while (1) {
        vTaskDelay((TASK_CYCLE_TIME_MS) / portTICK_PERIOD_MS);
        provEvent_t provEvent = {0,0};
        provEvtGet(pCtrl, &provEvent);
        if(last_state == provEvent.state)
        	continue;
        last_state= provEvent.state;

        switch(last_state){
        case csLrProvState_unprovisioned:
        	vTaskDelay(5000 / portTICK_PERIOD_MS);
         	setProvState(pCtrl, csLrProvState_scanning);
        	if(startNetworkScan(pCtrl))
        		setProvState(pCtrl, csLrProvState_wifiConnecting);
        	else
        		setProvState(pCtrl, csLrProvState_unprovisioned);
        	break;
        case csLrProvState_scanning:
           	gc_dbg("<LR Provisioning> Scanning");
           	csProvStateSet(csProvState_provisioning);
        	//Do nothing. This is a place holder to wait for the scan results
        	break;
        case csLrProvState_wifiConnecting:
        	csFrameworkEventNotify(frmwkEvtCode_staConnecting, NULL);
        	wifiStaConf_t * wifi = &pCtrl->wifiData;
        	gc_dbg("<LR Provisioning> Connecting to Gateway %s", wifi->ssid);
			// Set Wi-Fi STA mode configuration
			wifi_config_t	wifiConf;
			memset(&wifiConf, 0, sizeof(wifiConf));
			memcpy(wifiConf.sta.ssid, wifi->ssid, wifi->ssidLen);
			esp_wifi_set_config(WIFI_IF_STA, &wifiConf);
			//connect
			esp_wifi_connect();
        	break;
        case csLrProvState_wifiConnected:
        	gc_dbg("<LR Provisioning> Connected to Gateway");
        	setProvState(pCtrl, csLrProvState_provisioning);
        	post_to_register(pCtrl);
        	break;
        case csLrProvState_provisioning:
        	gc_dbg("<LR Provisioning> Registering to Gateway");
        	//do nothing, we are waiting on HTTP post response
        	break;
        case csLrProvState_provisioned:
        	gc_dbg("<LR Provisioning> Successfully Registered to Gateway");
			csProvStateSet(csProvState_provisioned);
    		csFrameworkEventNotify(frmwkEvtCode_provDone, NULL);
    		goto exitMem;
        	break;
        case csLrProvState_provisionFailed:
        	gc_dbg("<LR Provisioning> Failed to Register to Gateway");
        	csProvStateSet(csProvState_unprovisioned);
    		esp_wifi_disconnect();
    		csFrameworkEventNotify(frmwkEvtCode_provFail, NULL);
    		vTaskDelay(1000 / portTICK_PERIOD_MS);
    		csFrameworkEventNotify(frmwkEvtCode_provStart, NULL);
        default:
        	break;
        }
    }
exitMem:
	vSemaphoreDelete(pCtrl->mutex);
    cs_heap_free(pCtrl);
    csFrameworkCallbackUnregister(frmwkEventCallback);
	taskCtrl = NULL;
	vTaskDelete(NULL);
}

static void frmwkEventCallback(
	uint32_t	cbData,
	callCtx_t	ctx,
	uint32_t	evtCode,
	uint32_t	evtData
)
{
	taskCtrl_t *			pCtrl = CS_ADR2PTR(cbData);

	switch ((frmwkEvtCode_t)evtCode)
	{
	case frmwkEvtCode_provStart:
		setProvState(pCtrl, csLrProvState_unprovisioned);
		break;
	case frmwkEvtCode_staGotIp:
		setProvState(pCtrl, csLrProvState_wifiConnected);
		break;
	default:
		break;
	}
}

//static void timerCallback(TimerHandle_t tmr)
//{
//	taskCtrl_t *	pCtrl = (taskCtrl_t *)pvTimerGetTimerID(tmr);

    // Release allocated resources
//	xTimerDelete(pCtrl->provTimer, 10);
//	vSemaphoreDelete(pCtrl->mutex);
//	cs_heap_free(pCtrl);
//	taskCtrl = NULL;
//}


static void provEvtPut(taskCtrl_t * pCtrl, provEvent_t * evt)
{
	provEvtQue_t *	pq = &pCtrl->provEvtQue;

	MUTEX_GET(pCtrl);

	pq->evt[pq->put] = *evt;

	if (pq->count < PROV_EVT_Q_SZ) {
		if (++(pq->put) == PROV_EVT_Q_SZ) {
			pq->put = 0;
		}

		pq->count += 1;
	}

	MUTEX_PUT(pCtrl);
}


static void provEvtGet(taskCtrl_t * pCtrl, provEvent_t * evt)
{
	provEvtQue_t *	pq = &pCtrl->provEvtQue;

	MUTEX_GET(pCtrl);

	*evt = pq->evt[pq->get];

	if (pq->count > 1) {
		if (++(pq->get) == PROV_EVT_Q_SZ) {
			pq->get = 0;
		}

		pq->count -= 1;
	}

	MUTEX_PUT(pCtrl);
}

static void setProvState(taskCtrl_t * pCtrl, csLrProvState_t state)
{
	static uint64_t				baseMs   = 0;
	static csLrProvState_t	prvState = csLrProvState_null;

	if (prvState == state)
		return;
	prvState = state;

	uint64_t	currMs = timeMgrGetUptimeMs();

	provEvent_t	evt = {
		.state = state
	};

	if (csLrProvState_initial == state) {
		baseMs     = currMs;
		evt.timeMs = 0;
	} else if (csLrProvState_unprovisioned == state) {
		baseMs     = currMs;
		evt.timeMs = 0;
	} else {
		evt.timeMs = (uint32_t)(currMs - baseMs);
	}

	provEvtPut(pCtrl, &evt);
}



static bool startNetworkScan(taskCtrl_t * pCtrl)
{
	bool connecting = false;
	csLrProvConf_t *	conf  = &pCtrl->conf;
	gc_dbg("Start Wi-Fi Scan");


	// Run scan for Wi-Fi APs
	wifi_ap_record_t *	apList;
	uint16_t			apCount;

	if (csWifiGetApList(&apList, &apCount)) {
		return connecting;
	}

	csWifiSortApList(apList, &apCount, conf->ssidFilter);

	if(apCount > 0){

		wifi_ap_record_t *	pSlot;

		wifiStaConf_t * wifi = &pCtrl->wifiData;
		memset(wifi, 0, sizeof(*wifi));

		pSlot = apList;
		for (int i = 0; i < apCount; i++, pSlot++) {

			//check ssid against blacklist
			bool skip_ssid = false;
	    	ssidBlocked_t * blackListEntry = pCtrl->ssidBlacklist;
	    	for(int j=0; j<SSID_BLACKLIST_SZ; j++){
	    		if(blackListEntry[j].expirationUpTime <= timeMgrGetUptime()){
	    			memset(&blackListEntry[j], 0, sizeof(ssidBlocked_t));
	    		}
	    		if((blackListEntry[j].ssidLen == strlen((char *)pSlot->ssid)) && memcmp(blackListEntry[j].ssid, pSlot->ssid, strlen((char *)pSlot->ssid))==0){//if ssid is matches
	    			gc_dbg("<LR Provisioning> Skipping blacklisted SSID %s for %d more seconds", blackListEntry[j].ssid, (blackListEntry[j].expirationUpTime-timeMgrGetUptime()));
	    			skip_ssid = true;
	    			break;
	    		}
	    	}
	    	if(skip_ssid){
	    		continue;
	    	}

			wifi->ssidLen = strlen((char *)pSlot->ssid);
			if (wifi->ssidLen > sizeof(wifi->ssid)) {
				wifi->ssidLen = 0;
				continue;
			}
			memcpy(wifi->ssid, pSlot->ssid, wifi->ssidLen);
			wifi->ssidIsSet = true;
			break;
		}

		if(wifi->ssidIsSet){
			// Store the configuration
			csProvConfStore(wifi);
			// Initiate Wi-Fi connection
			connecting=true;
		}
	}

	csWifiReleaseApList(apList);
	return connecting;
}

