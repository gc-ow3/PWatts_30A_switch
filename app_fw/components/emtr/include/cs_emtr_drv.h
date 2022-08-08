/** \file cs_emtr_drv.h
 * Declarations for the EMTR driver
 *
 */

#ifndef COMPONENTS_CS_DRIVER_INCLUDE_CS_EMTR_DRV_H_
#define COMPONENTS_CS_DRIVER_INCLUDE_CS_EMTR_DRV_H_

#include "sdkconfig.h"

// Standard C headers
#include "cs_common.h"

#include <driver/uart.h>

#ifdef __cplusplus
extern "C" {
#endif


/**
 * \brief socket number data type
 */
typedef uint8_t	csEmtrSockNum_t;


/**
 * \brief EMTR socket information table
 *
 * Defines the attributes and command codes common to all instances of EMTR
 * Set the cmd code value to 0 for commands not supported by the current
 * version of EMTR firmware
 */
typedef struct {
	csEmtrSockNum_t	sockNum;	//!< Socket number, starting with 1
	struct {
		uint8_t	turnOn;			//!< Turn on a socket
		uint8_t	turnOff;		//!< Turn off a socket
		uint8_t	instEnergyGet;	//!< Get instant energy
		uint8_t	wattHoursGet;	//!< Get Watt-Hours
		uint8_t	cyclesGet;		//!< Get cycle counts
		uint8_t	hcciGet;		//!< Get HCCI level
		uint8_t	hcciSet;		//!< Set HCCI level
		uint8_t	loadDetectSet;	//!< Set load detection hysteresis hi/lo
		uint8_t	sigRead;		//!< On-demand read of power signature
		uint8_t	calGet;			//!< Read calibration data
		uint8_t	calSet;			//!< Store calibration to flash
	} cmd;
} const csEmtrSockInfo_t;


/**
 * \brief EMTR UART configuration
 *
 * If enable is false, the rest of the structure will be ignored
 */
typedef struct {
	uart_port_t		port;			//!< UART port number
	int8_t			gpioUartTx;		//!< GPIO pin for UART transmit
	int8_t			gpioUartRx;		//!< GPIO pin for UART receive
	uint32_t		baudRate;		//!< UART baud rate
} csEmtrUartConf_t;


/**
 * \brief EMTR driver events that may be sent to registered callback functions
 *
 * Updated EMTR firmware will be required for supporting these events in CS-IWO:
 *   _relayCycles
 *   _loadState
 *   _loadCycles
 *
 */
typedef enum {
	csEmtrEvtCode_commUp,			//!< EMTR communication is up
	csEmtrEvtCode_commDown,			//!< EMTR communication is down
	csEmtrEvtCode_reset,			//!< EMTR communication has been reset
	csEmtrEvtCode_temperature,		//!< Value change: Temperature
	csEmtrEvtCode_epoch,			//!< Value change: Epoch
	csEmtrEvtCode_overload,			//!< Value change: Overload detected
	csEmtrEvtCode_volts,			//!< Value change: instantaneous volts
	csEmtrEvtCode_amps,				//!< Value change: instantaneous amps
	csEmtrEvtCode_watts,			//!< Value change: instantaneous watts
	csEmtrEvtCode_pFactor,			//!< Value change: instantaneous power factor
	csEmtrEvtCode_wattHours,		//!< Value change: cumulative watt-hours
	csEmtrEvtCode_relayState,		//!< Value change: relay active/inactive
	csEmtrEvtCode_relayCycles,		//!< Value change: relay cycle count
	csEmtrEvtCode_loadState,		//!< Value change: load active/inactive
	csEmtrEvtCode_loadCycles,		//!< Value change: load cycle count
	csEmtrEvtCode_plugState,		//!< Value change: plug detected
} csEmtrEvtCode_t;


/**
 * \brief EMTR driver event data
 */
typedef union {
	struct {
		uint16_t	value;			//!< Degrees C
	} temperature;					//!< data for csEmtrEvtCode_temperature
	struct {
		uint32_t	value;			//!< cumulative seconds powered
	} epoch;						//!< data for csEmtrEvtCode_epoch
	struct {
		csEmtrSockNum_t	sockNum;	//!< Socket number
		uint32_t		value;		//!< Number of seconds relay has been in the current state
	} stateTime;					//!< data for csEmtrEvtCode_relayStateTime
	struct {
		csEmtrSockNum_t	sockNum;	//!< Socket number
		uint16_t		value;		//!< dVolts, mAmps, dWatts, or ratio (0..100)
	} energy;						//!< data for csEmtrEvtCode_volts, _amps, _watts, _powerFactor
	struct {
		csEmtrSockNum_t	sockNum;	//!< Socket number
		uint32_t		value;		//!< cumulative watt-Hours
	} wattHours;					//!< data for csEmtrEvtCode_wattHours
	struct {
		csEmtrSockNum_t	sockNum;	//!< Socket number
		bool			value;		//!< true/false state
	} state;						//!< data for a change of state notification
	struct {
		csEmtrSockNum_t	sockNum;	//!< Socket number
		bool			active;		//!< Relay state: active/inactive
		uint32_t		stateTime;	//!< Number of seconds relay has been in the current state
		uint32_t		cycles;		//!< Cumulative count of relay switching on
		uint32_t		epoch;		//!<
	} stateChg;						//!< data for csEmtrEvtCode_relayState
	struct {
		csEmtrSockNum_t	sockNum;
		uint32_t		count;
		uint32_t		epoch;
	} cycle;
} csEmtrEvtData_t;


/**
 * \brief Device-level status information
 *
 * See \ref csEmtrGetDeviceStatus
 *
 */
typedef struct {
	// Items maintained by driver
	bool		commUp;			//!< True if EMTR communication is active
	uint32_t	resetCount;		//!< Number of times EMTR has been reset since boot
	// Items read from EMTR
	uint16_t	temperature;	//!< Degrees C
	uint32_t	epoch;			//!< Cumulative number of seconds EMTR has been powered
} csEmtrDeviceStatus_t;


/**
 * \brief Instant energy measurements
 */
typedef struct {
	uint16_t		dVolts;		//!< units of 0.1 Volts
	uint16_t		mAmps;		//!< units of 0.001 Amps
	uint16_t		dWatts;		//!< units of 0.1 Watts
	uint16_t		pFactor;	//!< Power factor ratio 0..100
} csEmtrInstEnergy_t;


/**
 * \brief For an energy type min/max/average values
 */
typedef struct {
	uint32_t	min;	//!< Minimum value
	uint32_t	max;	//!< Maximum value
	uint32_t	avg;	//!< Average value
} csEmtrAvgEnergy_t;


/**
 * \brief Accumulated values for energies
 */
typedef struct {
	csEmtrAvgEnergy_t	dVolts;		//!< units of 0.1 Volts
	csEmtrAvgEnergy_t	mAmps;		//!< units of 0.001 Amps
	csEmtrAvgEnergy_t	dWatts;		//!< units of 0.1 Watts
	csEmtrAvgEnergy_t	pFactor;	//!< Power factor ratio 0..100
} csEmtrAccEnergy_t;


/**
 * \brief Counters for relay On/Off and Load cycles
 *
 * These are cumulative counters that persist over reboots and power cycles
 *
 * \note At present relay and load cycle counting is not available in the CS-IWO
 */
typedef struct {
	uint32_t	relayCycles;	//!< Number of time relay has been turned on
	uint32_t	relayEpoch;		//!< Number of seconds relay has been active
	uint32_t	loadCycles;		//!< Number of time active load has been detected
	uint32_t	loadEpoch;		//!< Number of seconds load has been active
} csEmtrCycles_t;


/**
 * \brief Relay-level status information
 *
 * See \ref csEmtrDrvGetRelayStatus
 *
 */
typedef struct {
	bool				isPlugged;		//!< Plug detected
	bool				relayActive;	//!< Present active/inactive state of relay
	uint32_t			relayTime;		//!< Number of seconds since boot relay has been active/inactive
	bool				loadActive;		//!< Present active/inactive state of load
	uint32_t			loadTime;		//!< Number of seconds since boot load has been active/inactive
	bool				overload;		//!< true if relay is presently in overload state
	csEmtrInstEnergy_t	instEnergy;		//!< Instant energy values
	csEmtrCycles_t		cycles;			//!< Cumulative relay and load cycles
	uint32_t			dWattHours;		//!< Cumulative units of 0.1 Watt-Hours
} csEmtrSockStatus_t;


/**
 * \brief High Current Circuit Interruption threshold
 *
 * These set the level at which the EMTR will automatically turn off the relay
 * when the HCCI current threshold is sustained for 0.3 seconds or more.
 *
 * These values map to EMTR firmware, do not change them!
 *
 */
typedef enum {
	csEmtrHcci_100A	= 0,	//!< 100 Amps
	csEmtrHcci_80A	= 1,	//!<  80 Amps
	csEmtrHcci_60A	= 2,	//!<  60 Amps
	csEmtrHcci_40A	= 3,	//!<  40 Amps
	csEmtrHcci_20A	= 4		//!<  20 Amps
} csEmtrHcci_t;


typedef void *	csEmtrDrvHandle_t;

/**
 * \brief Meta data passed to callback functions
 */
typedef struct {
	csEmtrDrvHandle_t	drvHandle;
	uint32_t			curTime;
	void *				privData;
} csEmtrDrvCbData_t;


/**
 * \brief device-level flags
 */
#define CS_EMTR_DEV_FLAG_FACTORY_RESET	(1 << 0)	//!< Initiate a factory reset

/**
 * \brief socket-level flags
 */
#define CS_EMTR_SOCK_FLAG_RELAY			(1 << 0)	//!< Relay on
#define CS_EMTR_SOCK_FLAG_PLUG			(1 << 1)	//!< Plug detected
#define CS_EMTR_SOCK_FLAG_LOAD			(1 << 2)	//!< Load detected
#define CS_EMTR_SOCK_FLAG_OVRLD			(1 << 3)	//!< Current overload detected


/**
 * \brief Callback function to read device-level status
 */
typedef esp_err_t (* csEmtrDrvCbDevStatus_t)(
	csEmtrDrvCbData_t *		cbData,			//!< Pointer to callback data structure
	uint32_t *				devFlags,		//!< Device-level flags
	uint32_t *				sockFlags,		//!< Array of flag words, one for each socket
	csEmtrDeviceStatus_t *	devStatus
);


/**
 * \brief Callback function to read instant energy for all sockets
 */
typedef esp_err_t (* csEmtrDrvCbSockStatus_t)(
	csEmtrDrvCbData_t *		cbData,
	csEmtrSockStatus_t *	status,
	uint8_t					maxSock
);


/**
 * \brief Callback function to read load detection hi/lo set points
 */
typedef esp_err_t (* csEmtrDrvCbGetLoadDetect_t)(
	csEmtrDrvCbData_t *	cbData,
	csEmtrSockNum_t		sockNum,
	uint16_t *			hiVal,
	uint16_t *			loVal
);


/**
 * \brief Callback function to set load detection hysteresis
 */
typedef esp_err_t (* csEmtrDrvCbSetLoadDetect_t)(
	csEmtrDrvCbData_t *	cbData,
	csEmtrSockNum_t		sockNum,
	uint16_t			hiVal,
	uint16_t			loVal
);


/**
 * \brief Callback function to get HCCI level
 */
typedef esp_err_t (* csEmtrDrvCbGetHcci_t)(
	csEmtrDrvCbData_t *	cbData,
	csEmtrSockNum_t		sockNum,
	csEmtrHcci_t *		value
);


/**
 * \brief Callback function to set HCCI level
 */
typedef esp_err_t (* csEmtrDrvCbSetHcci_t)(
	csEmtrDrvCbData_t *	cbData,
	csEmtrSockNum_t		sockNum,
	csEmtrHcci_t		value
);


/**
 *
 * This is passed by reference to \ref csEmtrDrvInit
 *
 * The array pointed to by info must have an entry for each socket
 */
typedef struct {
	char			appTag;
	struct {
		uint64_t	plugDetect : 1;
		uint64_t	loadDetect : 1;
		uint64_t	hcci       : 1;
		uint64_t	calData    : 1;
	} features;
	uint16_t					resetDelayMs;	//!< Number of milliseconds to delay after driving reset
	uint8_t						numSockets;		//!< Number of sockets defined by the array
	csEmtrSockInfo_t *			sockInfo;		//!< Pointer to an array of socket information structures
	uint8_t						numAccChan;		//!< Number of accumulator channels to allocate
	csEmtrUartConf_t			uartConf;		//!< Configure the command UART channel
	int8_t						gpioEmtrRst;	//!< GPIO pin for driving the EMTR RESET line
	int							taskPrio;		//!< Priority to assign to the EMTR driver task
	const uint8_t *				fwImage;		//!< Pointer to EMTR firmware image
	void *						cbPrivate;		//!< Reference to private data to pass to the callback
	// Device-level command codes
	struct {
		uint8_t	stateGet;
		uint8_t	statusGet;
		uint8_t	xmodemStart;
	} cmd;
	// Bitmasks for device flags read from device status
	struct {
		uint8_t	factoryReset;
	} devFlag;
	// Bit masks for socket flags read from device status
	struct {
		uint8_t	relayActive;
		uint8_t	loadActive;
		uint8_t	overload;
	} sockFlag;
	// Call-back functions to application level
	struct {
		csEmtrDrvCbDevStatus_t		devStatus;		//!< Read device status
		csEmtrDrvCbSockStatus_t		sockStatus;		//!< Read socket status
		csEmtrDrvCbGetLoadDetect_t	loadDetectGet;	//!< Read load detect hi/lo set points
		csEmtrDrvCbSetLoadDetect_t	loadDetectSet;	//!< Set load detect thresholds
		csEmtrDrvCbGetHcci_t		hcciGet;		//!< Get HCCI
		csEmtrDrvCbSetHcci_t		hcciSet;		//!< Set HCCI
	} cbFunc;
} csEmtrDrvConf_t;

typedef struct {
	struct {
		uint32_t spy    : 1;
		uint32_t noResp : 1;
	} flags;
	uint64_t	timeoutMs;
} csEmtrDrvCmdOpt_t;

#define csEmtrDrvCmdOptDefault() { \
	.flags = {			\
		.spy    = 0,	\
		.noResp = 0		\
	},					\
	.timeoutMs = 5000	\
}

