/*
 * cs_emtr_drv_support.c
 *
 *  Created on: Apr 22, 2020
 *      Author: wesd
 */

#include "cs_packer.h"

#include <string.h>
#include <esp_err.h>

static esp_err_t _packInit(csPacker_t * pack, uint8_t * array, int arraySz)
{
	if (!pack || !array || arraySz < 0) {
		return ESP_ERR_INVALID_ARG;
	}

	pack->array   = array;
	pack->arraySz = arraySz;
	pack->idx     = 0;
	pack->status  = ESP_OK;

	return ESP_OK;
}


esp_err_t csPackInit(csPacker_t * pack, uint8_t * array, int arraySz)
{
	return _packInit(pack, array, arraySz);
}


esp_err_t csPackInitZ(csPacker_t * pack, uint8_t * array, int arraySz)
{
	esp_err_t	status;

	if ((status = _packInit(pack, array, arraySz)) == ESP_OK) {
		memset(pack->array, 0, pack->arraySz);
	}

	return status;
}


esp_err_t csPackZero(csPacker_t * pack)
{
	if (!pack) {
		return ESP_ERR_INVALID_ARG;
	}

	memset(pack->array, 0, pack->arraySz);
	return ESP_OK;
}


esp_err_t csPackStatus(csPacker_t * pack)
{
	if (!pack) {
		return ESP_ERR_INVALID_ARG;
	}
	return pack->status;
}


int csPackUnused(csPacker_t * pack)
{
	if (!pack) {
		return -1;
	}

	return pack->arraySz - pack->idx;
}


static esp_err_t _checkPack(csPacker_t * pack, int len)
{
	if (!pack || len < 0) {
		return ESP_ERR_INVALID_ARG;
	}

	if (ESP_OK != pack->status) {
		return pack->status;
	}

	if (pack->idx + len > pack->arraySz) {
		pack->status = ESP_ERR_NO_MEM;
	}

	return pack->status;
}


esp_err_t csPackSkip(csPacker_t * pack, int count)
{
	esp_err_t	status  = _checkPack(pack, count);
	if (ESP_OK != status) {
		return status;
	}

	pack->idx += count;
	return ESP_OK;
}


esp_err_t csPackU8(csPacker_t * pack, uint8_t inVal)
{
	esp_err_t	status  = _checkPack(pack, 1);
	if (ESP_OK != status) {
		return status;
	}

	pack->array[pack->idx++] = inVal;
	return ESP_OK;
}


esp_err_t csPackStr(csPacker_t * pack, const char * inVal)
{
	if (!inVal) {
		return ESP_ERR_INVALID_ARG;
	}

	int	cpyLen = strlen(inVal) + 1;

	esp_err_t	status  = _checkPack(pack, cpyLen);
	if (ESP_OK != status) {
		return status;
	}

	memcpy((pack->array + pack->idx), inVal, cpyLen);
	pack->idx += cpyLen - 1;

	return ESP_OK;
}


esp_err_t csPackArray(csPacker_t * pack, const uint8_t * inVal, int inLen)
{
	esp_err_t	status  = _checkPack(pack, inLen);
	if (ESP_OK != status) {
		return status;
	}

	memcpy((pack->array + pack->idx), inVal, inLen);
	pack->idx += inLen;

	return ESP_OK;
}


esp_err_t csPackBEI16(csPacker_t * pack, int16_t inVal)
{
	esp_err_t	status  = _checkPack(pack, 2);
	if (ESP_OK != status) {
		return status;
	}

	uint16_t	temp = (uint16_t)inVal;

	pack->array[pack->idx + 0] = (uint8_t)(temp >>  8);
	pack->array[pack->idx + 1] = (uint8_t)(temp >>  0);
	pack->idx += 2;

	return ESP_OK;
}


esp_err_t csPackBEU16(csPacker_t * pack, uint16_t inVal)
{
	esp_err_t	status  = _checkPack(pack, 2);
	if (ESP_OK != status) {
		return status;
	}

	pack->array[pack->idx + 0] = (uint8_t)(inVal >>  8);
	pack->array[pack->idx + 1] = (uint8_t)(inVal >>  0);
	pack->idx += 2;

	return ESP_OK;
}


esp_err_t csPackLEU16(csPacker_t * pack, uint16_t inVal)
{
	esp_err_t	status  = _checkPack(pack, 2);
	if (ESP_OK != status) {
		return status;
	}

	pack->array[pack->idx + 0] = (uint8_t)(inVal >>  0);
	pack->array[pack->idx + 1] = (uint8_t)(inVal >>  8);
	pack->idx += 2;

	return ESP_OK;
}


esp_err_t csPackBEU32(csPacker_t * pack, uint32_t inVal)
{
	esp_err_t	status  = _checkPack(pack, 4);
	if (ESP_OK != status) {
		return status;
	}

	pack->array[pack->idx + 0] = (uint8_t)(inVal >> 24);
	pack->array[pack->idx + 1] = (uint8_t)(inVal >> 16);
	pack->array[pack->idx + 2] = (uint8_t)(inVal >>  8);
	pack->array[pack->idx + 3] = (uint8_t)(inVal >>  0);
	pack->idx += 4;

	return ESP_OK;
}


