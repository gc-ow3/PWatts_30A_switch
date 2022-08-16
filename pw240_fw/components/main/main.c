
#include <driver/i2c.h>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_flash_encrypt.h>

#include "cs_platform.h"
#include "cs_control.h"
#include "cs_framework.h"
#include "cs_binhex.h"
#include "mfg_data.h"
#include "param_mgr.h"
#include "emtr_drv.h"
#include "app_pw_api.h"
#include "app_params.h"
#include "app_led_mgr.h"
#include "app_control.h"
#include "app_self_test.h"
#include "outlet_mgr.h"
#include "cap1298_handler.h"
#include "utest_cap1298.h"
#include "fw_file_check.h"
#include "cs_prov_support.h"

// Comment out the MOD_NAME line to disable debug prints from this file
#define MOD_NAME	"app_main"
#include "mod_debug.h"

#define PWGW_FILTER "PWGW"

/*
********************************************************************************
* Local functions
********************************************************************************
*/
static const char * isFound(bool value);
//static const char * isEnabled(bool value);
static esp_err_t modulesInit(void);
static esp_err_t modulesStart(void);


/*
********************************************************************************
* Constant data
********************************************************************************
*/
// Product identifying information
csCoreInit0_t appInfo = {
	.manufacturer  = "Powder Watts",
	.product       = "PW240",
	.model         = "switch30a",
	.fwVersion     = appFwVer,
};


/**
 * \brief Configure the EMTR driver for CS-IWO hardware
 */
static const emtrDrvConf_t	emtrConf = {
	.uartCmd = {
		.uart         = UART_NUM_0,
		.baudRate     = 921600UL,
		.gpioUartTx   = GPIO_NUM_1,
		.gpioUartRx   = GPIO_NUM_3
	},
	.gpioEmtrRst  = GPIO_NUM_0,
	.numSockets   = 2,
	.taskPrio     = TASK_PRIO_EMTR_DRV
};


/**
 * \brief Configure the ESP I2C driver info for bus 1
 */
static const i2c_config_t	i2c1Conf = {
	.mode          = I2C_MODE_MASTER,
	.sda_io_num    = 23,
	.sda_pullup_en = false,
	.scl_io_num    = 19,
	.scl_pullup_en = false,
	.master = {
		.clk_speed = 100000UL
	}
};


/**
 * \brief Application I2C bus configuration
 *
 * This port is assigned to CAP1298 and ATECC608A
 *
 */
static csI2cBusConf_t i2cBus1Conf = {
	.i2cPort      = I2C_NUM_1,
	.busIsShared  = true,
	.busLock      = csI2cMutexTake,
	.busUnlock    = csI2cMutexGive,
	.i2cConf      = &i2c1Conf
};


/**
 * \brief I2C configuration for the CAP1298 touch controller
 */
static const cap1298DrvConf_t cap1298Conf = {
	.taskPriority = TASK_PRIO_CAP1298_DRV,
	.i2cAddr      = (0x50),
	.i2cBusConf   = &i2cBus1Conf
};