/**
 * \brief Configure and initialize the EMTR driver, and update the EMTR firmware if needed
 *
 * This will initialize the driver and test for the presence of the EMTR and read the
 * version numbers for the EMTR boot loader and EMTR application. If there is a newer
 * firmware available for the EMTR it will be programmed during the initialization. In
 * this case there may be a significant delay before this API returns.
 *
 * \param [in] conf Pointer to configuration
 *
 * \return ESP_OK On success
 * \return ESP_ERR_INVALID_ARG Null pointer passed for conf or if conf content is not valid
 * \return ESP_ERR_NO_MEM Unable to allocate memory
 * \return ESP_FAIL Other error
 *
 */
esp_err_t csEmtrDrvInit(const csEmtrDrvConf_t * conf);


/**
 * \brief Start the EMTR driver
 *
 * \return ESP_OK On success
 * \return ESP_ERR_NO_MEM Unable to allocate memory
 * \return ESP_FAIL Other error
 *
 * \ref csEmtrDrvInit must be called prior to this call
 *
 */
esp_err_t csEmtrDrvStart(void);


esp_err_t csEmtrDrvStop(void);

/**
 * \brief Check if EMTR driver is running
 */
bool csEmtrDrvIsRunning(void);


/**
 * \brief Get EMTR boot loader version
 */
