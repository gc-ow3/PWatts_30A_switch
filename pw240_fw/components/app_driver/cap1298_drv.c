/*
 * cap1298_iwo.c
 *
 *  Created on: Sep 1, 2017
 *      Author: cristian
 */
#include "cs_common.h"
#include "cs_heap.h"
#include "cap1298_drv.h"
#include "driver/gpio.h"
#include "driver/i2c.h"

#include "cs_i2c_bus.h"

// Comment out the MOD_NAME line to disable debug prints from this file
#define MOD_NAME	"cap_drv"
#include "mod_debug.h"

// Max time to wait for an I2C transfer to complete
#define WAIT_TIME_I2C						(20)

// How frequently to poll the CAP controller
#define POLL_CYCLE_MS						(10)

#define PRESS_TIMER_MS						(40)
#define DEBOUNCE_LIMIT						(8)

#define CAP1298_ID							(0x71)

// Register addresses
#define CAP_REG_MAIN_CTRL					(0x00)
#define CAP_REG_GENERAL_STS					(0x02)
#define CAP_REG_SENSOR_INPUT_STS			(0x03)
#define CAP_REG_NOISE_FLAG_STS				(0x0A)
#define CAP_REG_SENSOR_1_DELTA_CT			(0x10)
#define CAP_REG_SENSOR_2_DELTA_CT			(0x11)
#define CAP_REG_SENSOR_3_DELTA_CT			(0x12)
#define CAP_REG_SENSOR_4_DELTA_CT			(0x13)
#define CAP_REG_SENSOR_5_DELTA_CT			(0x14)
#define CAP_REG_SENSOR_6_DELTA_CT			(0x15)
#define CAP_REG_SENSOR_7_DELTA_CT			(0x16)
#define CAP_REG_SENSOR_8_DELTA_CT			(0x17)
#define CAP_REG_SENSITIVITY_CTRL			(0x1F)
#define CAP_REG_CONFIGURATION				(0x20)
#define CAP_REG_SENSOR_INPUT_ENABLE			(0x21)
#define CAP_REG_SENSOR_INPUT_CFG_1			(0x22)
#define CAP_REG_SENSOR_INPUT_CFG_2			(0x23)
#define CAP_REG_AVG_AND_SAMPLE_CFG			(0x24)
#define CAP_REG_CAL_ACTIVE_AND_STATUS		(0x26)
#define CAP_REG_INTERRUPT_ENABLE			(0x27)
#define CAP_REG_REPEAT_RATE_ENABLE			(0x28)
#define CAP_REG_SIGNAL_GUARD_ENABLE			(0x29)
#define CAP_REG_MULTI_TOUCH_CFG				(0x2A)
#define CAP_REG_MULTI_TOUCH_PATTERN_CFG		(0x2B)
#define CAP_REG_MULTI_TOUCH_PATTERN			(0x2D)
#define CAP_REG_BASE_COUNT_OUT_OF_LIMIT		(0x2E)
#define CAP_REG_RECALIBRATION_CFG			(0x2F)
#define CAP_REG_SENSOR_1_THRESHOLD			(0x30)
#define CAP_REG_SENSOR_2_THRESHOLD			(0x31)
#define CAP_REG_SENSOR_3_THRESHOLD			(0x32)
#define CAP_REG_SENSOR_4_THRESHOLD			(0x33)
#define CAP_REG_SENSOR_5_THRESHOLD			(0x34)
#define CAP_REG_SENSOR_6_THRESHOLD			(0x35)
#define CAP_REG_SENSOR_7_THRESHOLD			(0x36)
#define CAP_REG_SENSOR_8_THRESHOLD			(0x37)
#define CAP_REG_SENSOR_NOISE_THRESHOLD		(0x38)

// Standby registers
#define CAP_REG_STBY_CHANNEL				(0x40)
#define CAP_REG_STBY_CFG					(0x41)
#define CAP_REG_STBY_SENSITIVITY			(0x42)
#define CAP_REG_STBY_THRESHOLD				(0x43)
#define CAP_REG_CONFIGURATION_2				(0x44)

