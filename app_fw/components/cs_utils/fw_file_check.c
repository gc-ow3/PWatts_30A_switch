/*
 * fw_file_check.c
 *
 *  Created on: Jul 24, 2019
 *      Author: wesd
 */
#include <esp_err.h>
#include <esp_log.h>
#include "fw_file_check.h"
#include "esp32/rom/crc.h"

#define OTA_FW_MAX_SZ	(128 * 1024)

static const char*	TAG = "fw_check";

// The expected tag at the start of a firmware image
static const char	otaHdrTag[] = {"CSFW"};


/**
 * \brief Validate the firmware file
 */
bool csFwFileIsValid(const uint8_t * fwFile, const char * fileType)
{
	if (NULL == fwFile || NULL == fileType) {
		return false;
	}

	const csFwHdr_t *	hdr = (const csFwHdr_t *)fwFile;
	esp_err_t			status;

	if ((status = csFwFileCheckHeader(hdr, fileType)) != ESP_OK)
		return false;

	// File data follows immediately after the header
	uint8_t *	fileData = (uint8_t *)(hdr + 1);

	// Compute the CRC of the file data
	uint32_t	crc = 0;
	crc = crc32_le(crc, fileData, hdr->dataLen);

	// Verify CRC against the value in the header
	if (hdr->dataCrc != crc) {
		ESP_LOGE(TAG, "CRC mismatch: got %08x, expected %08x", crc, hdr->dataCrc);
		return false;
	}

	return true;
}


esp_err_t csFwFileCheckHeader(const csFwHdr_t * hdr, const char * type)
{
	if (memcmp(otaHdrTag, hdr->tag, 4) != 0) {
		// Tag does not match
		ESP_LOGE(TAG, "OTA header tag mismatch");
		return ESP_FAIL;
	}

	// Optionally verify expected type is in the header
	if (NULL != type) {
		if (memcmp(type, hdr->type, 4) != 0) {
			// Firmware type does not match
			ESP_LOGE(TAG, "OTA firmware type mismatch");
			return ESP_FAIL;
		}
	}

	if (1 != hdr->hdrVersion) {
		// Header version does not match
		ESP_LOGE(TAG, "OTA header version mismatch");
		return ESP_FAIL;
	}

	uint32_t	fileSz  = sizeof(*hdr) + hdr->dataLen;

	if (OTA_FW_MAX_SZ < fileSz) {
		// file too large
		ESP_LOGE(TAG, "OTA file too large: %u", fileSz);
		return ESP_FAIL;
	}

	uint32_t	calcCrc = crc32_le(0, (uint8_t *)hdr, sizeof(*hdr) - 4);

	if (calcCrc != hdr->hdrCrc) {
		ESP_LOGE(TAG, "OTA header CRC check failed");
		ESP_LOGE(TAG, "  Expected %08X, Got %08X", hdr->hdrCrc, calcCrc);
		return ESP_ERR_INVALID_CRC;
	}

	return ESP_OK;
}


void csFwFilePrintHeader(const csFwHdr_t * hdr)
{
	ESP_LOGI(TAG, "OTA Firmware header");
	ESP_LOGI(TAG, "  Signature       : %.4s", hdr->tag);
	ESP_LOGI(TAG, "  Fw Type         : %.4s", hdr->type);
	ESP_LOGI(TAG, "  Hdr Version     : %u",   hdr->hdrVersion);
	ESP_LOGI(TAG, "  Fw Major Version: %u",   hdr->majorVer);
	ESP_LOGI(TAG, "  Fw Minor Version: %u",   hdr->minorVer);
	ESP_LOGI(TAG, "  Fw Patch Version: %u",   hdr->patchVer);
	ESP_LOGI(TAG, "  Flags           : %08X", hdr->flags);
	ESP_LOGI(TAG, "  Fw Length       : %u",   hdr->dataLen);
	ESP_LOGI(TAG, "  Fw CRC          : %08X", hdr->dataCrc);
	ESP_LOGI(TAG, "  Header CRC      : %08X", hdr->hdrCrc);
}