const char * csEmtrDrvBlVersion(void);


/**
 * \brief Get EMTR firmware version
 */
const char * csEmtrDrvFwVersion(void);


/**
 * \brief Read the device-level status
 *
 * \param [out] Pointer to structure to receive the device status
 *
 * \return ESP_OK On success
 * \return ESP_ERR_INVALID_ARG If NULL is passed for ret
 * \return ESP_FAIL On other errors
 *
 */
esp_err_t csEmtrDrvDeviceStatus(csEmtrDeviceStatus_t * ret);


/**
 * \brief Read the status for the selected relay
 *
 * \param [in] sockNum select the socket
 * \param [out] Pointer to structure to receive the socket status
 *
 * \return ESP_OK On success
 * \return ESP_ERR_INVALID_ARG If sockNum is out of range
 * \return ESP_ERR_INVALID_ARG If NULL is passed for ret
 * \return ESP_FAIL On other errors
 *
 */
esp_err_t csEmtrDrvSockStatus(csEmtrSockNum_t sockNum, csEmtrSockStatus_t * ret);


/**
 * \brief Read an energy accumulator
 *
 * \param [in] sockNum Select the socket
 * \param [in] chan Select the accumulator channel
 * \param [out] acc Pointer to structure to receive accumulator data
 * \param [in] reset Set true to clear the accumulator after reading
 *
 * \return ESP_OK Success
 * \return ESP_ERR_INVALID_ARG Channel number out of range or acc is NULL
 * \return ESP_ERR_INVALID_STATE Driver not running
 *
 */
