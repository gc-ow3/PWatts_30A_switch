#include "sdkconfig.h"

#include <esp_err.h>
#include <esp_log.h>
#include <driver/i2c.h>
#include <driver/gpio.h>
#include <driver/uart.h>
//#include <esp_flash_encrypt.h>

#include "cs_common.h"
#include "pinout.h"
#include "led_drv.h"
#include "app_emtr_drv.h"
#include "emtr_pwr_sig.h"

static const char	TAG[] = {"app_main"};

/*
********************************************************************************
* Local functions
********************************************************************************
*/
static esp_err_t modulesInit(void);
static esp_err_t modulesStart(void);

/*
********************************************************************************
* Constant data
********************************************************************************
*/
#if 0
// Product identifying information
static const csCoreInit0_t coreInit0 = {
	.manufacturer  = "Wayne Water",
	.product       = "Halo",
	.model         = "WW-HALO",
	.mfgModel      = "MFG-HALO",
	.fwVersion     = appFwVer,
	.mqttTopicRoot = "iot8020",
	.features = {
		.iotPing    = 0,	// ToDo Enable when cloud support is added
		.secMfgData = 0
	},
	.otaUpdate     = {
#if CONFIG_SECURE_BOOT
		.type = "mcu",
#else
		.type = "xmcu",
#endif
		.reqCert = true
	}
};
#endif

#ifdef CONFIG_IOT8020_USE_BLUFI

// BLUFI provisioning UUID in reverse order:
// 0000FFFF-0000-1000-8000-00805F9B34FB
static const uint8_t	blufiProvUuid[16] = {
	0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00,
	0x00, 0x80,
	0x00, 0x10,
	0x00, 0x00,
	0xFF, 0xFF,		// Service id
	0x00, 0x00
};

#endif


/*
********************************************************************************
* Global data
********************************************************************************
*/
//appSelfTest_t	appSelfTest;
//appInfo_t		appInfo;


/*
********************************************************************************
* Private data
********************************************************************************
*/


/*
********************************************************************************
* Public functions
********************************************************************************
*/


/**
 * \brief Product software execution starts here
 *
 */
void app_main(void)
{
	// Do not change this message because factory process checks for it
	printf("\r\nApplication start\r\n");

	// Start the LED manager first
	ESP_ERROR_CHECK(ledDrvInit());
	ESP_ERROR_CHECK(ledDrvStart());

	ledMode_t	ledMode[] = {ledMode_red, ledMode_grn, ledMode_blu};

	// Flash the LEDs to show boot
	int	i;
	for (i = 0; i < 3; i++) {
		ledMode_t	mode = ledMode[i];

		vTaskDelay(pdMS_TO_TICKS(200));
		ledDrvSetMode(ledId_system, mode);
		ledDrvSetMode(ledId_ble, mode);

		vTaskDelay(pdMS_TO_TICKS(200));
		ledDrvSetMode(ledId_system, ledMode_off);
		ledDrvSetMode(ledId_ble, ledMode_off);
	}

	// Do low-level core initialization to load manufacturing data and parameters first
//	ESP_LOGD(TAG, "csCoreInit0");
//	ESP_ERROR_CHECK(csCoreInit0((csCoreInit0_t *)&coreInit0));

	// Load application manufacturing data
//	ESP_LOGD(TAG, "appMfgDataLoad");
//	ESP_ERROR_CHECK(appMfgDataLoad());

	// Load application parameters
//	ESP_LOGD(TAG, "appParamsLoad");
//	ESP_ERROR_CHECK(appParamsLoad());

	// Start EMTR driver
	// Flash LEDs in case firmware update is required during start
	ESP_ERROR_CHECK(appEmtrDrvInit());
	//ledMgrFlashLed(ledId_system, 250, 250, 0, 0);
	ESP_ERROR_CHECK(appEmtrDrvStart());
	//ledMgrTurnLedOff(ledId_system);

#if 0
	// Initialize the CS core
	csCoreInit1_t	coreInit1 = {
		.awsConf = {
			.clientTaskPriority = TASK_PRIO_AWS_CLIENT,
			.connectionTimeoutMs = (5UL * 60UL * 1000UL)
		},
#ifdef CONFIG_IOT8020_USE_BLUFI
		.blufiConf = {
			.uuid       = blufiProvUuid,
			.appMethods = NULL,
			.features = {
				.accountId  = 1,
				.claimCode  = 1,
				.iotURL     = 0,
				.apiURL     = 0,
				.instDate   = 1
			}
		},
#endif
	};

	ESP_LOGD(TAG, "csCoreInit1");
	ESP_ERROR_CHECK(csCoreInit1(&coreInit1));
#endif

	// Print sign-on information
#if 0
	csCoreSignon();
	printf("---- Device-specific info ----\r\n");
	printf("(TBD)\r\n");
	printf("\r\n");
#endif

	// Display the parameters
//	paramMgrSettingsDump();

	// Initialize application modules
	ESP_ERROR_CHECK(modulesInit());

#if 0
	// Start application modules that may need to act on events from the core starting
	esp_err_t	status;

	if ((status = appControlStart()) != ESP_OK) {
		ESP_LOGE(TAG, "appControlStart error %d", status);
	}

	// Start core modules
	csCoreStartParams_t	startParams = {
		.softApProvConf = {
			.rsaPrvKey = appMfgData.rsaPrvKey,
			// Select provisioning features used by this device
			.features = {
				.accountId = 1,
				.claimCode = 1,
				.iotURL    = 0,
				.apiURL    = 0,
				.instDate  = 1
			},
			.testFuncCb = appSoftApTestCb
		}
	};

	ESP_LOGD(TAG, "Start core modules");
	ESP_ERROR_CHECK(csCoreStart(&startParams));
#endif

	// Start application modules
	ESP_ERROR_CHECK(modulesStart());
}


