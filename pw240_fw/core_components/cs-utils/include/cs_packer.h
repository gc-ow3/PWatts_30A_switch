/*
 * cs_packer.h
 *
 *  Created on: Apr 23, 2020
 *      Author: wesd
 */

#ifndef COMPONENTS_CS_UTILS_INCLUDE_CS_PACKER_H_
#define COMPONENTS_CS_UTILS_INCLUDE_CS_PACKER_H_

#include "cs_common.h"

#ifdef __cplusplus
extern "C" {
#endif


typedef struct {
	uint8_t *	array;
	int			arraySz;
	int			idx;
	esp_err_t	status;
} csPacker_t;


/**
 * \brief Initialize a packer control structure
 *
 * \param [in] pack Pointer to a packer control structure
 * \param [in] array Pointer to an array to be packed or unpacked
 * \param [in] arraySz Size of the array
 *
 * \return ESP_OK Success
 * \return ESP_INVALID_ARG NULL passed for pack or array, or minus value for array size
 *
 */
esp_err_t csPackInit(csPacker_t * pack, uint8_t * array, int arraySz);


/**
 * \brief Zero the packer array
 */
esp_err_t csPackZero(csPacker_t * pack);


/**
 * \brief Return status of the pack/unpack session
 */
esp_err_t csPackStatus(csPacker_t * pack);


/**
 * \brief Return space available in the pack/unpack buffer
 *
 * \param [in] pack Pointer to a packer control structure
 *
 * \return Number of unused bytes in the array
 *
 */
int csPackUnused(csPacker_t * pack);

/**
 * \brief Skip over bytes in a packer array
 *
 * \param [in] pack Pointer to a packer control structure
 * \param [in] count Number of bytes to ship
 *
 * \return ESP_OK Operation completed successfully
 * \return ESP_INVALID_ARG NULL passed for a pointer or minus value passed for a length
 * \return ESP_ERR_INVALID_SIZE The operation would exceed the array limit
 *
 * The caller must instantiate the packer control structure and call
 * \ref csPackerInit before using the pack/unpack functions
 *
 */
esp_err_t csPackSkip(csPacker_t * pack, int count);

/**
 * \brief Pack values into a byte array
 *
 * \param [in] pack Pointer to a packer control structure
 * \param [in] inVal value to be packed
 *
 * \return ESP_OK Operation completed successfully
 * \return ESP_INVALID_ARG NULL passed for a pointer or minus value passed for a length
 * \return ESP_ERR_INVALID_SIZE The operation would exceed the array limit
 *
 * The caller must instantiate the packer control structure and call
 * \ref csPackerInit before using the pack/unpack functions
 *
 */
esp_err_t csPackU8(csPacker_t * pack, uint8_t inVal);

esp_err_t csPackStr(csPacker_t * pack, const char * inVal);

esp_err_t csPackArray(csPacker_t * pack, uint8_t * inVal, int inLen);

esp_err_t csPackBEU16(csPacker_t * pack, uint16_t inVal);

esp_err_t csPackLEU16(csPacker_t * pack, uint16_t inVal);

esp_err_t csPackBEU32(csPacker_t * pack, uint32_t inVal);

esp_err_t csPackLEU32(csPacker_t * pack, uint32_t inVal);


/**
 * \brief Unpack values from a byte array
 *
 * \param [in] pack Pointer to a packer control structure
 * \param [in] outVal pointer to variable to receive unpacked value
 *
 * \return ESP_OK Operation completed successfully
 * \return ESP_INVALID_ARG NULL passed for a pointer or minus value passed for a length
 * \return ESP_ERR_INVALID_SIZE The operation would exceed the array limit
 *
 * The caller must instantiate the packer control structure and call
 * \ref csPackerInit before using the pack/unpack functions
 *
 */
esp_err_t csUnpackU8(csPacker_t * pack, uint8_t * outVal);

esp_err_t csUnpackArray(csPacker_t * pack, uint8_t * outVal, int outLen);

esp_err_t csUnpackBEU16(csPacker_t * pack, uint16_t * outVal);

esp_err_t csUnpackLEU16(csPacker_t * pack, uint16_t * outVal);

esp_err_t csUnpackBEU32(csPacker_t * pack, uint32_t * outVal);

esp_err_t csUnpackLEU32(csPacker_t * pack, uint32_t * outVal);


#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_CS_UTILS_INCLUDE_CS_PACKER_H_ */