// Base count registers
#define CAP_REG_SENSOR_1_BASE_CT			(0x50)
#define CAP_REG_SENSOR_2_BASE_CT			(0x51)
#define CAP_REG_SENSOR_3_BASE_CT			(0x52)
#define CAP_REG_SENSOR_4_BASE_CT			(0x53)
#define CAP_REG_SENSOR_5_BASE_CT			(0x54)
#define CAP_REG_SENSOR_6_BASE_CT			(0x55)
#define CAP_REG_SENSOR_7_BASE_CT			(0x56)
#define CAP_REG_SENSOR_8_BASE_CT			(0x57)

// Power button registers
#define CAP_REG_POWER_BTN					(0x60)
#define CAP_REG_POWER_BTN_CFG				(0x61)

// Calibration sensitivity
#define CAP_REG_CAL_SENSITIVITY_CFG_1		(0x80)
#define CAP_REG_CAL_SENSITIVITY_CFG_2		(0x81)

// Calibration registers
#define CAP_REG_SENSOR_1_CAL				(0xB1)
#define CAP_REG_SENSOR_2_CAL				(0xB2)
#define CAP_REG_SENSOR_3_CAL				(0xB3)
#define CAP_REG_SENSOR_4_CAL				(0xB4)
#define CAP_REG_SENSOR_5_CAL				(0xB5)
#define CAP_REG_SENSOR_6_CAL				(0xB6)
#define CAP_REG_SENSOR_7_CAL				(0xB7)
#define CAP_REG_SENSOR_8_CAL				(0xB8)
#define CAP_REG_SENSOR_CAL_LSB_1			(0xB9)
#define CAP_REG_SENSOR_CAL_LSB_2			(0xBA)

// ID Registers
#define CAP_REG_PRODUCT_ID					(0xFD)
#define CAP_REG_MANUFACTURER_ID				(0xFE)
#define CAP_REG_REVISION					(0xFF)

// Sensor assignments to outlet function
#define CAP_SENSOR_A		(CAP_TOUCH_PAD2)
#define CAP_SENSOR_CENTER	(CAP_TOUCH_PAD4)
#define CAP_SENSOR_B		(CAP_TOUCH_PAD8)

#define SENSORS_ENABLED		(CAP_SENSOR_A | CAP_SENSOR_CENTER | CAP_SENSOR_B)

////////////////////////////////////////////////////////////////////////////////
// Data types
////////////////////////////////////////////////////////////////////////////////

// State machine states
typedef enum {
	smState_idle,
	smState_press,
	smState_release,
} smState_t;


// Task control structure
typedef struct {
	bool				isRunning;
	cap1298DrvConf_t	conf;
	UBaseType_t			taskPriority;
	TaskHandle_t		taskHandle;
	SemaphoreHandle_t	mutex;
	capDrvCallback_t	cbFunc;
	uint32_t			cbData;
	int					ioError;
	// State machine control
	capDrvSource_t		currSensorId;
	uint8_t				currSensorBit;
	smState_t			smState;
	uint32_t			smTimerMs;
	int					debounceCt;
	uint32_t			calTimeMs;
	uint8_t				activeSensors;
} taskCtrl_t;


// Controller initialization data structure
typedef struct {
	uint8_t		addr;
	uint8_t		val;
} const capInit_t;


////////////////////////////////////////////////////////////////////////////////
// Local functions
////////////////////////////////////////////////////////////////////////////////
static esp_err_t capDrvRead(taskCtrl_t * pCtrl, uint8_t reg, uint8_t * buf, int len);
static esp_err_t capDrvWrite(taskCtrl_t * pCtrl, uint8_t reg, uint8_t * buf, int len);
static void ctrlTask(void * arg);


////////////////////////////////////////////////////////////////////////////////
// Constant data
////////////////////////////////////////////////////////////////////////////////