/*
********************************************************************************
* Private functions
********************************************************************************
*/


void pwrSigCallback(
	pwrSigMeta_t*	meta,
	uint8_t*		data,
	int				dataLen,
	void*			cbData
)
{
#if 0
	ESP_LOGI(TAG, "pwrSig meta data");
	ESP_LOGI(TAG, "  Reason          : %s", pwrSigReasonStr(meta->reason));
	ESP_LOGI(TAG, "  Samples         : %u", meta->numSamples);
	ESP_LOGI(TAG, "  Resolution      : %u", meta->resolution);
	ESP_LOGI(TAG, "  Relay cycles    : %u", meta->relayCycles);
	ESP_LOGI(TAG, "  Seconds powered : %u", meta->timePowered);
	ESP_LOGI(TAG, "  Seconds running : %u", meta->timeRunning);
	if (pwrSigReason_on == meta->reason) {
		ESP_LOGI(TAG, "  Inrush amps     : %0.3f", (double)meta->mAmpsInrush/1000.0);
	} else if (pwrSigReason_off == meta->reason) {
		ESP_LOGI(TAG, "  Cycle length    : %u", meta->cycleLength);
		ESP_LOGI(TAG, "  Average volts   : %0.2f", (double)meta->dVolts/10.0);
		ESP_LOGI(TAG, "  Average amps    : %0.2f", (double)meta->mAmps/1000.0);
		ESP_LOGI(TAG, "  Average pFactor : %u", meta->pFactor);
		ESP_LOGI(TAG, "  Temperature     : %u", meta->temperature);
	}
#endif

	// ToDo

}


/**
 * \brief Perform product-specific module initializations
 */
static esp_err_t modulesInit(void)
{
	ESP_LOGI(TAG, "Initialize modules");

	pwrSigConf_t	pwrSigConf = {
		.port = UART_NUM_2,
		.rxGpio = UART2_RX_GPIO,
		.baudRate = 921600,
		.taskPriority = 4,
		.cbFunc = NULL,		// ToDo
		.cbData = NULL		// ToDo
	};
	ESP_ERROR_CHECK(pwrSigInit(&pwrSigConf));

#if 0
	esp_err_t	status;
	if ((status = appControlInit()) != ESP_OK) {
		ESP_LOGE(TAG, "appControlInit error %d", status);
		return status;
	}
#endif

	return ESP_OK;
}


/**
 * \brief Start product-specific modules
 */
static esp_err_t modulesStart(void)
{
	ESP_LOGI(TAG, "Start modules");

	ESP_ERROR_CHECK(pwrSigStart());

	// ToDo ?

	return ESP_OK;
}