esp_err_t csEmtrDrvReadAccumulator(
	csEmtrSockNum_t		sockNum,
	uint8_t				chan,
	csEmtrAccEnergy_t *	acc,
	bool				reset
);


/**
 * \brief Reset an energy accumulator
 *
 * \param [in] sockNum Select the socket
 * \param [in] chan Select the accumulator channel
 *
 * \return ESP_OK Success
 * \return ESP_ERR_INVALID_ARG Channel number out of range
 * \return ESP_ERR_INVALID_STATE Driver not running
 *
 */
esp_err_t csEmtrDrvResetAccumulator(csEmtrSockNum_t sockNum, uint8_t chan);


/**
 * \brief Turn on or off the relay for the selected socket
 *
 * \param [in] sockNum select the socket
 * \param [in] active Set true to turn on the relay, false to turn off the relay
 *
 * \return ESP_OK On success
 * \return ESP_ERR_INVALID_ARG If sockNum is out of range
 * \return ESP_ERR_INVALID_ARG If NULL is passed for ret
 * \return ESP_FAIL On other errors
 *
 */
esp_err_t csEmtrDrvSetRelay(csEmtrSockNum_t sockNum, bool active);


/**
 * \brief Get the current active/inactive relay state
 *
 * \param [in] sockNum select the socket
 *
 * \return true relay is active
 * \return false relay is active or, bad sockNum was given.
 *
 */
