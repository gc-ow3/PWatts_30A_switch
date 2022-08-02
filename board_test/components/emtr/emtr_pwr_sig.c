/*
 * emtr_pwr_sig.c
 *
 *  Created on: Jul 20, 2022
 *      Author: wesd
 */
#include <esp_err.h>
#include <esp_log.h>

#include "cs_packer.h"
#include "emtr_pwr_sig.h"

static const char TAG[] = {"pwr_sig"};

////////////////////////////////////////////////////////////////////////////////
// Convenience macros
////////////////////////////////////////////////////////////////////////////////

// Sleep for a number of milliseconds
#define	CS_SLEEP_MS(t)	vTaskDelay(pdMS_TO_TICKS(t))

// Read number of seconds since boot
#define	TIME_SEC()		(esp_timer_get_time()/1000000)
#define	TIME_MS()		(esp_timer_get_time()/1000)

// Acquire and release mutex
#define MUTEX_GET(ctrl)		xSemaphoreTake((ctrl)->mutex, portMAX_DELAY)
#define MUTEX_PUT(ctrl)		xSemaphoreGive((ctrl)->mutex)


////////////////////////////////////////////////////////////////////////////////
// Internal data types
////////////////////////////////////////////////////////////////////////////////

/**
 * \brief States for the power signature state machine
 */
typedef enum {
	pwrSigState_idle = 0,
	pwrSigState_sop,
	pwrSigState_eop,
	pwrSigState_rdLen,
	pwrSigState_recvHdr,
	pwrSigState_recvData,
	pwrSigState_cksum,
	pwrSigState_discard,
	pwrSigState_recvTestResult,
} pwrSigState_t;

#define PWR_METRICS_HDR_SZ		(32)

typedef enum {
	pwrPayloadType_sig
} pwrSigPayloadType_t;


/**
 * \brief Control structure for the power signature state machine
 */
typedef struct {
	pwrSigConf_t		conf;
	bool				isRunning;
	pwrSigState_t		state;
	uint8_t				cksum;
	uint64_t			curTimeMs;
	uint32_t			curTime;
	uint64_t			startTimeMs;
	pwrSigMeta_t		meta;
	uint8_t				recvBuf[256];
	int					rxLen;
	pwrSigPayloadType_t	payLoadType;
	int					payloadLen;
	int					hdrLen;
	uint8_t				sigHdr[PWR_SIG_HDR_SZ];
	int					sigLen;
	uint8_t				sigBuf[PWR_METRICS_HDR_SZ + PWR_SIG_MAX_SIG_SZ];
	uint32_t			sigCount;
} taskCtrl_t;


////////////////////////////////////////////////////////////////////////////////
// Local functions
////////////////////////////////////////////////////////////////////////////////
static void pwrSigTask(void * params);


////////////////////////////////////////////////////////////////////////////////
// Local variables
////////////////////////////////////////////////////////////////////////////////
static taskCtrl_t*	taskCtrl;


esp_err_t pwrSigInit(pwrSigConf_t* conf)
{
	if (taskCtrl) {
		return ESP_OK;
	}

	taskCtrl_t*	pCtrl = calloc(1, sizeof(*pCtrl));
	if (!pCtrl) {
		return ESP_ERR_NO_MEM;
	}

	// Copy configuration
	pCtrl->conf = *conf;

	uart_config_t	uCfg = {
		.baud_rate = pCtrl->conf.baudRate,
		.data_bits = UART_DATA_8_BITS,
		.parity    = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE
	};

	esp_err_t	status;

	if ((status = uart_param_config(pCtrl->conf.port, &uCfg)) != ESP_OK) {
		return status;
	}

	status = uart_set_pin(
		pCtrl->conf.port,
		UART_PIN_NO_CHANGE,	// TXD not used
		pCtrl->conf.rxGpio,
		UART_PIN_NO_CHANGE,	// RTS not used
		UART_PIN_NO_CHANGE	// CTS not used
	);
	if (ESP_OK != status) {
		return status;
	}

	int	rxBufSz = PWR_SIG_HDR_SZ + PWR_SIG_MAX_SIG_SZ;
	status = uart_driver_install(pCtrl->conf.port, rxBufSz, 0, 0, NULL, 0);
	if (ESP_OK != status) {
		return status;
	}

	taskCtrl = pCtrl;
	return ESP_OK;
}