// Register initializations
static capInit_t	capRegInit[] = {
	{CAP_REG_MAIN_CTRL,					0x40},
	{CAP_REG_SENSITIVITY_CTRL,			0x2F},
	{CAP_REG_CONFIGURATION,				0x20},
	{CAP_REG_SENSOR_INPUT_ENABLE,		SENSORS_ENABLED},
	{CAP_REG_SENSOR_INPUT_CFG_1,		0xA4},
	{CAP_REG_SENSOR_INPUT_CFG_2,		0x07},
	{CAP_REG_AVG_AND_SAMPLE_CFG,		0x39},
//	{CAP_REG_CAL_ACTIVE_AND_STATUS,		0x00},
	{CAP_REG_INTERRUPT_ENABLE,			0x00},
	{CAP_REG_REPEAT_RATE_ENABLE,		0x00},
	{CAP_REG_SIGNAL_GUARD_ENABLE,		0x00},
	{CAP_REG_MULTI_TOUCH_CFG,			0x00},
	{CAP_REG_MULTI_TOUCH_PATTERN_CFG,	0x00},
	{CAP_REG_MULTI_TOUCH_PATTERN,		0xFF},
	{CAP_REG_RECALIBRATION_CFG,			0x0A},
	{CAP_REG_SENSOR_1_THRESHOLD,		0x40},	// unused
	{CAP_REG_SENSOR_2_THRESHOLD,		0x20},	// Touch A
	{CAP_REG_SENSOR_3_THRESHOLD,		0x40},	// no connect
	{CAP_REG_SENSOR_4_THRESHOLD,		0x20},	// Center
	{CAP_REG_SENSOR_5_THRESHOLD,		0x40},	// no connect
	{CAP_REG_SENSOR_6_THRESHOLD,		0x40},	// no connect
	{CAP_REG_SENSOR_7_THRESHOLD,		0x40},	// unused
	{CAP_REG_SENSOR_8_THRESHOLD,		0x20},	// Touch B
	{CAP_REG_SENSOR_NOISE_THRESHOLD,	0x01},
	{CAP_REG_STBY_CHANNEL,				0x00},
	{CAP_REG_STBY_CFG,					0x39},
	{CAP_REG_STBY_SENSITIVITY,			0x02},
	{CAP_REG_STBY_THRESHOLD,			0x40},
	{CAP_REG_CONFIGURATION_2,			0x40},
	{CAP_REG_POWER_BTN,					0x00},
	{CAP_REG_POWER_BTN_CFG,				0x22},
	{CAP_REG_CAL_SENSITIVITY_CFG_1,		0x00},
	{CAP_REG_CAL_SENSITIVITY_CFG_2,		0x00}
};
#define capRegInitSz		(sizeof(capRegInit) / sizeof(capInit_t))


////////////////////////////////////////////////////////////////////////////////
// Local variables
////////////////////////////////////////////////////////////////////////////////

// Task control structure
static taskCtrl_t *	pTaskCtrl;


////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////


/**
 * \brief Initialize the CAP1298 driver
 */
esp_err_t cap1298DrvInit(const cap1298DrvConf_t * conf)
{
	taskCtrl_t *	pCtrl = pTaskCtrl;
	if (NULL != pCtrl)
		return ESP_OK;

	if ((pCtrl = cs_heap_calloc(1, sizeof(*pCtrl))) == NULL)
		return ESP_ERR_NO_MEM;

	pCtrl->conf = *conf;

	// Write initial values to the controller
	capInit_t *		init = capRegInit;
	int			i;
	uint8_t		wrValue;

	if ((pCtrl->mutex = xSemaphoreCreateMutex()) == NULL) {
		gc_err("Mutex create failed");
		return ESP_FAIL;
	}


	for (i = 0; i < capRegInitSz; i++, init++) {
		wrValue = init->val;

		if (capDrvWrite(pCtrl, init->addr, &wrValue, 1) != ESP_OK) {
			gc_err("Failed to write to CAP register %02X", init->addr);
			return ESP_FAIL;
		}
	}

	vTaskDelay(pdMS_TO_TICKS(50));

	uint8_t		regVal[3];

	if (capDrvRead(pCtrl, CAP_REG_PRODUCT_ID, regVal, 3) != ESP_OK) {
		gc_dbg("Failed to read CAP registers");
		return ESP_FAIL;
	}

	uint8_t	expected[3] = {0x71, 0x5D, 0x01};

	if (memcmp(expected, regVal, 3) != 0) {
		gc_err("Failed to read expected data from CAP1298");
		gc_hexDump2("Expected", expected, 3, true);
		gc_hexDump2("Received", regVal, 3, true);

		return ESP_FAIL;
	}

	pTaskCtrl = pCtrl;
	return ESP_OK;
}


