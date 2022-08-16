/*
********************************************************************************
* FILENAME:   xmodem_snd.c
*
* REVISION HISTORY:
*    REV LEVEL:   1.00
*    DATE:        2010-01-16
*    REASON:      Initial Release version of the module.
*
* MODULE DESCRIPTION:
*   Implementation of XMODEM for sending a file to another computer
*
* COPYRIGHT 2010 Grid Connect Inc.
* All Rights Reserved
* Redistribution or unauthorized use without expressed written agreement
* from Grid Connect is forbidden.
*
********************************************************************************
*/


/*
********************************************************************************
* INCLUDES Section
********************************************************************************
*/
#include "xmodem.h"

// Comment out the MOD_NAME line to disable debug prints from this file
#define MOD_NAME	"xmodem"
#include "mod_debug.h"


/*
********************************************************************************
* DEFINES Section
********************************************************************************
*/

// XMODEM control characters
#define XMCTL_SOH		(0x01)		// Start of 128-byte block
#define XMCTL_STX		(0x02)		// Start of 1024-byte block
#define XMCTL_EOT		(0x04)		// End of tranfer mark
#define XMCTL_ACK		(0x06)		// Acknowledge
#define XMCTL_NAK		(0x15)		// Negative-ACK
#define XMCTL_CAN		(0x18)		// Cancel
#define XMCTL_GO		(XMCTL_NAK)	// Block checksum method
#define XMCTL_GOCRC		('C')		// Block CRC method

// Block validation modes
#define XMRM_XMODEM				(1)
#define XMRM_XMODEM_CRC			(2)

// Xmodem sender states
#define XMST_IDLE				(0)
#define XMST_RUN				(1)
#define XMST_ERR				(2)

// Size of XMODEM data buffer
#define XM_BUF_SIZE			(1024)

/*
********************************************************************************
* TYPEDEFS Section
********************************************************************************
*/

// Control structure for the XMODEM sender state machine
typedef struct {
	uart_port_t		uart;
	uint8_t			state;		// State of the xmodem engine
	uint16_t		blockSize;	// Size of Xmodem transfer - 128 or 1024
	uint16_t		wrIdx;		// Next position to write in the buffer
	uint8_t			blockNum;	// Number of block currently being sent
	uint16_t		sendMode;	// X-Modem receive mode: CRC or original
	uint8_t *		pBuf;		// Pointer to Xmodem buffer
} xmCtrl_t;


/*
********************************************************************************
* PROTOTYPES Section - external functions
********************************************************************************
*/


/*
********************************************************************************
* PROTOTYPES Section - internal functions
********************************************************************************
*/
static int	xmSendBlock(xmCtrl_t * pCtrl, uint8_t * pData);

// IO abstraction functions
static int		ioWaitChar(uart_port_t uartDev, uint32_t waitMs);
static void		ioPutChar(uart_port_t uartDev, uint8_t c);


/*
********************************************************************************
* PORTING section - map xmodem functions to application-specific functions
********************************************************************************
*/

// Map generic xmodem IO and utility functions

// xmWaitChar
//   description:
//      Check for presence of a byte of data ready to be received. If no data
//      is ready, wait for a time period for one to be received.
//   parameters:
//     xm = pointer to XMRCV_T
//     t  = Number of milliseconds to wait, 0 = wait forever
//  returns:
//     int: -1 if timeout expired, 0-255 received byte
#define xmWaitChar(xm, t)		ioWaitChar(xm->uart, t)

// xmPutChar
//   description:
//      Output a single character to the IO port.
//   parameters:
//     xm = pointer to XMRCV_T
//     c  = character to be transmitted
//  returns:
//     none
#define xmPutChar(xm, c)		ioPutChar(xm->uart, c)

// xmDelayMs
//   description:
//      Delay for a period of milliseconds.
//   parameters:
//     xm = pointer to XMRCV_T
//     t  = number of milliseconds to delay
//  returns:
//     none
#define xmDelayMs(xm, t)		vTaskDelay(pdMS_TO_TICKS(t))

// xmMemAllocCtlStruct
//   description:
//      Allocate memory for xmodem engine control structure.
//   parameters:
//      n  = number of bytes to allocate
//   returns:
//      pointer to memory block of at least 's' bytes in size
// (The gridARM bootrom implementation uses a statically allocated structure)
#define xmMemAllocCtlStruct(n)	(xmCtrl_t *)cs_heap_calloc(1, sizeof(xmCtrl_t))

// xmMemAllocRcvBuf
//   description:
//      Allocate memory for xmodem receive buffer
//   parameters:
//      n  = number of bytes to allocate
//   returns:
//      pointer to memory block of at least 's' bytes in size
// (The gridARM bootrom implementation uses a statically allocated array)
#define xmMemAllocRcvBuf(sz)		(uint8_t *)cs_heap_calloc(sz, 1)