esp_err_t pwrSigStart(void)
{
	taskCtrl_t*	pCtrl = taskCtrl;

	if (!pCtrl) {
		return ESP_ERR_INVALID_STATE;
	}

	if (pCtrl->isRunning) {
		return ESP_OK;
	}

	// Start the power signature read task
	BaseType_t	xStatus;
	xStatus = xTaskCreate(
		pwrSigTask,
		"pwrsig",
		4000,
		(void*)pCtrl,
		pCtrl->conf.taskPriority,
		NULL
	);
	if (pdPASS == xStatus) {
		//printf("pwrSigTask created\r\n");
	} else {
		ESP_LOGE(TAG, "Task create failed");
		return ESP_FAIL;
	}

	pCtrl->isRunning = true;
	return ESP_OK;
}


/**
 * \brief Return the count of received power signatures since boot
 */
esp_err_t pwrSigCount(uint32_t * ret)
{
	if (!ret) {
		return ESP_ERR_INVALID_ARG;
	}

	taskCtrl_t*	pCtrl = taskCtrl;
	if (NULL == pCtrl) {
		ESP_LOGE(TAG, "Driver not initialized");
		return ESP_ERR_INVALID_STATE;
	}
	if (!pCtrl->isRunning) {
		ESP_LOGE(TAG, "Driver not started");
		return ESP_ERR_INVALID_STATE;
	}

	*ret = pCtrl->sigCount;
	return ESP_OK;
}


/**
 * \brief Send power signature data to serial output as a hex string
 * Provided here for testing
 */
void pwrSigReadHex(int maxLen)
{
	taskCtrl_t*	pCtrl = taskCtrl;
	if (NULL == pCtrl) {
		ESP_LOGE(TAG, "Driver not initialized");
		return;
	}
	if (!pCtrl->isRunning) {
		ESP_LOGE(TAG, "Driver not started");
		return;
	}

	// Shorthand to the meta data structure
	pwrSigMeta_t *	meta = &pCtrl->meta;

	/*** ToDo

	// Pack the metrics header
	// Space was reserved in front of the signature data buffer for the header
	csPacker_t	pack;
	csPackInit(&pack, pwr->sigBuf, PWR_METRICS_HDR_SZ);
	csPackZero(&pack);

	// Version 1 header
	csPackU8(&pack, 1);
	// Reason code
	csPackU8(&pack, (uint8_t)meta->reason);
	// Water level
	csPackU8(&pack, meta->waterLevel);
	// (was GFCI level)
	csPackSkip(&pack, 1);
	// Number of samples
	csPackBEU16(&pack, meta->numSamples);
	// Two reserved bytes
	csPackSkip(&pack, 2);
	// Number of cycles the pump has run
	csPackBEU32(&pack, meta->pumpCycles);
	// Number of seconds pump has been powered
	csPackBEU32(&pack, meta->timePowered);
	// Number of seconds pump has been running
	csPackBEU32(&pack, meta->timeRunning);

	esp_err_t	status;

	if ((status = csPackStatus(&pack)) != ESP_OK) {
		ESP_LOGE(TAG, "Error %d packing metrics header", status);
		return;
	}

	// Transmit most recent power signature as a hex digit stream
	int			txLen = (PWR_METRICS_HDR_SZ + pwr->sigLen);
	uint8_t *	pData;
	int			i;

	txLen = (txLen <= maxLen) ? txLen : maxLen;

	for (i = 0, pData = pwr->sigBuf; i < txLen; i++, pData++) {
		printf("%02X", *pData);
	}
	printf("\r\n");

	***/
}


static uint32_t getInrushCurrent(taskCtrl_t * pwr)
{
	// Signature data begins after the header
	uint8_t *	samp    = &pwr->sigBuf[PWR_METRICS_HDR_SZ];
	// For each sample, two 16-bit signed values
	//   bytes 1-0 == volts ADC value
	//   bytes 3-2 == amps ADC value

	float	iLo = 0.0;
	float	iHi = 0.0;

	int	i;
	for (i = 0; i < pwr->meta.numSamples; i++, samp += 4) {

		uint16_t	iADC = ((uint16_t)(samp[2]) << 8) + ((uint16_t)(samp[3]));
		float		amps;

		if (iADC < 0x8000) {
			amps = (float)iADC / 327.68;
		} else {
			amps = -(float)(iADC ^ 0xFFFF) / 327.68;
		}

#if 0
		uint16_t	vADC = ((uint16_t)(samp[0]) << 8) + ((uint16_t)(samp[1]));
		float		volts;

		if (vADC < 0x8000) {
			volts = (float)vADC / 65.535;
		} else {
			volts = -(float)(vADC ^ 0xFFFF) / 65.535;
		}

		if (0 == i) {
			gc_dbg("v_adc, volts, i_adc, amps");
		}
		gc_dbg("  0x%04x, %3.3f, 0x%04x, %3.3f", vADC, volts, iADC, amps);
#endif

		if (amps < iLo) {
			iLo = amps;
		} else if (amps > iHi) {
			iHi = amps;
		}
	}

	float	iPP  = iHi - iLo;
	float	iRMS = (iPP / 2.0) * 0.7071;

	// In-rush current is RMS value of the peak-to-peak current
	return (uint32_t)(iRMS * 1000.0);
}


