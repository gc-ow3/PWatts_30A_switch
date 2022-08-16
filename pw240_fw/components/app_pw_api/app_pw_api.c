/*
 * app_local_api.c
 *
 *  Created on: Oct 14, 2019
 *      Author: wesd
 */

#include "sdkconfig.h"

#include <ctype.h>
#include <esp_wifi.h>
#include <esp_http_client.h>
#include <esp_http_server.h>
#include "mbedtls/base64.h"
#include "cs_platform.h"
#include "cs_rpc_proc.h"
#include "cJSON.h"
#include "cs_json_utils.h"
#include "cs_control.h"
#include "outlet_mgr.h"
#include "app_led_mgr.h"
#include "time_mgr.h"
#include "app_params.h"
#include "app_pw_api.h"
#include "mfg_data.h"
#include "emtr_drv.h"
#include "fw_update.h"

// Comment out the MOD_NAME line to disable debug prints from this file
#define MOD_NAME	"app_pw_api"
#include "mod_debug.h"

#define POLL_FREQUENCY_MINUTES (15)

extern const uint8_t server_root_cert_pem_start[] asm("_binary_server_root_cert_pem_start");
extern const uint8_t server_root_cert_pem_end[]   asm("_binary_server_root_cert_pem_end");


typedef enum {
	appPWApiMsgCode_wakeUp,
	appPWApiMsgCode_sendEvent,
	appPWApiMsgCode_shutDown,
	appPWApiMsgCode_pause,
	appPWApiMsgCode_resume,
} appPWApiMsgCode_t;

typedef struct {
	char * server_root_cert_pem;
	char * client_cert_pem;
	char * client_key_pem;
} pw_certs_t;

typedef struct {
	bool	isStarted;
	bool shutdown;
	bool pause;
	TaskHandle_t		apiTask;
	QueueHandle_t		queue;
	QueueHandle_t		eventQueue;
	httpd_handle_t httpServer;
	esp_http_client_handle_t client;
	esp_http_client_config_t clientConfig;
	pw_certs_t certs;
} control_t;
static control_t *	control;

static char local_response_buffer[2048] = {'n','a',0};

esp_err_t _http_event_handler(esp_http_client_event_t *evt);
static void pwApiTask(void * arg);
static esp_err_t wakeup_post_handler(httpd_req_t * req);
static void http_post_to_status(control_t * pCtrl);
static void sysEventCb(
	uint32_t	cbData,
	callCtx_t	ctx,
	uint32_t	evtCode,
	uint32_t	evtData
);
static void emtrSocketEvtCb(
	uint32_t	cbData,
	callCtx_t	ctx,
	uint32_t	evtCode,
	uint32_t	evtData
);

static httpd_uri_t wake_up = {
    .uri       = "/api/v1/wakeup",
    .method    = HTTP_POST,
    .handler   = wakeup_post_handler,
};