/**
 * \brief Start the CAP1298 driver
 */
esp_err_t cap1298DrvStart(capDrvCallback_t cbFunc, uint32_t cbData)
{
	taskCtrl_t *	pCtrl = pTaskCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	if (pCtrl->isRunning)
		return ESP_OK;

	// Null callback function is not allowed
	if (NULL == cbFunc)
		return ESP_ERR_INVALID_ARG;

	// Attach the passed callback function and data
	pCtrl->cbFunc = cbFunc;
	pCtrl->cbData = cbData;

	esp_err_t		status;

	// Start the control task
	status = xTaskCreate(
		ctrlTask,
		"cap1298drv",
		2000,
		(void *)pCtrl,
		pCtrl->taskPriority,
		&pCtrl->taskHandle
	);
	if (pdPASS != status) {
		gc_err("Task create failed");
		return ESP_FAIL;
	}

	pCtrl->isRunning = true;
	return ESP_OK;
}


/**
 * \brief Signal the control task to calibrate the touch sensor
 */
esp_err_t cap1298DrvCalibrate(void)
{
	taskCtrl_t *	pCtrl = pTaskCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;
	if (!pCtrl->isRunning)
		return ESP_FAIL;

	pCtrl->calTimeMs = 200;
	return ESP_OK;
}


/**
 * \brief Return bit mask of active sensors
 *
 * Bits are defined by CAP_TOUCH_PADx
 * This is provided for factory test support
 *
 */
uint8_t cap1298DrvActiveSensors(void)
{
	taskCtrl_t *	pCtrl = pTaskCtrl;
	if (NULL == pCtrl)
		return (uint8_t)0;

	return pCtrl->activeSensors;
}


/**
 * \brief For the selected touch pad, return a set of registers
 *
 * \param [in] padNum 1..8
 * \param [out] regs
 *
 * \return ESP_OK on success
 * \return ESP_FAIL
 *
 */
esp_err_t cap1298ReadTouchRegs(uint8_t padNum, touchReg_t * regs)
{
	taskCtrl_t *	pCtrl = pTaskCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	if (padNum < 1 || padNum > 8)
		return ESP_ERR_INVALID_ARG;
	int		padIdx = padNum - 1;

	esp_err_t	status;

	status = capDrvRead(pCtrl, CAP_REG_SENSOR_1_DELTA_CT + padIdx, &regs->deltaCount, 1);
	if (ESP_OK != status)
		return status;

	status = capDrvRead(pCtrl, CAP_REG_SENSOR_1_BASE_CT + padIdx, &regs->baseCount, 1);
	if (ESP_OK != status)
		return status;

	return ESP_OK;
}


////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////


/**
 * \brief If there is a registered callback, send event notification to it
 */
static void notify(taskCtrl_t * pCtrl, capDrvEvt_t evtCode, capDrvSource_t src)
{
	if (pCtrl->cbFunc) {
		pCtrl->cbFunc(pCtrl->cbData, evtCode, src);
	}
}


/**
 * \brief Read a sequence of controller registers
 */
