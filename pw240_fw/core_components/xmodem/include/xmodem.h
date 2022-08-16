/*
********************************************************************************
* FILENAME:   xmodem.h
*
* REVISION HISTORY:
*    REV LEVEL:   1.00
*    DATE:        2010-01-15
*    REASON:      Initial Release version of the module.
*
* MODULE DESCRIPTION:
*   Defines, data types and function prototypes used by the xmodem receiver
*
* COPYRIGHT 2010 Grid Connect Inc.
* All Rights Reserved
* Redistribution or unauthorized use without expressed written agreement
* from Grid Connect is forbidden.
*
********************************************************************************
*/
#if !defined(XMODEM_H)
#define XMODEM_H

#include "driver/uart.h"
#include "cs_common.h"
#include "cs_heap.h"

#ifdef __cplusplus
extern "C" {
#endif


/*
********************************************************************************
* DEFINES Section
********************************************************************************
*/


/*
********************************************************************************
* TYPEDEFS Section
********************************************************************************
*/

typedef void *	csXmHandle_t;

typedef struct {
	int		blockSize;		// Either 128 or 1024
} csXmodemCfg_t;



//------------------------------------------------------------------------------
//         Global functions
//------------------------------------------------------------------------------

/**
 * \brief Open a xmodem session to send data
 *
 * \param [in] uart Identify the UART to use
 * \param [out] pXmHandle Pointer to handle to be used for subsequent xmodem calls
 * \param [in] pCfg Pointer to xmodem configuration structure. May be NULL in which
 * default configuration will be used
 *
 * \return ESP_OK Success
 * \return ESP_ERR_INVALID_ARG NULL handle pointer passed
 * \return ESP_ERR_NO_MEM Unable to allocate memory
 * \return ESP_FAIL Other error
 *
 */
esp_err_t	csXmSendOpen(uart_port_t uart, csXmHandle_t * pXmHandle, csXmodemCfg_t * pCfg);

/**
 * \brief Write data to the xmodem partner
 *
 * \param [in] xmHandle Handle obtained in call to csXmSendOpen
 * \param [in] pData Pointer to data to be sent
 * \param [in] dataLen Number of bytes to send
 *
 * \return ESP_OK Success
 * \return ESP_ERR_INVALID_ARG NULL handle passed
 * \return ESP_FAIL Other error
 *
 */
esp_err_t	csXmSendWrite(csXmHandle_t xmHandle, uint8_t * pData, uint16_t dataLen);

/**
 * \brief Close a xmodem session, release allocated resources
 *
 * \param [in] xmHandle Handle obtained in call to csXmSendOpen
 *
 * \return ESP_OK Success
 * \return ESP_ERR_INVALID_ARG NULL handle passed
 * \return ESP_FAIL Other error
 *
 */
esp_err_t	csXmSendClose(csXmHandle_t xmHandle, bool abortFlag);

/**
 * \brief xmodem CRC calculation
 *
 * For internal use
 *
 */
uint16_t    csXmCrc16(uint16_t crc, uint8_t data);

#ifdef __cplusplus
}
#endif

#endif