bool csEmtrDrvRelayIsActive(csEmtrSockNum_t sockNum);


/**
 * \brief Get the current active/inactive load state
 *
 * \param [in] sockNum select the socket
 *
 * \return true An active load is detected
 * \return false No active load detected or, bad sockNum was given.
 *
 */
bool csEmtrDrvLoadIsActive(csEmtrSockNum_t sockNum);


/**
 * \brief Get the load detection high/low hysteresis values
 *
 * \note This is at present not available for the CS-IWO
 *
 * \param [in] sockNum select the socket
 * \param [out] hiVal Pointer to where high value will be copied (milliamps)
 * \param [out] loVal Pointer to where low value will be copied (milliamps)
 *
 * \return ESP_OK On success
 * \return ESP_ERR_INVALID_ARG If sockNum is out of range
 * \return ESP_ERR_INVALID_ARG If hiVal or loVal are NULL
 * \return ESP_ERR_NOT_SUPPORTED This function is not supported
 *
 */
esp_err_t csEmtrDrvGetLoadDetect(csEmtrSockNum_t sockNum, uint16_t * hiVal, uint16_t * loVal);


/**
 * \brief Set the load detection high/low hysteresis values
 *
 * \note This is at present not available for the CS-IWO
 *
 * \param [in] sockNum select the socket
 * \param [in] hiVal High value (milliamps)
 * \param [in] loVal Low value (milliamps)
 *
 * \return ESP_OK On success
 * \return ESP_ERR_INVALID_ARG If sockNum is out of range
 * \return ESP_ERR_INVALID_ARG If hiVal or loVal are not valid
 * \return ESP_ERR_NOT_SUPPORTED This function is not supported
 *
 * loVal must be 10 or higher
 * hiVal must be greater than loVal by at least 10
 *
 * Based on instantaneous Amps:
 * When the value rises above hiVal, the load is considered to be active
 * When the value drops below loVal, the load is considered to be inactive
 * When the value is between hiVal and loVal, the load is considered to be in the last determined state
 *
 */
esp_err_t csEmtrDrvSetLoadDetect(csEmtrSockNum_t sockNum, uint16_t hiVal, uint16_t loVal);


/**
 * \brief Get the high current circuit interruption threshold (HCCI)
 *
 * \note This is at present not available for the CS-IWO
 *
 * \param [in] sockNum select the socket
 * \param [out] ret Pointer to variable to receive the HCCI value
 *
 * \return ESP_OK On success
 * \return ESP_ERR_INVALID_ARG sockNum is out of range
 * \return ESP_ERR_INVALID_ARG ret is a NULL pointer
 * \return ESP_ERR_NOT_SUPPORTED This function is not supported
 *
 */
esp_err_t csEmtrDrvGetHcci(csEmtrSockNum_t sockNum, csEmtrHcci_t * ret);