// xmMemFree
//   description:
//      Release memory block that was allocated by one of the xmodem
//      memory allocation functions.
//   parameters:
//      p  = pointer to block of memory to be released.
//   returns:
//      none
// (The gridARM bootrom implementation uses statically allocated memory
//  so this function does nothing)
#define xmMemFree(p)				cs_heap_free(p)

#define xmUptimeMs()				((uint32_t)(esp_timer_get_time() / 1000)

static const csXmodemCfg_t	xmDefaultCfg = {
	.blockSize = 1024
};


esp_err_t csXmSendOpen(uart_port_t uart, csXmHandle_t * pXmHandle, csXmodemCfg_t * pCfg)
{
	esp_err_t		status;
	xmCtrl_t *		pCtrl;
	bool			loop;

	xTaskGetTickCount();

	if (!pXmHandle)
		return ESP_ERR_INVALID_ARG;

	if (!pCfg) {
		pCfg = (csXmodemCfg_t *)&xmDefaultCfg;
	}

	// Allocate memory for the control structure
	if ((pCtrl = xmMemAllocCtlStruct()) == NULL) {
		return ESP_ERR_NO_MEM;
	}

	// Allocate memory for the buffer
	if ((pCtrl->pBuf = xmMemAllocRcvBuf(XM_BUF_SIZE)) == NULL) {
		status = ESP_ERR_NO_MEM;
		goto exitCtl;
	}

	pCtrl->uart = uart;

	// Make sure there's no old data floating around in the RX FIFO
	uart_flush_input(pCtrl->uart);

	// Initialize the transfer variables
	pCtrl->blockNum   = 1;
	pCtrl->blockSize  = pCfg->blockSize;
	pCtrl->wrIdx      = 0;
	pCtrl->state      = XMST_RUN;

	// Wait for transfer start character
	// 'C' means CRC will be used for block validation
	// NAK means checksum will be used for block validation
	// CAN means abort the transfer
	// Ignore other inputs
	status = ESP_OK;
	for (loop = true; loop; )
	{
		int		rdChar;

		rdChar = xmWaitChar(pCtrl, 5000);

		switch (rdChar)
		{
		case -1:
			// Timed out
			status = ESP_ERR_TIMEOUT;
			goto exitBuf;
			break;

		case XMCTL_GO:
			// Use checksum to validate block
			pCtrl->sendMode = XMRM_XMODEM;
			loop = false;
			break;

		case XMCTL_GOCRC:
			// Use CRC to validate block
			pCtrl->sendMode = XMRM_XMODEM_CRC;
			loop = false;
			break;

		case XMCTL_CAN:
			// Receiver canceled transfer
			status = ESP_FAIL;
			goto exitBuf;
			break;

		default:
			// Ignore other characters
			break;
		}
	}

	*pXmHandle = (csXmHandle_t)pCtrl;
	return ESP_OK;

exitBuf:
	xmMemFree(pCtrl->pBuf);
exitCtl:
	xmMemFree(pCtrl);

   return status;
}


esp_err_t csXmSendWrite(csXmHandle_t xmHandle, uint8_t * pData, uint16_t dataLen)
{
	xmCtrl_t *	pCtrl;

	if (!xmHandle || !pData)
		return ESP_ERR_INVALID_ARG;

	pCtrl = (xmCtrl_t *)xmHandle;

	if (pCtrl->state != XMST_RUN)
		return ESP_OK;

	while (dataLen)
	{
		// Copy data to the Xmodem buffer bytewise
		// when the buffer is full, send it to the receiver
		pCtrl->pBuf[pCtrl->wrIdx] = *pData++;

		if (++(pCtrl->wrIdx) == pCtrl->blockSize)
		{
			int		status;

			status = xmSendBlock(pCtrl, pCtrl->pBuf);
			if (ESP_OK != status) {
				pCtrl->state = XMST_ERR;
				return status;
			}
			pCtrl->wrIdx = 0;
		}

		dataLen -= 1;
	}

	return ESP_OK;
}