/**
 * \brief Unpack meta data from the power signature header
 *   offset  len
 *        0    1  Reason: 0 = pump off, 1 = pump on, 2 = demand
 *        1    4  Pump cycle count, 32-bit, big-endian
 *        5    4  Relay cycle count, 32-bit, big-endian
 *        9    4  Cycle run time, seconds, valid only on pump off
 *       13    2  Average amps, units of 0.001 amps, valid only on pump off
 *       15    2  Average volts, units of 0.1 volts, valid only on pump off
 *       17    1  Average power factor, valid only on pump off
 *       18    1  Temperature, degrees C, valid only on pump off
 *       19    4  Pump epoch, 32-bit, big-endian
 *       23    4  Pump run time, 32-bit, big-endian, valid only on pump off
 *       27    1  Water level 0..5
 *       28    1  GFCI -- Deprecated
 *       29    1  Sample resolution
 *                0 12.0 KHz
 *                1  6.0 KHz
 *                2  4.0 KHz
 *                3  3.0 KHz
 *                4  2.4 KHz
 *                5  2.0 KHz
 */
static void pwrSigUnpackMeta(taskCtrl_t * pCtrl)
{
	// Shorthand to the meta data structure
	pwrSigMeta_t *	meta = &pCtrl->meta;

	csPacker_t	unpack;
	csPackInit(&unpack, pCtrl->sigHdr, pCtrl->hdrLen);

	/* ToDo

	uint8_t	u8Temp;

	// Reason code
	csUnpackU8(&unpack, &u8Temp);
	meta->reason = (wwPwrSigReason_t)u8Temp;
	// Pump cycles
	csUnpackBEU32(&unpack, &meta->pumpCycles);
	// Relay cycles
	csUnpackBEU32(&unpack, &meta->relayCycles);
	// These are valid only on pump-off
	if (wwPwrSigReason_pumpOff == meta->reason) {
		// Cycle length
		csUnpackBEU32(&unpack, &meta->cycleLength);
		// Average amps
		csUnpackBEU16(&unpack, &meta->mAmps);
		// Average volts
		csUnpackBEU16(&unpack, &meta->dVolts);
		// Average power factor
		csUnpackU8(&unpack, &meta->pFactor);
		// Temperature
		csUnpackU8(&unpack, &meta->temperature);
	} else {
		csUnpackSkip(&unpack, 10);

		meta->cycleLength = 0;
		meta->mAmps       = 0;
		meta->dVolts      = 0;
		meta->pFactor     = 0;
		meta->temperature = 0;
	}
	// Total time powered
	csUnpackBEU32(&unpack, &meta->timePowered);
	// Total run time
	csUnpackBEU32(&unpack, &meta->timeRunning);
	// Water level
	csUnpackU8(&unpack, &meta->waterLevel);
	// GFCI level -- deprecated
	csUnpackSkip(&unpack, 1);
	// Resolution
	csUnpackU8(&unpack, &meta->resolution);

	// Derive the sample count from the size of the data
	meta->numSamples = pCtrl->sigLen / PWR_SIG_SMP_SZ;

	*/

	// For "on" event, determine inrush current
	if (pwrSigReason_on == pCtrl->meta.reason) {
		meta->mAmpsInrush = getInrushCurrent(pCtrl);
	}

#if 0
	gc_dbg("pwrSig meta data");
	gc_dbg("  Reason          : %s", pwrSigReasonStr(meta->reason));
	gc_dbg("  Pump cycles     : %u", meta->pumpCycles);
	gc_dbg("  Relay cycles    : %u", meta->relayCycles);
	gc_dbg("  Samples         : %u", meta->numSamples);
	gc_dbg("  Resolution      : %u", meta->resolution);
	gc_dbg("  Seconds powered : %u", meta->timePowered);
	gc_dbg("  Seconds running : %u", meta->timeRunning);
	gc_dbg("  Water level     : %u", meta->waterLevel);
	if (pwrSigReason_on == meta->reason) {
		gc_dbg("  Inrush amps     : %0.3f", (double)meta->mAmpsInrush/1000.0);
	} else if (pwrSigReason_off == meta->reason) {
		gc_dbg("  Cycle length    : %u", meta->cycleLength);
		gc_dbg("  Average volts   : %0.2f", (double)meta->dVolts/10.0);
		gc_dbg("  Average amps    : %0.2f", (double)meta->mAmps/1000.0);
		gc_dbg("  Average pFactor : %u", meta->pFactor);
		gc_dbg("  Temperature     : %u", meta->temperature);
	}
#endif
}