esp_err_t appPWApiInit(void)
{

	control_t *	pCtrl = control;
	if (NULL != pCtrl) {
		return ESP_OK;
	}

	pCtrl = cs_heap_calloc(1, sizeof(*pCtrl));
	if (NULL == pCtrl) {
		return ESP_ERR_NO_MEM;
	}

	/*set TLS keys and certs*/

	//server root cert
	if(server_root_cert_pem_end-server_root_cert_pem_start <= 0){
		return ESP_ERR_NO_MEM;
	}
	pCtrl->certs.server_root_cert_pem = (char *)cs_heap_calloc(1, (1+server_root_cert_pem_end-server_root_cert_pem_start));
	if (NULL == pCtrl->certs.server_root_cert_pem) {
		return ESP_ERR_NO_MEM;
	}
	memcpy(pCtrl->certs.server_root_cert_pem, server_root_cert_pem_start, (server_root_cert_pem_end-server_root_cert_pem_start));

	//client cert
	size_t cert_size = 0;
	mbedtls_base64_decode( NULL, 0, &cert_size, (uint8_t *)coreMfgData.tlsCertB64, strlen(coreMfgData.tlsCertB64));
	if(cert_size <= 0){
		return ESP_ERR_NO_MEM;
	}
	pCtrl->certs.client_cert_pem = (char *)cs_heap_calloc(1, (1+cert_size));
	if (NULL == pCtrl->certs.client_cert_pem) {
		return ESP_ERR_NO_MEM;
	}
	char* certchain_unswapped = (char *)cs_heap_calloc(1, (1+cert_size));
	if (NULL == certchain_unswapped) {
		return ESP_ERR_NO_MEM;
	}
	mbedtls_base64_decode( (uint8_t *)certchain_unswapped, (1+cert_size), &cert_size, (uint8_t *)coreMfgData.tlsCertB64, strlen(coreMfgData.tlsCertB64));
	char* child_cert = strstr(&certchain_unswapped[strlen("-----BEGIN CERTIFICATE-----")], "-----BEGIN CERTIFICATE-----");
	if (!child_cert){
		return ESP_ERR_NO_MEM;
	}
	sprintf(pCtrl->certs.client_cert_pem,child_cert);
	sprintf(&pCtrl->certs.client_cert_pem[strlen(pCtrl->certs.client_cert_pem)],"\n");
	snprintf(&pCtrl->certs.client_cert_pem[strlen(pCtrl->certs.client_cert_pem)], child_cert-certchain_unswapped, certchain_unswapped);
	cs_heap_free(certchain_unswapped);


	//client key
	size_t key_size = 0;
	mbedtls_base64_decode( NULL, 0, &key_size, (uint8_t *)coreMfgData.tlsKeyB64, strlen(coreMfgData.tlsKeyB64));
	if(key_size <= 0){
		return ESP_ERR_NO_MEM;
	}
	pCtrl->certs.client_key_pem = (char *)cs_heap_calloc(1, (1+key_size));
	if (NULL == pCtrl->certs.client_key_pem) {
		return ESP_ERR_NO_MEM;
	}
	mbedtls_base64_decode( (uint8_t *)pCtrl->certs.client_key_pem, (1+key_size), &key_size, (uint8_t *)coreMfgData.tlsKeyB64, strlen(coreMfgData.tlsKeyB64));

	gc_dbg("start->%s<-end", pCtrl->certs.client_cert_pem);
	gc_dbg("start->%s<-end", pCtrl->certs.client_key_pem);
	gc_dbg("rootstart->%s<-end", pCtrl->certs.server_root_cert_pem);

	if ((pCtrl->queue = xQueueCreate(16, sizeof(cJSON *))) == NULL) {
		return ESP_ERR_NO_MEM;
	}

	if ((pCtrl->eventQueue = xQueueCreate(16, sizeof(cJSON *))) == NULL) {
		return ESP_ERR_NO_MEM;
	}

	pCtrl->shutdown=false;
	pCtrl->pause=false;

    // Init the http client
	pCtrl->clientConfig.url = "https://192.168.0.1/api/v1/register";
	pCtrl->clientConfig.transport_type = HTTP_TRANSPORT_OVER_SSL;
	pCtrl->clientConfig.use_global_ca_store = false;
	pCtrl->clientConfig.client_cert_pem = pCtrl->certs.client_cert_pem;
	pCtrl->clientConfig.client_key_pem = pCtrl->certs.client_key_pem;
	pCtrl->clientConfig.timeout_ms = 20000;
	pCtrl->clientConfig.cert_pem = pCtrl->certs.server_root_cert_pem;
	pCtrl->clientConfig.skip_cert_common_name_check=true;
	pCtrl->clientConfig.user_data = local_response_buffer;
	pCtrl->clientConfig.event_handler = _http_event_handler;

	control = pCtrl;
	return ESP_OK;
}

//we need to free all our queued items
void resetQueue(control_t *	pCtrl){
	cJSON *queue_json = NULL;
	while(xQueueReceive(pCtrl->queue, &queue_json, 0)){
		cJSON_Delete(queue_json);
	}
	xQueueReset(pCtrl->queue);
}