static esp_err_t _capDrvRead(taskCtrl_t * pCtrl, uint8_t reg, uint8_t * buf, int len)
{
	csI2cBusConf_t *	i2cBus  = pCtrl->conf.i2cBusConf;
	uint8_t				i2cAddr = pCtrl->conf.i2cAddr;
	i2c_port_t			i2cPort = pCtrl->conf.i2cBusConf->i2cPort;
	esp_err_t			status;
	i2c_cmd_handle_t	cmd;

	if ((cmd = i2c_cmd_link_create()) == NULL) {
		return ESP_ERR_NO_MEM;
	}

	// Send start
	i2c_master_start(cmd);
	// Send the slave address
	i2c_master_write_byte(cmd, i2cAddr | I2C_MASTER_WRITE, true);
	// Write the register number
	i2c_master_write_byte(cmd, reg, true);
	// Restart for the read cycle
	i2c_master_start(cmd);
	// Set the slave address
	i2c_master_write_byte(cmd, i2cAddr | I2C_MASTER_READ, true);
	// Read the data
	if (len > 1) {
		i2c_master_read(cmd, buf, (len - 1), I2C_MASTER_ACK);
	}
	i2c_master_read_byte(cmd, (buf + len - 1), I2C_MASTER_NACK);
	i2c_master_stop(cmd);

	if (i2cBus->busIsShared)
		i2cBus->busLock(i2cPort, portMAX_DELAY);
	status = i2c_master_cmd_begin(i2cPort, cmd, pdMS_TO_TICKS(WAIT_TIME_I2C));
	if (i2cBus->busIsShared)
		i2cBus->busUnlock(i2cPort);

	i2c_cmd_link_delete(cmd);
	return status;
}


static esp_err_t capDrvRead(taskCtrl_t * pCtrl, uint8_t reg, uint8_t * buf, int len)
{
	esp_err_t	status;

	xSemaphoreTake(pCtrl->mutex, portMAX_DELAY);
	status = _capDrvRead(pCtrl, reg, buf, len);
	xSemaphoreGive(pCtrl->mutex);

	return status;
}


/**
 * \brief Write a sequence of controller registers
 */
static esp_err_t _capDrvWrite(taskCtrl_t * pCtrl, uint8_t reg, uint8_t * buf, int len)
{
	csI2cBusConf_t *	i2cBus  = pCtrl->conf.i2cBusConf;
	uint8_t				i2cAddr = pCtrl->conf.i2cAddr;
	i2c_port_t			i2cPort = pCtrl->conf.i2cBusConf->i2cPort;
	esp_err_t			status;
	i2c_cmd_handle_t	cmd;

	if ((cmd = i2c_cmd_link_create()) == NULL) {
		return ESP_ERR_NO_MEM;
	}

	i2c_master_start(cmd);
	// Set the slave address
	i2c_master_write_byte(cmd, i2cAddr | I2C_MASTER_WRITE, true);
	// Write the register number
	i2c_master_write_byte(cmd, reg, true);
	// Write the data
	i2c_master_write(cmd, buf, len, true);
	i2c_master_stop(cmd);

	if (i2cBus->busIsShared)
		i2cBus->busLock(i2cPort, portMAX_DELAY);
	status = i2c_master_cmd_begin(i2cPort, cmd, pdMS_TO_TICKS(WAIT_TIME_I2C));
	if (i2cBus->busIsShared)
		i2cBus->busUnlock(i2cPort);

	i2c_cmd_link_delete(cmd);
	return status;
}


static esp_err_t capDrvWrite(taskCtrl_t * pCtrl, uint8_t reg, uint8_t * buf, int len)
{
	esp_err_t	status;

	xSemaphoreTake(pCtrl->mutex, portMAX_DELAY);
	status = _capDrvWrite(pCtrl, reg, buf, len);
	xSemaphoreGive(pCtrl->mutex);

	return status;
}


#if 0	// Not currently used so hide it for now
/**
 * \brief Set a mask of bit in a controller register
 */