// EMTR message framing characters
#define MSG_CHAR_SOP	((uint8_t)0x1B)
#define MSG_CHAR_EOP	((uint8_t)0x0A)

/**
 * \brief Process received data from the EMTR power signature channel
 *
 * Form of the data stream for power signature:
 *   Offset  Length  Content
 *        0       1  0x1B : Start of packet
 *        1       2  'S'  : Start of signature data
 *        3       2  Payload_size, 16-bit, big-endian
 *                   ---- header start ----
 *        5       1  Reason: 0 = pump off, 1 = pump on, 2 = demand
 *        6       4  Pump cycle count, 32-bit, big-endian
 *       10       4  Relay cycle count, 32-bit, big-endian
 *       14       4  Cycle length
 *       18       2  Average amps
 *       20       2  Average volts
 *       22       1  Average power factor
 *       23       1  board temperature
 *       24       4  Pump epoch, 32-bit, big-endian
 *       28       4  Pump run time, 32-bit, big-endian
 *       32       1  Water level 0..5
 *       33       1  GFCI leak level 0..5
 *       34       1  Sample resolution 0..5 == 12 KHz .. 2 KHz
 *                   ---- header end ----
 *       35  (payload_size - header size) power data, 8 bytes per sample
 *        *       1  Check digit
 *        *       1  0x0A : end of packet
 *
 * Form of the data stream for system test result
 *   Offset  Length  Content
 *        0       1  0x1B : Start of packet
 *        1       2  'T'  : Start of System Test result
 *        3       2  Payload_size, 16-bit, big-endian
 *
 *        // ToDo TBD
 *
 *        *       1  Check digit
 *        *       1  0x0A : end of packet
 */