esp_err_t appPWApiStart(void)
{

	control_t *	pCtrl = control;
	if (NULL == pCtrl) {
		return ESP_FAIL;
	}
	vTaskDelay(pdMS_TO_TICKS(100));
	if (pCtrl->isStarted) {
		return ESP_OK;
	}
	if(pCtrl->shutdown){
		gc_err("Race condition where local API is trying to start while it is shutting down, reboot to recover");
		csControlReboot(200, csRebootReason_recovery);
		return ESP_FAIL;
	}

	pCtrl->pause=false;

	BaseType_t			xStatus;
	// Start the PW api
	xStatus = xTaskCreate(
		pwApiTask,
		"pw_api",
		16384,
		(void *)pCtrl,
		TASK_PRIO_LOCAL_API,
		&pCtrl->apiTask
	);
	if (pdPASS != xStatus) {
		gc_err("Failed to start PW API server");
		return ESP_FAIL;
	}

    httpd_config_t config = {                        \
            .task_priority      = tskIDLE_PRIORITY+5,       \
            .stack_size         = 8192,                     \
            .server_port        = 80,                       \
            .ctrl_port          = 32768,                    \
            .max_open_sockets   = 1,                        \
            .max_uri_handlers   = 12,                       \
            .max_resp_headers   = 2,                        \
            .backlog_conn       = 5,                        \
            .lru_purge_enable   = false,                    \
            .recv_wait_timeout  = 5,                        \
            .send_wait_timeout  = 5,                        \
    };

    //register system event callback
	if (csControlCallbackRegister(sysEventCb, CS_PTR2ADR(pCtrl)) != ESP_OK){
    	vTaskDelete(pCtrl->apiTask);
    	gc_dbg("Error registering control callback!");
		return ESP_FAIL;
	}

	//register sockets callback
	if (emtrDrvCallbackRegister(emtrCbId_socket1, emtrSocketEvtCb, CS_PTR2ADR(pCtrl)) != ESP_OK ){
		csControlCallbackUnregister(sysEventCb);
		return ESP_FAIL;
	}
	if(emtrDrvCallbackRegister(emtrCbId_socket2, emtrSocketEvtCb, CS_PTR2ADR(pCtrl)) != ESP_OK ){
		csControlCallbackUnregister(sysEventCb);
		emtrDrvCallbackUnregister(emtrCbId_socket1, emtrSocketEvtCb);
		return ESP_FAIL;
	}

    // Start the httpd server
    gc_dbg("Starting server on port: '%d'", config.server_port);
    if (httpd_start(&pCtrl->httpServer, &config) == ESP_OK) {
        // Set URI handlers
    	gc_dbg("Registering URI handlers");
        httpd_register_uri_handler(pCtrl->httpServer, &wake_up);
    }
    else{
    	vTaskDelete(pCtrl->apiTask);
    	gc_dbg("Error starting HTTP server!");
    	return ESP_FAIL;
    }

	pCtrl->isStarted = true;
	return ESP_OK;
}

esp_err_t appPWApiStop(void)
{

	control_t *	pCtrl = control;
	if (NULL == pCtrl) {
		return ESP_FAIL;
	}
	if (!pCtrl->isStarted || pCtrl->shutdown) {
		return ESP_OK;
	}

	char MsgCode_buf[100]={0};
	sprintf(MsgCode_buf, "{\"appPWApiMsgCode\":%d}",appPWApiMsgCode_shutDown);
	cJSON * MsgCode_json = NULL;
	MsgCode_json = cJSON_Parse(MsgCode_buf);
	resetQueue(pCtrl);
	if(xQueueSend(pCtrl->queue, &MsgCode_json, pdMS_TO_TICKS(10)) != pdPASS){
		//failed to send message code to queue
		gc_err("Failed to send message code to queue");
		cJSON_Delete(MsgCode_json);
	}


	return ESP_OK;
}

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    static int output_len;       // Stores number of bytes read
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            break;
        case HTTP_EVENT_ON_CONNECTED:
            break;
        case HTTP_EVENT_HEADER_SENT:
            break;
        case HTTP_EVENT_ON_HEADER:
            break;
        case HTTP_EVENT_ON_DATA:
			if (evt->user_data) {
				if(output_len + evt->data_len <= 2048-1)
					memcpy(evt->user_data + output_len, evt->data, evt->data_len);
			}
			output_len += evt->data_len;
            break;
        case HTTP_EVENT_ON_FINISH:
			if (evt->user_data) {
				memset(evt->user_data + output_len, 0, 1);
			}
			output_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED:
            {
				if (evt->user_data) {
					memset(evt->user_data + output_len, 0, 1);
				}
            }
            output_len = 0;
            break;
    }
    return ESP_OK;
}

