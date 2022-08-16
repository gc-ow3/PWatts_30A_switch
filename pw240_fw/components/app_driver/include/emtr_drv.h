/*
 * emtr_drv.h
 *
 *  Created on: Jan 18, 2019
 *      Author: wesd
 */

#ifndef COMPONENTS_CS_DRIVER_INCLUDE_EMTR_DRV_H_
#define COMPONENTS_CS_DRIVER_INCLUDE_EMTR_DRV_H_

#include "driver/uart.h"
#include "cs_common.h"
#include "event_callback.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EMTR_DRV_MAX_SOCKETS			(2)
#define PWR_SIGNATURE_NUM_SAMPLES		(1536)

// The number of bytes in a page of power signature data
#define PWR_SIGNATURE_PAGE_SZ			(128)

// Each sample uses 4 bytes:
// 2 bytes for voltage
// 2 bytes for current
#define PWR_SIGNATURE_BUF_SZ			(4 * PWR_SIGNATURE_NUM_SAMPLES)

// EMTR energy reading accumulators
#define	EMTR_ACC_NUM_CHANS		(1)
#define EMTR_ACC_CHAN_SAPIENT	(0)

typedef struct {
	uart_port_t		uart;
	int8_t			gpioUartTx;
	int8_t			gpioUartRx;
	uint32_t		baudRate;
} emtrUartConf_t;


typedef struct {
	emtrUartConf_t	uartCmd;
	int8_t			gpioEmtrRst;
	int				numSockets;
	int				taskPrio;
} emtrDrvConf_t;


/**
 * \brief Device-level status information
 *
 * See \ref emtrDrvGetDeviceStatus
 *
 */
typedef struct {
	bool		emtrCommUp;		// True if EMTR communication is active
	uint16_t	temperature;	// Degrees C
	uint32_t	epoch;			// Seconds
	uint8_t		flags;
} emtrDeviceStatus_t;


typedef struct {
	uint32_t	dVolts;			// 0.1 volt units
	uint32_t	mAmps;			// 0.001 Amp units
	uint32_t	dWatts;			// 0.1 Watt units
	uint32_t	powerFactor;	// 0..100
} emtrInstEnergy_t;


/**
 * min/max/average measurements
 */
typedef struct {
	uint32_t	min;	// Minimum value read
	uint32_t	max;	// Maximum value read
	uint32_t	avg;
	uint32_t	sampleCt;
} emtrAvgEnergy_t;


/**
 * \brief Accumulated enery readings
 */
typedef struct {
	emtrAvgEnergy_t		dVolts;			// 0.1 volt units
	emtrAvgEnergy_t		mAmps;			// 0.001 Amp units
	emtrAvgEnergy_t		dWatts;			// 0.1 Watt units
	emtrAvgEnergy_t		powerFactor;	// 0..100
} emtrAccEnergy_t;


/**
 * \brief Outlet-level status information
 *
 * See \ref emtrDrvGetOutletStatus
 *
 */
typedef struct {
	bool				isPlugged;
	bool				isOn;
	uint32_t			relayTime;
	emtrInstEnergy_t	instEnergy;
	uint64_t			dWattHours;		// 1.0 Watt-Hour units
	uint64_t			dWattsTotal;	// 0.1 Watt units
} emtrSocketStatus_t;


/**
 * \brief EMTR driver events that can be sent to registered callbacks
 *
 */
typedef enum {
	emtrEvtCode_socketOn,
	emtrEvtCode_socketOff,
	emtrEvtCode_plugInserted,
	emtrEvtCode_plugRemoved,
	emtrEvtCode_commDown,
	emtrEvtCode_commUp,
	emtrEvtCode_reset,
	emtrEvtCode_temperature,
	emtrEvtCode_epoch,
	emtrEvtCode_volts,
	emtrEvtCode_amps,
	emtrEvtCode_watts,
	emtrEvtCode_powerFactor,
	emtrEvtCode_wattHours,
	emtrEvtCode_stateTime
} emtrEvtCode_t;


typedef union {
	struct {
		int			socketNum;
	} onOff;
	struct {
		int			socketNum;
	} plug;
	struct {
		int			socketNum;
		uint32_t	value;
	} stateTime;
	struct {
		uint16_t	value;
	} temperature;
	struct {
		uint32_t	value;
	} epoch;
	struct {
		int			socketNum;
		int			value;
	} energy;
	struct {
		uint32_t	socketNum;
		uint32_t	value;
	} dWattHours;
} emtrEvtData_t;


esp_err_t emtrDrvInit(const emtrDrvConf_t * conf);

esp_err_t emtrDrvStart(void);

typedef enum {
	emtrCbId_null = 0,
	emtrCbId_device,
	emtrCbId_socket1,
	emtrCbId_socket2
} emtrCbId_t;

esp_err_t emtrDrvCallbackRegister(emtrCbId_t cbId, eventCbFunc_t cbFunc, uint32_t cbData);

esp_err_t emtrDrvCallbackUnregister(emtrCbId_t cbId, eventCbFunc_t cbFunc);

esp_err_t emtrDrvSetSocket(int outletNum, bool turnOn);

bool emtrDrvIsSocketOn(int sockNum);

bool emtrDrvIsPlugInserted(int sockNum);

bool emtrDrvIsSocketActive(int sockNum);

esp_err_t emtrDrvGetDeviceStatus(emtrDeviceStatus_t * ret);

esp_err_t emtrDrvGetAccEnergy(int sockNum, int chan, emtrAccEnergy_t * eAcc);

esp_err_t emtrDrvGetSocketStatus(int sockNum, emtrSocketStatus_t * ret);

const char * emtrDrvGetFwVersion(void);

const char * emtrDrvGetBlVersion(void);

esp_err_t emtrDrvGetSignature(
	int				socketNum,
	uint32_t *		ts,
	uint8_t *		reason,
	uint8_t *		buf,
	int				bufLen
);

const char * emtrDrvEventString(emtrEvtCode_t code);


#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_CS_DRIVER_INCLUDE_EMTR_DRV_H_ */