/**
 * \brief Set the high current circuit interruption threshold (HCCI)
 *
 * \note This is at present not available for the CS-IWO
 *
 * \param [in] sockNum select the socket
 * \param [in] threshold HCCI threshold
 *
 * \return ESP_OK On success
 * \return ESP_ERR_INVALID_ARG sockNum is out of range
 * \return ESP_ERR_INVALID_ARG HCCI threshold is out of range
 * \return ESP_ERR_NOT_SUPPORTED This function is not supported
 *
 */
esp_err_t csEmtrDrvSetHcci(csEmtrSockNum_t sockNum, csEmtrHcci_t threshold);


/**
 * \brief Read calibration data
 *
 * \param [in] sockNum Select socket to read
 * \param [out] buf Buffer to receive calibration data, must be at least 128 bytes
 * \param [in,out] bufSz On entry the size of the buffer, on exit the number of bytes returned
 *
 * \return ESP_OK Success
 * \return ESP_ERR_INVALID_ARG sockNum out of range, NULL passed for buf or bufSZ, or bufSz too small
 * \return ESP_ERR_INVALID_STATE EMTR driver not running
 * \return * Other error
 *
 */
esp_err_t csEmtrDrvGetCalData(csEmtrSockNum_t sockNum, uint8_t * buf, int * bufSz);


/**
 * \brief Send a command to the EMTR and optionally read back response data
 *
 * \param [in] cmd Command code
 * \param [in] payload Pointer to 4-byte array of command parameters, may be NULL if no parameters are required.
 * \param [out] retBuf Pointer to buffer to receive response data, NULL is no data expected
 * \param [in,out] retLen Pointer to length. On entry this must be set to the size of the buffer, on
 * exit this will hold the number of bytes returned. Ignored if retBuf is NULL.
 *
 * \return ESP_OK On success
 * \return ESP_ERR_INVALID_STATE The driver is not in a state to process commands
 * \return (other) Some other error
 *
 */
esp_err_t csEmtrDrvCommand(uint8_t cmd, uint8_t * payload, uint8_t * retBuf, int * retLen);

esp_err_t csEmtrDrvCommandOpt(
	uint8_t				cmd,
	uint8_t *			payload,
	uint8_t *			retBuf,
	int *				retLen,
	csEmtrDrvCmdOpt_t *	opt
);


/**
 * \brief From an EMTR driver callback function, send a command to the EMTR
 *
 * \param [in] handle Driver handle passed to the callback
 * \param [in] cmd Command code
 * \param [in] payload Pointer to 4-byte array of command parameters, may be NULL if no parameters are required.
 * \param [out] retBuf Pointer to buffer to receive response data, NULL is no data expected
 * \param [in,out] retLen Pointer to length. On entry this must be set to the size of the buffer, on
 * exit this will hold the number of bytes returned. Ignored if retBuf is NULL.
 *
 * An EMTR driver callback function is executing within the context of a protected EMTR
 * transaction, meaning the driver mutex is locked. So a callback function must use this
 * function when sending commands to the EMTR to avoid mutex deadlock.
 *
 * \return ESP_OK On success
 * \return ESP_ERR_INVALID_STATE The driver is not in a state to process commands
 * \return (other) Some other error
 *
 */
esp_err_t csEmtrDrvCmdFromCb(
	csEmtrDrvHandle_t	handle,
	uint8_t				cmd,
	uint8_t *			payload,
	uint8_t *			retBuf,
	int *				retLen
);


/**
 * \brief From an EMTR driver callback function, send an event notification
 *
 * \param [in] handle Driver handle passed to the callback
 * \param [in] evtCode Event code
 * \param [in] evtData Pointer to event-specific data, may be null if data not required
 *
 */
void csEmtrDrvEventFromCb(
	csEmtrDrvHandle_t	handle,
	csEmtrEvtCode_t		evtCode,
	csEmtrEvtData_t *	evtData
);


/**
 * \brief Return the text string for an EMTR event code
 *
 * \param evtCode One of the defined EMTR event codes
 *
 * \return Pointer to a constant string of the event name
 * \return "Undefined" if event code is not recognized
 *
 */
const char * csEmtrDrvEventStr(csEmtrEvtCode_t evtCode);


#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_CS_DRIVER_INCLUDE_CS_EMTR_DRV_H_ */