esp_err_t csPackLEU32(csPacker_t * pack, uint32_t inVal)
{
	esp_err_t	status  = _checkPack(pack, 4);
	if (ESP_OK != status) {
		return status;
	}

	pack->array[pack->idx + 0] = (uint8_t)(inVal >>  0);
	pack->array[pack->idx + 1] = (uint8_t)(inVal >>  8);
	pack->array[pack->idx + 2] = (uint8_t)(inVal >> 16);
	pack->array[pack->idx + 3] = (uint8_t)(inVal >> 24);
	pack->idx += 4;

	return ESP_OK;
}


esp_err_t csPackFloat(csPacker_t * pack, float inVal)
{
	esp_err_t	status  = _checkPack(pack, 4);
	if (ESP_OK != status) {
		return status;
	}

	memcpy(&pack->array[pack->idx], &inVal, 4);
	pack->idx += 4;

	return ESP_OK;
}


static esp_err_t _checkUnpack(csPacker_t * pack, void * out, int len)
{
	esp_err_t	status = _checkPack(pack, len);
	if (ESP_OK != status) {
		return status;
	}

	if (!out) {
		return ESP_ERR_INVALID_ARG;
	}

	return ESP_OK;
}


esp_err_t csUnpackSkip(csPacker_t * pack, int count)
{
	esp_err_t	status  = _checkPack(pack, count);
	if (ESP_OK != status) {
		return status;
	}

	pack->idx += count;
	return ESP_OK;
}


esp_err_t csUnpackU8(csPacker_t * pack, uint8_t * outVal)
{
	esp_err_t	status  = _checkUnpack(pack, outVal, 1);
	if (ESP_OK != status) {
		return status;
	}

	*outVal = pack->array[pack->idx];
	pack->idx += 1;

	return ESP_OK;
}


esp_err_t csUnpackArray(csPacker_t * pack, uint8_t * outVal, int outLen)
{
	esp_err_t	status  = _checkUnpack(pack, outVal, outLen);
	if (ESP_OK != status) {
		return status;
	}

	memcpy(outVal, (pack->array + pack->idx), outLen);
	pack->idx += outLen;

	return ESP_OK;
}


esp_err_t csUnpackBEI16(csPacker_t * pack, int16_t * outVal)
{
	esp_err_t	status;
	uint16_t	temp;

	if ((status = csUnpackBEU16(pack, &temp)) != ESP_OK) {
		return status;
	}

	*outVal = (int16_t)temp;
	return ESP_OK;
}


esp_err_t csUnpackBEU16(csPacker_t * pack, uint16_t * outVal)
{
	esp_err_t	status  = _checkUnpack(pack, outVal, 2);
	if (ESP_OK != status) {
		return status;
	}

	*outVal =
		(((uint16_t)pack->array[pack->idx + 0]) << 8) +
		(((uint16_t)pack->array[pack->idx + 1]) << 0);
	pack->idx += 2;

	return ESP_OK;
}


esp_err_t csUnpackLEU16(csPacker_t * pack, uint16_t * outVal)
{
	esp_err_t	status  = _checkUnpack(pack, outVal, 2);
	if (ESP_OK != status) {
		return status;
	}

	*outVal =
		(((uint16_t)pack->array[pack->idx + 0]) << 0) +
		(((uint16_t)pack->array[pack->idx + 1]) << 8);
	pack->idx += 2;

	return ESP_OK;
}


esp_err_t csUnpackBEU32(csPacker_t * pack, uint32_t * outVal)
{
	esp_err_t	status  = _checkUnpack(pack, outVal, 4);
	if (ESP_OK != status) {
		return status;
	}

	*outVal =
		(((uint32_t)pack->array[pack->idx + 0]) << 24) +
		(((uint32_t)pack->array[pack->idx + 1]) << 16) +
		(((uint32_t)pack->array[pack->idx + 2]) <<  8) +
		(((uint32_t)pack->array[pack->idx + 3]) <<  0);
	pack->idx += 4;

	return ESP_OK;
}


esp_err_t csUnpackLEU32(csPacker_t * pack, uint32_t * outVal)
{
	esp_err_t	status  = _checkUnpack(pack, outVal, 4);
	if (ESP_OK != status) {
		return status;
	}

	*outVal =
		(((uint32_t)pack->array[pack->idx + 0]) <<  0) +
		(((uint32_t)pack->array[pack->idx + 1]) <<  8) +
		(((uint32_t)pack->array[pack->idx + 2]) << 16) +
		(((uint32_t)pack->array[pack->idx + 3]) << 24);
	pack->idx += 4;

	return ESP_OK;
}


esp_err_t csUnpackFloat(csPacker_t * pack, float * outVal)
{
	esp_err_t	status  = _checkUnpack(pack, outVal, 4);
	if (ESP_OK != status) {
		return status;
	}

	memcpy(outVal, &pack->array[pack->idx], 4);
	pack->idx += 4;

	return ESP_OK;
}