esp_err_t csXmSendClose(csXmHandle_t xmHandle, bool abortFlag)
{
	esp_err_t	status = ESP_OK;
	xmCtrl_t *	pCtrl;

	if (!xmHandle)
		return ESP_ERR_INVALID_ARG;

	pCtrl = (xmCtrl_t *)xmHandle;

	// If there is remainder data in the xmodem buffer, send it
	if (!abortFlag && (pCtrl->state == XMST_RUN) && (pCtrl->wrIdx > 0))
	{
		uint16_t	idx;

		// Zero-pad the remainder of the block
		for (idx = pCtrl->wrIdx; idx < pCtrl->blockSize; idx++)
			pCtrl->pBuf[idx] = 0;

		// Send the block
		(void)xmSendBlock(pCtrl, pCtrl->pBuf);
	}

	// The last block has been sent, signal end of transfer
	if (abortFlag || (pCtrl->state == XMST_ERR)) {
		int		i;

		for (i = 0; i < 5; i++) {
			xmDelayMs(pCtrl, 5);
			xmPutChar(pCtrl, XMCTL_CAN);
		}

		if (pCtrl->state == XMST_ERR)
			status = ESP_FAIL;
	} else {
		// Send an EOT, expect to get a NAK in response
		xmPutChar(pCtrl, XMCTL_EOT);
		(void)xmWaitChar(pCtrl, 5000);

		// Send another EOT, expect to get an ACK this time
		xmPutChar(pCtrl, XMCTL_EOT);
		(void)xmWaitChar(pCtrl, 5000);
	}

	// Done with the control structure, release its memory
	xmMemFree(pCtrl->pBuf);
	xmMemFree(pCtrl);

	return status;
}


/**
 * \brief Send a block of data using xmodem protocol
*/
static int xmSendBlock(xmCtrl_t * pCtrl, uint8_t * pData)
{
	union {
		uint16_t    w;
		uint8_t		b[2];
	} crc;
	uint8_t		cksum;
	uint16_t    idx;
	uint8_t		retries;

	retries = 8;
	while (retries > 0) {
		int	rdChar;

		retries -= 1;

		// Put either the 1024-byte or 128-byte start-of-block character
		if (pCtrl->blockSize == 1024)
			xmPutChar(pCtrl, XMCTL_STX);
		else
			xmPutChar(pCtrl, XMCTL_SOH);

		// Put the block number followed by its 1's complement
		xmPutChar(pCtrl, pCtrl->blockNum);
		xmPutChar(pCtrl, ~pCtrl->blockNum);

		// Send the data bytes, updating checksum and CRC for each byte
		crc.w = 0;
		cksum = 0;
		for (idx = 0; idx < pCtrl->blockSize; idx++) {
			uint8_t	u8Data = pData[idx];

			// Put out the data byte
			xmPutChar(pCtrl, u8Data);

			// Update the running checksum
			cksum += u8Data;

			// Update the running CRC
			crc.w = csXmCrc16(crc.w, u8Data);
		}

		// Put either the checksum or the CRC, depending on the mode
		if (pCtrl->sendMode == XMRM_XMODEM_CRC) {
			// Send the CRC in high-byte, low-byte order
			xmPutChar(pCtrl, crc.b[1]);
			xmPutChar(pCtrl, crc.b[0]);
		} else {
			// Put the checksum byte
			xmPutChar(pCtrl, cksum);
		}

		// Wait up to 4 seconds for reply
		rdChar = xmWaitChar(pCtrl, 4000);

		// Process the reply
		switch (rdChar)
		{
		case -1:
			// Timed out waiting for reply
			// The block was not received, send it again until retry
			// limit has been reached
			//return -WM_E_TIMEOUT;
			break;

		case XMCTL_ACK:
			// The block was acknowledged, move on to the next one
			pCtrl->blockNum += 1;
			return ESP_OK;

		case XMCTL_NAK:
			// The block was not received, send it again until retry
			// limit has been reached
			break;

		case XMCTL_CAN:
			gc_err("Receiver cancelled transfer");
			retries = 0;
			break;

		default:
			// Unexpected reply - force loop to exit
			gc_err("Unexpected response %08X", rdChar);
			//retries = 0;
			break;
		}
	}

	// If this point is reached, the retry limit has been reached
	return ESP_FAIL;
}


/**
 * \brief Wait for a character to be received from the xmodem partner
 */
static int ioWaitChar(uart_port_t uart, uint32_t waitMs)
{
	uint8_t		rdChr;

	(void)waitMs;

	while (1)
	{
		if (uart_read_bytes(uart, &rdChr, 1, pdMS_TO_TICKS(100)) == 1) {
			return (int)rdChr;
		}

		if (waitMs > 100) {
			waitMs -= 100;
		} else if (waitMs > 0) {
			break;
		}
	}

	return -1;
}


/**
 * \brief Write a character to the xmodem partner
 */
static void ioPutChar(uart_port_t uartDev, uint8_t c)
{
	// Write the character
	uart_write_bytes(uartDev, (const char *)&c, 1);

	// Impose a delay between characters
	volatile uint32_t	delayCt = 0;
	do {
		delayCt += 1;
	} while (delayCt < 5000);
}