/*
********************************************************************************
* Global data
********************************************************************************
*/
appSelfTest_t	appSelfTest;


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
	printf("\r\nApplication startup\r\n");

	// This can be removed with IDF version 3.2.3, or 3.3.1, or 4.x.y
	if (esp_flash_encryption_enabled()) {
		esp_flash_write_protect_crypt_cnt();
	}

	// Start the LED manager first
	ESP_ERROR_CHECK(ledMgrInit(100));
	ESP_ERROR_CHECK(ledMgrStart());

	// Flash the LEDs twice to show boot
	int	i;
	for (i = 0; i < 2; i++) {
		ledMgrTurnLedOn(csLedNum_wifi);
		ledMgrTurnLedOn(csLedNum_status);
		ledMgrTurnLedOn(csLedNum_socket1);
		ledMgrTurnLedOn(csLedNum_socket2);
		vTaskDelay(pdMS_TO_TICKS(125));

		ledMgrTurnLedOff(csLedNum_wifi);
		ledMgrTurnLedOff(csLedNum_status);
		ledMgrTurnLedOff(csLedNum_socket1);
		ledMgrTurnLedOff(csLedNum_socket2);
		vTaskDelay(pdMS_TO_TICKS(125));
	}

	// Set up I2C Port 1, used by CAP1298 and ATECC608A
	ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_1, i2cBus1Conf.i2cConf));
	ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_1, I2C_MODE_MASTER, 0, 0, 0));

	// I2C bus 1 is shared by two devices requiring a mutex
	ESP_ERROR_CHECK(csI2cMutexCreate(1));

	// Do low-level core initialization to load manufacturing data and parameters first
	//gc_dbg("Core init 0");
	ESP_ERROR_CHECK(csCoreInit0(&appInfo));

	// Load application parameters
	//c_dbg("appParamsLoad");
	ESP_ERROR_CHECK(appParamsLoad());

	// Higher-level core initialization
	// - Initializes support APIs and tasks
	csCoreInit1_t	coreInit1 = {
		.controlConf={
			.wifiTimeoutMs = (60UL * 1000UL)
		},
	};

	//gc_dbg("csCoreInit1");
	ESP_ERROR_CHECK(csCoreInit1(&coreInit1));

	// Run components through self test
	appSelfTest.failCount = 0;

	// Test for presence of CAP1298 controller
	appSelfTest.cap1298Present = (utestCap1298(&cap1298Conf) == ESP_OK);
	appSelfTest.failCount += (appSelfTest.cap1298Present ? 0 : 1);

	// Test for presence of EMTR
	appSelfTest.emtrPresent = (emtrDrvInit(&emtrConf) == ESP_OK);
	appSelfTest.failCount  += (appSelfTest.emtrPresent ? 0 : 1);

	// Put out the sign-on message
	printf("\r\n\r\n");
	printf("Product name        : %s\r\n", csCoreConf.info.product);
	printf("Model number        : %s\r\n", csCoreConf.info.model);
	printf("Manufacturer        : %s\r\n", csCoreConf.info.manufacturer);
	printf("Firmware version    : %s\r\n", csCoreConf.info.fwVersion);
	printf("Hardware version    : %s\r\n", coreMfgData.hwVersion);
	printf("CS-core version     : %s\r\n", csCoreVersion);
	printf("ESP-IDF version     : %s\r\n", esp_get_idf_version());
	printf("Serial number       : %s\r\n", coreMfgData.serialNum);
	printf("Base MAC address    : %s\r\n", csCoreConf.baseMacStr);
	printf("Touch sensor        : %s\r\n", isFound(appSelfTest.cap1298Present));
	printf("EMTR                : %s\r\n", isFound(appSelfTest.emtrPresent));
	if (appSelfTest.emtrPresent) {
		printf("  BL version        : %s\r\n", emtrDrvGetBlVersion());
		printf("  FW version        : %s\r\n", emtrDrvGetFwVersion());
	}
	printf("Last boot reason    : %s\r\n", csBootReasonGet());
	printf("\r\n");

	// If any of the component checks failed, halt with error
	//ESP_ERROR_CHECK((0 == appSelfTest.failCount) ? ESP_OK : ESP_FAIL);

	// Display the parameters
	paramMgrSettingsDump();

	// If EMTR is present, start the driver before the rest of the application
	if (appSelfTest.emtrPresent) {
		// If the EMTR driver determines there is a firmware update to perform
		// it will take about a minute to return from this call
		// Set the two socket LEDs to flash during this period
		ledMgrFlashLed(csLedNum_socket1, 250, 250, 0, 0);
		ledMgrFlashLed(csLedNum_socket2, 250, 250, 0, 0);

		ESP_ERROR_CHECK(emtrDrvStart());

		ledMgrTurnLedOff(csLedNum_socket1);
		ledMgrTurnLedOff(csLedNum_socket2);
	}

	// Set configured LED brightness
	ledMgrSetBrightness(appParams.brightness, callCtx_local);

	// Initialize application modules
	ESP_ERROR_CHECK(modulesInit());

	// Start application modules that depend on events from core
	ESP_ERROR_CHECK(appControlStart());

	// Start core modules
	csCoreStartParams_t	startParams = {
		.provConf = {
			.ssidFilter = PWGW_FILTER,
		}
	};
	ESP_ERROR_CHECK(csCoreStart(&startParams));

	// Start application modules
	ESP_ERROR_CHECK(modulesStart());

	// All done - party like it's 1999
}


/*
********************************************************************************
* Private functions
********************************************************************************
*/


static const char * isFound(bool value)
{
	return value ? "Found" : "Not Found";
}


/*static const char * isEnabled(bool value)
{
	return value ? "Yes" : "No";
}
*/

/**
 * \brief Perform product-specific module initializations
 */
static esp_err_t modulesInit(void)
{
	esp_err_t	status;

	gc_dbg("Initialize modules");

	if ((status = appControlInit()) != ESP_OK) {
		gc_err("appControlInit error %d", status);
		return status;
	}

	if ((status = cap1298HandlerInit(&cap1298Conf)) != ESP_OK) {
		gc_err("cap1298HandlerInit error %d", status);
		return status;
	}

	if ((status = outletMgrInit()) != ESP_OK) {
		gc_err("outletMgrInit error %d", status);
		return status;
	}

	if ((status = appPWApiInit()) != ESP_OK) {
		// Print error, but keep going
		gc_err("appPWApiInit error %d", status);
	}

	return ESP_OK;
}


/**
 * \brief Start product-specific modules
 */
static esp_err_t modulesStart(void)
{
	esp_err_t		status;

	gc_dbg("Start modules");

	if ((status = cap1298HandlerStart()) != ESP_OK) {
		gc_err("cap1298HandlerStart error %d", status);
		return status;
	}

	if ((status = outletMgrStart()) != ESP_OK) {
		gc_err("outletMgrStart error %d", status);
		return status;
	}

	// If manufacturing data is not valid, the unit is in production test
	// and has not had its data set, so do a minimal startup and bypass
	// starting the extra features
	if (!coreMfgData.isValid || csSelfTestIsEnabled()) {

		return ESP_OK;
	}

	return ESP_OK;
}