static void handlePwrData(taskCtrl_t* pCtrl, uint8_t * data, int len)
{
	uint64_t	curTimeMs = TIME_MS();

	if (pwrSigState_idle != pCtrl->state) {
		if (curTimeMs - pCtrl->startTimeMs >= 500) {
			ESP_LOGD(TAG, "Previous transfer canceled");
			pCtrl->state = pwrSigState_idle;
		}
	}

	// Handle data in a byte-wise manner to drive the state machine
	int	i;
	for (i = 0; i < len; i++, data++) {
		switch (pCtrl->state)
		{
		case pwrSigState_idle:
			// Discard data until start of a new message is received
			if (MSG_CHAR_SOP == *data) {
				pCtrl->cksum       = 0;
				pCtrl->state       = pwrSigState_sop;
				pCtrl->startTimeMs = curTimeMs;
			}
			break;

		case pwrSigState_sop:
			// Expecting 2nd character to be 'S' (signature) or 'T' (system test)
			// Update the checksum
			pCtrl->cksum ^= *data;

			// Set up to read payload length
			pCtrl->rxLen      = 0;
			pCtrl->payloadLen = 0;
			pCtrl->state      = pwrSigState_rdLen;

			switch (*data)
			{
			case 'S':
				pCtrl->payLoadType = pwrPayloadType_sig;
				break;

			default:
				ESP_LOGE(TAG, "pwrSig: Expected 'S', got 0x%02x", *data);
				pCtrl->state = pwrSigState_idle;
				break;
			}
			break;

		case pwrSigState_rdLen:
			// Update the checksum
			pCtrl->cksum ^= *data;

			// Update the payload length
			pCtrl->payloadLen = (pCtrl->payloadLen << 8) + (int)*data;
			if (++pCtrl->rxLen == 2) {
				//ESP_LOGD(TAG, "Payload length = %d", pwr->payloadLen);

				switch (pCtrl->payLoadType)
				{
				case pwrPayloadType_sig:
					// Set up to receive the power signature header
					if (pCtrl->payloadLen > PWR_SIG_HDR_SZ) {
						pCtrl->hdrLen = 0;
						pCtrl->state  = pwrSigState_recvHdr;
					} else {
						ESP_LOGE(TAG, "pwrSig: Payload length (%d) too small", pCtrl->payloadLen);
						pCtrl->state = pwrSigState_idle;
					}
					break;

				default:
					ESP_LOGE(TAG, "Payload type %d not supported", pCtrl->payLoadType);
					pCtrl->state = pwrSigState_idle;
					break;
				}
			}
			break;

		case pwrSigState_recvHdr:
			// Update the checksum
			pCtrl->cksum ^= *data;

			// Store the header data
			pCtrl->sigHdr[pCtrl->hdrLen++] = *data;
			if (pCtrl->hdrLen == PWR_SIG_HDR_SZ) {
				// Set up to receive signature data
				// payload length minus header length and checksum
				pCtrl->sigLen = pCtrl->payloadLen - PWR_SIG_HDR_SZ - 1;

				//ESP_LOGD(TAG, "Signature length = %d", pwr->sigLen);

				pCtrl->rxLen = 0;
				pCtrl->state = pwrSigState_recvData;
			}
			break;

		case pwrSigState_recvData:
			if (pCtrl->rxLen < sizeof(pCtrl->sigBuf)) {
				// Update the checksum
				pCtrl->cksum ^= *data;

				// Store the data, reserving space for the metrics header
				pCtrl->sigBuf[PWR_METRICS_HDR_SZ + pCtrl->rxLen] = *data;
				if (++pCtrl->rxLen == pCtrl->sigLen) {
					// End of power data, the next byte is checksum
					pCtrl->state = pwrSigState_cksum;
				}
			} else {
				ESP_LOGE(TAG, "Power signature buffer overflow");
				pCtrl->state = pwrSigState_discard;
			}
			break;

		case pwrSigState_recvTestResult:
			// Use sigBuf to receive the test result payload
			if (pCtrl->rxLen < sizeof(pCtrl->sigBuf)) {
				// Update the checksum
				pCtrl->cksum ^= *data;

				pCtrl->sigBuf[pCtrl->rxLen] = *data;
				if (++pCtrl->rxLen == pCtrl->payloadLen) {
					// End of data, the next byte is checksum
					pCtrl->state = pwrSigState_cksum;
				}
			} else {
				ESP_LOGE(TAG, "pwrSig: Power signature buffer overflow");
				pCtrl->state = pwrSigState_discard;
			}
			break;

		case pwrSigState_cksum:
			// Validate checksum
			if (pCtrl->cksum == *data) {
				// Next (and final) byte is expected to be EOP
				pCtrl->state = pwrSigState_eop;
			} else {
				ESP_LOGE(TAG, "Checksum: expected %02X, got %02X", *data, pCtrl->cksum);
				pCtrl->state = pwrSigState_idle;
			}
			break;

		case pwrSigState_eop:
			// Expecting this to be EOP
			if (MSG_CHAR_EOP == *data) {
				switch (pCtrl->payLoadType)
				{
				case pwrPayloadType_sig:
					// Count number of successful signatures received
					pCtrl->sigCount += 1;
					// Unpack the meta data from the header
					pwrSigUnpackMeta(pCtrl);
					// Send the signature to the eagerly awaiting client
					if (pCtrl->conf.cbFunc) {
						pCtrl->conf.cbFunc(&pCtrl->meta, pCtrl->sigBuf, pCtrl->rxLen, pCtrl->conf.cbData);
					}
					break;

				default:
					ESP_LOGE(TAG, "Payload type %d not supported", pCtrl->payLoadType);
					break;
				}
			} else {
				ESP_LOGE(TAG, "Expected EOP, got 0x%02x", *data);
			}

			pCtrl->state = pwrSigState_idle;
			break;

		case pwrSigState_discard:
			// Discard data until end of message
			break;

		default:
			ESP_LOGE(TAG, "Unhandled power data state (%d)", pCtrl->state);
			pCtrl->state = pwrSigState_idle;
			break;
		}
	}
}


static void pwrSigTask(void * params)
{
	taskCtrl_t*	pCtrl  = (taskCtrl_t*)params;

	pCtrl->state = pwrSigState_idle;

	while (1)
	{
		CS_SLEEP_MS(20);
		pCtrl->curTimeMs = TIME_MS();
		pCtrl->curTime = (uint32_t)(pCtrl->curTimeMs / 1000);

		// Check for incoming from the data channel
		int	rdLen;
		do {
			rdLen = uart_read_bytes(pCtrl->conf.port, pCtrl->recvBuf, sizeof(pCtrl->recvBuf), 0);
			handlePwrData(pCtrl, pCtrl->recvBuf, rdLen);
		} while (rdLen > 0);
	}
}