static void http_post_to_event(control_t *	pCtrl, cJSON * event_json){
	if (NULL == pCtrl) {
		return;
	}

    char target_url[62];
    char mac_str[18];
    sprintf(mac_str,"%02x:%02x:%02x:%02x:%02x:%02x",
    		coreMfgData.macAddrBase[0],
    		coreMfgData.macAddrBase[1],
    		coreMfgData.macAddrBase[2],
    		coreMfgData.macAddrBase[3],
    		coreMfgData.macAddrBase[4],
    		coreMfgData.macAddrBase[5]);
    sprintf(target_url, "https://192.168.0.1/api/v1/%s/event/%s",
    		csCoreConf.info.model,
			mac_str);

    // POST
    char * post_data = NULL;
    post_data = cJSON_PrintUnformatted(event_json);

    if(post_data != NULL){
    	esp_http_client_set_url(pCtrl->client, target_url);
		gc_dbg("HTTP POST \"%s\" to \"%s\"",post_data, target_url);
		esp_http_client_set_method(pCtrl->client, HTTP_METHOD_POST);
		esp_http_client_set_header(pCtrl->client, "Content-Type", "application/json");
		esp_http_client_set_post_field(pCtrl->client, post_data, strlen(post_data));

		esp_err_t err = esp_http_client_perform(pCtrl->client);
		if (err == ESP_OK) {
			gc_dbg("HTTP POST Status: %d", esp_http_client_get_status_code(pCtrl->client));
		} else {
			gc_err("HTTP POST request failed: %s", esp_err_to_name(err));
		}

		cJSON_free(post_data);
    }
}