static void capDrvSetBits(taskCtrl_t * pCtrl, uint8_t reg, uint8_t mask)
{
	uint8_t		value;

	xSemaphoreTake(pCtrl->mutex, portMAX_DELAY);

	pCtrl->ioError = _capDrvRead(pCtrl, reg, &value, 1);
	if (ESP_OK == pCtrl->ioError) {
		// Set the bits in the mask
		value |= mask;
		pCtrl->ioError = _capDrvWrite(pCtrl, reg, &value, 1);
	}

	xSemaphoreGive(pCtrl->mutex);
}
#endif


/**
 * \brief Clear a mask of bit in a controller register
 */
static void capDrvClearBits(taskCtrl_t * pCtrl, uint8_t reg, uint8_t mask)
{
	uint8_t		value;

	xSemaphoreTake(pCtrl->mutex, portMAX_DELAY);

	pCtrl->ioError = _capDrvRead(pCtrl, reg, &value, 1);
	if (ESP_OK == pCtrl->ioError) {
		// Clear the bits in the mask
		value &= ~mask;
		pCtrl->ioError = _capDrvWrite(pCtrl, reg, &value, 1);
	}

	xSemaphoreGive(pCtrl->mutex);
}


/**
 * \brief Reset the INT bit in the status/control register
 */
static void capDrvResetSensorStatus(taskCtrl_t * pCtrl)
{
	// Clear the INT bit in Main Control register
	capDrvClearBits(pCtrl, CAP_REG_MAIN_CTRL, (1 << 0));
}


/**
 * \brief Read a register
 */
static uint8_t capDrvGetReg(taskCtrl_t * pCtrl, uint8_t reg)
{
	uint8_t 	value = 0;

	pCtrl->ioError = capDrvRead(pCtrl, reg, &value, 1);

    return value;
}


/**
 * \brief Update the CAP sensor state machine
 */
static void stateMachineUpdate(taskCtrl_t * pCtrl)
{
	switch (pCtrl->smState)
	{
	case smState_idle:
		if (0 == pCtrl->activeSensors)
			return;

		if (CAP_SENSOR_A & pCtrl->activeSensors) {
			// Outlet A button
			if (capDrvSource_A != pCtrl->currSensorId) {
				//gc_dbg("Sensor: A");
				pCtrl->currSensorId  = capDrvSource_A;
				pCtrl->currSensorBit = CAP_SENSOR_A;
				pCtrl->smState       = smState_press;
				pCtrl->smTimerMs     = 0;
			}
			pCtrl->debounceCt = 0;
		} else if (CAP_SENSOR_B & pCtrl->activeSensors) {
			// Outlet B button
			if (capDrvSource_B != pCtrl->currSensorId) {
				//gc_dbg("Sensor: B");
				pCtrl->currSensorId  = capDrvSource_B;
				pCtrl->currSensorBit = CAP_SENSOR_B;
				pCtrl->smState       = smState_press;
				pCtrl->smTimerMs     = 0;
			}
			pCtrl->debounceCt = 0;
		} else if (CAP_SENSOR_CENTER & pCtrl->activeSensors) {
			// Center button
			if (capDrvSource_center != pCtrl->currSensorId) {
				//gc_dbg("Sensor: Center");
				pCtrl->currSensorId  = capDrvSource_center;
				pCtrl->currSensorBit = CAP_SENSOR_CENTER;
				pCtrl->smState       = smState_press;
				pCtrl->smTimerMs     = 0;
			}
			pCtrl->debounceCt = 0;
		}
		break;

	case smState_press:
		if (pCtrl->activeSensors & pCtrl->currSensorBit) {
			// Same button being pressed
			pCtrl->debounceCt = 0;
			if (pCtrl->smTimerMs >= PRESS_TIMER_MS) {
				// Button was pressed long enough to count
				notify(pCtrl, capDrvEvt_press, pCtrl->currSensorId);
				// Now wait for it to be released
				pCtrl->smState = smState_release;
			}
		} else if (pCtrl->debounceCt < DEBOUNCE_LIMIT) {
			// Debounce check
			pCtrl->debounceCt += 1;
		} else {
			// Ignore spurious press
			gc_dbg("Sensor glitch ignored");
			pCtrl->currSensorId = capDrvSource_null;
			pCtrl->smState      = smState_idle;
		}
		break;

	case smState_release:
		// Waiting for release
		if (pCtrl->activeSensors & pCtrl->currSensorBit) {
			// Ignore the button as long as it's held
			pCtrl->debounceCt = 0;
		} else if (pCtrl->debounceCt < DEBOUNCE_LIMIT) {
			// Debounce check
			pCtrl->debounceCt += 1;
		} else {
			// Counts as a release
			notify(pCtrl, capDrvEvt_release, pCtrl->currSensorId);
			pCtrl->currSensorId = capDrvSource_null;
			pCtrl->smState      = smState_idle;
		}
		break;

	default:
		gc_err("Unexpected state %d", pCtrl->smState);
		break;
	}
}


/**
 * \brief Check for pending calibration
 */
static esp_err_t calibrate(taskCtrl_t * pCtrl)
{
	if (pCtrl->calTimeMs == 0)
		return ESP_OK;

	if (pCtrl->calTimeMs > POLL_CYCLE_MS) {
		pCtrl->calTimeMs -= POLL_CYCLE_MS;
		return ESP_OK;
	} else {
		pCtrl->calTimeMs = 0;
	}

	uint8_t		wrValue;

	// Perform calibration
	wrValue = SENSORS_ENABLED;
	capDrvWrite(pCtrl, CAP_REG_CAL_ACTIVE_AND_STATUS, &wrValue, 1);

	uint32_t	calTimer;
	uint32_t	calDelay = 50;

	for (calTimer = 0; calTimer < 1000; calTimer += calDelay) {
		uint8_t		genStatus;

		// Check periodically for the calibration status bit to be cleared
		vTaskDelay(pdMS_TO_TICKS(calDelay));

		// Check the ACAL status bit in General Status
		if (capDrvRead(pCtrl, CAP_REG_GENERAL_STS, &genStatus, 1) != ESP_OK) {
			gc_err("Failed to read CAP_REG_GENERAL_STS register");
			continue;
		} else if (0 == (genStatus & (1 << 5))) {
			return ESP_OK;
		} else {
			uint8_t	acalStatus = 0xff;
			capDrvRead(pCtrl, CAP_REG_CAL_ACTIVE_AND_STATUS, &acalStatus, 1);
			if (0 == (acalStatus & SENSORS_ENABLED)) {
				return ESP_OK;
			}
		}
	}

	gc_err("Auto calibration failed");
	return ESP_FAIL;
}


/**
 * \brief CAP1298 control task
 */
static void ctrlTask(void * taskParam)
{
	taskCtrl_t *	pCtrl = (taskCtrl_t *)taskParam;

	uint32_t	pollDelayMs = POLL_CYCLE_MS;

	pCtrl->smState      = smState_idle;
	pCtrl->currSensorId = capDrvSource_null;
	pCtrl->debounceCt   = 0;

	while (1)
	{
		vTaskDelay(pdMS_TO_TICKS(pollDelayMs));

		pCtrl->smTimerMs += pollDelayMs;

		// Check for pending calibration
		(void)calibrate(pCtrl);

		// Read status of sensors
		pCtrl->activeSensors = capDrvGetReg(pCtrl, CAP_REG_SENSOR_INPUT_STS);
		if (pCtrl->ioError == 0) {
			// Update the state machine
			stateMachineUpdate(pCtrl);

			// Clear status flags
			capDrvResetSensorStatus(pCtrl);

			pollDelayMs = POLL_CYCLE_MS;
		} else {
			gc_err("Failed to read Sensor status");
			pollDelayMs = 100;
		}
	}
}