static void http_post_to_status(control_t * pCtrl)
{
    char target_url[62];
    char mac_str[18];
    sprintf(mac_str,"%02x:%02x:%02x:%02x:%02x:%02x",
    		coreMfgData.macAddrBase[0],
    		coreMfgData.macAddrBase[1],
    		coreMfgData.macAddrBase[2],
    		coreMfgData.macAddrBase[3],
    		coreMfgData.macAddrBase[4],
    		coreMfgData.macAddrBase[5]);
    sprintf(target_url, "https://192.168.0.1/api/v1/%s/status/%s",
    		csCoreConf.info.model,
			mac_str);

    esp_http_client_set_url(pCtrl->client, target_url);

    // POST
    char post_data[2048] = {0};

    wifi_ap_record_t wifidata;
    sprintf(post_data, "{\"fw\":\"%s\",\"hw\":\"%s\",\"rssi\":%d",
    		csCoreConf.info.fwVersion,
			coreMfgData.hwVersion,
			esp_wifi_sta_get_ap_info(&wifidata)==0?wifidata.rssi:-1000);

	emtrDeviceStatus_t	emtr;
	if (emtrDrvGetDeviceStatus(&emtr) == ESP_OK) {
		// EMTR board temperature
		sprintf(post_data+strlen(post_data), ",\"temp\":%d", emtr.temperature);
	} else {
		gc_err("emtrDrvGetDeviceState() failed");
	}

	// Add socket JSON object array
	sprintf(post_data+strlen(post_data), ",\"socket\":[");
	int					sIdx;
	emtrSocketStatus_t	sock;
	for (sIdx = 0; sIdx < NUM_SOCKETS; sIdx++) {
		int		sNum = 1 + sIdx;
		if (emtrDrvGetSocketStatus(sNum, &sock) != ESP_OK) {
			gc_err("Failed to read status for socket %d", sNum);
			continue;
		}
		// Shorthand reference to instant energy values
		emtrInstEnergy_t *	ie = &sock.instEnergy;
		// start of socket object
		if(sIdx>0)
			sprintf(post_data+strlen(post_data), ",");
		sprintf(post_data+strlen(post_data), "{");
		// Socket Index
		sprintf(post_data+strlen(post_data), "\"socketIdx\":%d", sIdx);
		// Position
		sprintf(post_data+strlen(post_data), ",\"position\":%d", sock.isOn?1:0);
		// Occupied
		sprintf(post_data+strlen(post_data), ",\"occupied\":%s", sock.isPlugged?"true":"false");
		// State time
		sprintf(post_data+strlen(post_data), ",\"stateTime\":%d", sock.relayTime);
		// Volts
		sprintf(post_data+strlen(post_data), ",\"volts\":%.1f", (float)ie->dVolts/10.0);
		// Amps
		sprintf(post_data+strlen(post_data), ",\"amps\":%.3f", (float)ie->mAmps/1000.0);
		// Watts
		sprintf(post_data+strlen(post_data), ",\"watts\":%.1f", (float)ie->dWatts/10.0);
		// Watt-Hours
		sprintf(post_data+strlen(post_data), ",\"wattHours\":%lld", sock.dWattHours);
		// Power factor
		sprintf(post_data+strlen(post_data), ",\"powerFactor\":%d", ie->powerFactor);
		// end of socket object
		sprintf(post_data+strlen(post_data), "}");
	}
	sprintf(post_data+strlen(post_data), "]");
    sprintf(post_data+strlen(post_data), "}");

    gc_dbg("HTTP POST \"%s\" to \"%s\"",post_data, target_url);
    esp_http_client_set_method(pCtrl->client, HTTP_METHOD_POST);
    esp_http_client_set_header(pCtrl->client, "Content-Type", "application/json");
    esp_http_client_set_post_field(pCtrl->client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(pCtrl->client);
    if (err == ESP_OK) {
    	gc_dbg("HTTP POST Status: %d, content: = %s",
                esp_http_client_get_status_code(pCtrl->client), local_response_buffer);
    } else {
    	gc_err("HTTP POST request failed: %s", esp_err_to_name(err));
        return;
    }
    cJSON * queue_json = NULL;
    switch (esp_http_client_get_status_code(pCtrl->client)){
    case 200:
    	queue_json = cJSON_Parse(local_response_buffer);
        if (queue_json == NULL)
        {
            const char *error_ptr = cJSON_GetErrorPtr();
            if (error_ptr != NULL)
            {
            	gc_err("HTTP post JSON Error before: %s\n", error_ptr);
            }
            cJSON_Delete(queue_json);
        }
        else{
    		if(xQueueSend(pCtrl->queue, &queue_json, pdMS_TO_TICKS(10)) != pdPASS){
    			//failed to send settigs json to queue
    			gc_err("Failed to send settings json to queue");
    			cJSON_Delete(queue_json);
    		}
        }
    	break;
    default:
    	break;
    }
}

int appPWApiRegister()
{
    int status=0;

	control_t *	pCtrl = control;
	if (NULL == pCtrl) {
		return status;
	}

    static char mac_str[18];
    sprintf(mac_str,"%02x:%02x:%02x:%02x:%02x:%02x",
    		coreMfgData.macAddrBase[0],
    		coreMfgData.macAddrBase[1],
    		coreMfgData.macAddrBase[2],
    		coreMfgData.macAddrBase[3],
    		coreMfgData.macAddrBase[4],
    		coreMfgData.macAddrBase[5]);

    // Init the http client
    pCtrl->client = esp_http_client_init(&pCtrl->clientConfig);

    // POST
    char post_data[50] = {0};
    sprintf(post_data, "{\"mac\":\"%s\",\"type\":\"%s\"}",
    		mac_str,
			csCoreConf.info.model);
    gc_dbg("HTTP POST \"%s\" to \"%s\"",post_data, "https://192.168.0.1/api/v1/register");
    esp_http_client_set_method(pCtrl->client, HTTP_METHOD_POST);
    esp_http_client_set_url(pCtrl->client, "https://192.168.0.1/api/v1/register");
    esp_http_client_set_header(pCtrl->client, "Content-Type", "application/json");
    esp_http_client_set_post_field(pCtrl->client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(pCtrl->client);

    if (err == ESP_OK) {
    	gc_dbg("HTTP POST Status: %d", esp_http_client_get_status_code(pCtrl->client));
    	status = esp_http_client_get_status_code(pCtrl->client);
    	esp_http_client_cleanup(pCtrl->client);
    	return status;
    } else {
    	gc_err("HTTP POST request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(pCtrl->client);
        return status;
    }
}

esp_err_t appPWApiOTA()
{
	control_t *	pCtrl = control;
	if (NULL == pCtrl) {
		return ESP_FAIL;
	}

    char target_url[62];
    char mac_str[18];
    sprintf(mac_str,"%02x:%02x:%02x:%02x:%02x:%02x",
    		coreMfgData.macAddrBase[0],
    		coreMfgData.macAddrBase[1],
    		coreMfgData.macAddrBase[2],
    		coreMfgData.macAddrBase[3],
    		coreMfgData.macAddrBase[4],
    		coreMfgData.macAddrBase[5]);
    sprintf(target_url, "https://192.168.0.1/api/v1/%s/ota/%s",
    		csCoreConf.info.model,
			mac_str);

    esp_http_client_config_t config = {
        .url = target_url,
		.transport_type = HTTP_TRANSPORT_OVER_SSL,
		.use_global_ca_store = false,
		.client_cert_pem = pCtrl->certs.client_cert_pem,
		.client_key_pem = pCtrl->certs.client_key_pem,
		.timeout_ms = 20000,
		.cert_pem = pCtrl->certs.server_root_cert_pem,
		.skip_cert_common_name_check=true
    };

	return csFwUpdate(&config);
}

static void sendEvent(const char *  evtName, const char *   evtData_json){
	control_t *     pCtrl = control;
	if (NULL == pCtrl) {
		return;
	}
	if(evtName == NULL)
		return;
	if(strlen(evtName) <= 0)
		return;

	char MsgCode_buf[140]={0};//reusable json buffer
	cJSON * EventQueue_json = NULL;

	if(evtData_json != NULL && strlen(evtData_json)>0){
		sprintf(MsgCode_buf, "{\"event\":\"%s\",\"data\":%s}", evtName, evtData_json);
	} else {
		sprintf(MsgCode_buf, "{\"event\":\"%s\"}", evtName);
	}
	EventQueue_json = cJSON_Parse(MsgCode_buf);
	if(xQueueSend(pCtrl->eventQueue, &EventQueue_json, pdMS_TO_TICKS(10)) != pdPASS){
		//failed to send event to queue
		gc_err("Failed to send event to queue");
		cJSON_Delete(EventQueue_json);
		return;
	}
	//inform primary task there is an events to report
	cJSON * EventMsgCode_json = NULL;
	sprintf(MsgCode_buf, "{\"appPWApiMsgCode\":%d}",appPWApiMsgCode_sendEvent);
	EventMsgCode_json = cJSON_Parse(MsgCode_buf);

	if(xQueueSend(pCtrl->queue, &EventMsgCode_json, pdMS_TO_TICKS(10)) != pdPASS){
		//failed to send message code to queue
		gc_err("Failed to send message code to queue");
		cJSON_Delete(EventMsgCode_json);
		return;
	}
}

//returns true if it is a message code and false if it is not
static bool parseMsgCode(cJSON * api_json){

	control_t *	pCtrl = control;
	if (NULL == pCtrl) {
		return true;
	}
	if (!pCtrl->isStarted) {
		return true;
	}


	cJSON *	jItem = NULL;
	cJSON *event_json = NULL;
	int	code;
	if ((jItem = cJSON_GetObjectItem(api_json, "appPWApiMsgCode")) != NULL) {
		code = jItem->valueint;
		switch(code){
		case appPWApiMsgCode_sendEvent:
			while(xQueueReceive(pCtrl->eventQueue, &event_json, 0)) {
				http_post_to_event(pCtrl, event_json);
				cJSON_Delete(event_json);
			}
			return true;
		case appPWApiMsgCode_wakeUp:
			if(!pCtrl->pause)
				http_post_to_status(pCtrl);
			return true;
		case appPWApiMsgCode_shutDown:
			pCtrl->isStarted = false;
			pCtrl->shutdown=true;
			pCtrl->pause=true;
			return true;
		case appPWApiMsgCode_pause:
			gc_dbg("Pause PW API");
			pCtrl->pause=true;
			return true;
		case appPWApiMsgCode_resume:
			gc_dbg("Resume PW API");
			pCtrl->pause=false;
			http_post_to_status(pCtrl);
			return true;
		default:
			return true;
		}
	}
	return false;
}

static void parseApiJSON(cJSON * api_json){

	cJSON *	jItem = NULL;
	int	outletA;
	if ((jItem = cJSON_GetObjectItem(api_json, "position0")) != NULL) {
		outletA = jItem->valueint;
		if(outletA > 0){
			gc_dbg("Socket A - Relay on now!");
			esp_err_t	status= outletMgrSetSocket(
				1,//Socket A
				true,
				callCtx_local,
				appDeltaSrc_internal
			);
			if (ESP_OK != status) {
				gc_err("failed to set socket state");
			}
		}else{
			gc_dbg("Socket A - Relay off now!");
			esp_err_t	status= outletMgrSetSocket(
				1,//Socket A
				false,
				callCtx_local,
				appDeltaSrc_internal
			);
			if (ESP_OK != status) {
				gc_err("failed to set socket state");
			}
		}
	}

	int	outletB;
	if ((jItem = cJSON_GetObjectItem(api_json, "position1")) != NULL) {
		outletB = jItem->valueint;
		if(outletB > 0){
			gc_dbg("Socket B - Relay on now!");
			esp_err_t	status= outletMgrSetSocket(
				2,//Socket B
				true,
				callCtx_local,
				appDeltaSrc_internal
			);
			if (ESP_OK != status) {
				gc_err("failed to set socket state");
			}
		}else{
			gc_dbg("Socket B - Relay off now!");
			esp_err_t	status= outletMgrSetSocket(
				2,//Socket B
				false,
				callCtx_local,
				appDeltaSrc_internal
			);
			if (ESP_OK != status) {
				gc_err("failed to set socket state");
			}
		}
	}

	// Start Mutually exclusive actions
	// priority goes:
	//   1-ota
	//   2-factoryReset
	//   3-reboot
	int	OTA;
	if ((jItem = cJSON_GetObjectItem(api_json, "ota")) != NULL) {
		OTA = jItem->valueint;
		if(OTA>0){
			csControlSignal(csCtrlSignal_fwUpgrade);
			return;
		}
	}

	int	factoryReset;
	if ((jItem = cJSON_GetObjectItem(api_json, "factoryReset")) != NULL) {
		factoryReset = jItem->valueint;
		if(factoryReset>0){
			csControlSignal(csCtrlSignal_resetFactory);
			return;
		}
	}

	int	reboot;
	if ((jItem = cJSON_GetObjectItem(api_json, "reboot")) != NULL) {
		reboot = jItem->valueint;
		if(reboot>0){
			csControlReboot(300, csRebootReason_command);
			return;
		}
	}
}

static esp_err_t wakeUp(){
	control_t *	pCtrl = control;
	if (NULL == pCtrl) {
		return ESP_FAIL;
	}
	if(!pCtrl->pause){
		char MsgCode_buf[100]={0};
		sprintf(MsgCode_buf, "{\"appPWApiMsgCode\":%d}",appPWApiMsgCode_wakeUp);
		cJSON * MsgCode_json = NULL;
		MsgCode_json = cJSON_Parse(MsgCode_buf);

		if(xQueueSend(pCtrl->queue, &MsgCode_json, pdMS_TO_TICKS(10)) != pdPASS){
			//failed to send message code to queue
			gc_err("Failed to send message code to queue");
			cJSON_Delete(MsgCode_json);
			return ESP_FAIL;
		}
	}
	return ESP_OK;
}

static esp_err_t wakeup_post_handler(httpd_req_t * req)
{

	httpd_resp_set_status(req, HTTPD_200);
	httpd_resp_send(req, NULL, 0);
	return wakeUp();
}

static void pwApiTask(void * arg)
{
	control_t *	pCtrl = (control_t *)arg;

	gc_dbg("PW API started");
	//first poll immediately
	pCtrl->client = esp_http_client_init(&pCtrl->clientConfig);
	http_post_to_status(pCtrl);
	esp_http_client_cleanup(pCtrl->client);

	while (1)
	{
		cJSON *queue_json = NULL;
		BaseType_t isReceived;
		isReceived = xQueueReceive(pCtrl->queue, &queue_json, (POLL_FREQUENCY_MINUTES * 60 * 1000)/portTICK_PERIOD_MS);
    	pCtrl->client = esp_http_client_init(&pCtrl->clientConfig);
		do{
			if (isReceived == pdTRUE) {
				if(!parseMsgCode(queue_json))
					parseApiJSON(queue_json);
				cJSON_Delete(queue_json);
				vTaskDelay(pdMS_TO_TICKS(50));//add delay so multiple https post can accumulate in the queue if needed
			}
			if(pCtrl->shutdown) {
				gc_dbg("cleaning up http client!");
				esp_http_client_cleanup(pCtrl->client);
				gc_dbg("Unregistering Control Callback!");
				csControlCallbackUnregister(sysEventCb);
				gc_dbg("Unregistering Socket Callbacks!");
				emtrDrvCallbackUnregister(emtrCbId_socket1, emtrSocketEvtCb);
				emtrDrvCallbackUnregister(emtrCbId_socket2, emtrSocketEvtCb);
				gc_dbg("Stopping webserver!");
				httpd_stop(pCtrl->httpServer);
				gc_dbg("clearing queue");
				resetQueue(pCtrl);
				gc_dbg("deleting task");
				vTaskDelay(pdMS_TO_TICKS(10));
				pCtrl->shutdown=false;
				vTaskDelete(NULL);
				return;
			}
		    if(!isReceived && !pCtrl->pause){//update 15 minutes after last post
		    	http_post_to_status(pCtrl);
		    }
			queue_json = NULL;
			isReceived = xQueueReceive(pCtrl->queue, &queue_json, 0);
		} while(isReceived == pdTRUE);

	    esp_http_client_cleanup(pCtrl->client);
	}
}

static void emtrSocketEvtCb(
	uint32_t	cbData,
	callCtx_t	ctx,
	uint32_t	evtCode,
	uint32_t	evtData
)
{
	emtrEvtData_t *	eData = CS_ADR2PTR(evtData);
	char eventData[100]={0};

	switch((emtrEvtCode_t)evtCode)
	{
	case emtrEvtCode_socketOn:
		sprintf(eventData, "{\"socketIdx\":%d,\"state\":%s}",eData->onOff.socketNum-1,"1");
		sendEvent("position", eventData);
		break;

	case emtrEvtCode_socketOff:
		sprintf(eventData, "{\"socketIdx\":%d,\"state\":%s}",eData->onOff.socketNum-1,"0");
		sendEvent("position", eventData);
		break;

	case emtrEvtCode_plugInserted:
		sprintf(eventData, "{\"socketIdx\":%d,\"state\":%s}",eData->onOff.socketNum-1,"true");
		sendEvent("occupied", eventData);
		break;

	case emtrEvtCode_plugRemoved:
		sprintf(eventData, "{\"socketIdx\":%d,\"state\":%s}",eData->onOff.socketNum-1,"false");
		sendEvent("occupied", eventData);
		break;

	default:
		// Ignore other events
		return;
	}
}

static void sysEventCb(
	uint32_t	cbData,
	callCtx_t	ctx,
	uint32_t	evtCode,
	uint32_t	evtData
)
{
	control_t *	pCtrl = CS_ADR2PTR(cbData);

	csCtrlEvtData_t *	eData = CS_ADR2PTR(evtData);

	char MsgCode_buf[100]={0};
	char eventData[100]={0};
	cJSON * MsgCode_json = NULL;

	switch ((csCtrlEvtCode_t)evtCode)
	{
	case csCtrlEvtCode_rebooting:
		//Send reboot event
		sprintf(eventData, "{\"reason\":\"%s\"}",csBootReasonStr(eData->reboot.reason));
		sendEvent("reboot", eventData);
		break;

	case csCtrlEvtCode_fwUpgradeStart:
		//Send firmware start event
		sendEvent("fwu", "{\"state\":\"start\"}");

		sprintf(MsgCode_buf, "{\"appPWApiMsgCode\":%d}",appPWApiMsgCode_pause);
		MsgCode_json = cJSON_Parse(MsgCode_buf);
		if(xQueueSend(pCtrl->queue, &MsgCode_json, pdMS_TO_TICKS(10)) != pdPASS){
			//failed to send message code to queue
			gc_err("Failed to send message code to queue");
			cJSON_Delete(MsgCode_json);
			break;
		}
		break;

	case csCtrlEvtCode_fwUpgradeFail:
		// Firmware update failed - resume normal operation
		sprintf(MsgCode_buf, "{\"appPWApiMsgCode\":%d}",appPWApiMsgCode_resume);
		MsgCode_json = cJSON_Parse(MsgCode_buf);
		if(xQueueSend(pCtrl->queue, &MsgCode_json, pdMS_TO_TICKS(10)) != pdPASS){
			//failed to send message code to queue
			gc_err("Failed to send message code to queue");
			cJSON_Delete(MsgCode_json);
			break;
		}

		//Send firmware fail event
		sprintf(eventData, "{\"state\":\"fail\",\"message\":\"%s\"}",eData->fwUpgradeFail.reason);
		sendEvent("fwu", eventData);
		break;

	case csCtrlEvtCode_fwUpgradeSuccess:
		// Firmware success - resume normal operation so we can send event
		sprintf(MsgCode_buf, "{\"appPWApiMsgCode\":%d}",appPWApiMsgCode_resume);
		MsgCode_json = cJSON_Parse(MsgCode_buf);
		if(xQueueSend(pCtrl->queue, &MsgCode_json, pdMS_TO_TICKS(10)) != pdPASS){
			//failed to send message code to queue
			gc_err("Failed to send message code to queue");
			cJSON_Delete(MsgCode_json);
			break;
		}

		//Send firmware done event
		sendEvent("fwu", "{\"state\":\"done\"}");
		break;

	default:
		// Ignore other events
		return;
	}
}
