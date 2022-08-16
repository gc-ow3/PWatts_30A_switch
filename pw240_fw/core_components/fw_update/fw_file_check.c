/*
 * fw_file_check.c
 *
 *  Created on: Jul 24, 2019
 *      Author: wesd
 */

#include "fw_file_check.h"
#include "esp32/rom/crc.h"

// Comment out the MOD_NAME line to disable debug prints from this file
#define MOD_NAME	"fw_file_check"
#include "mod_debug.h"

#define OTA_FW_MAX_SZ	(128 * 1024)


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
		gc_err("CRC mismatch: got %08x, expected %08x", crc, hdr->dataCrc);
		return false;
	}

	return true;
}


esp_err_t csFwFileCheckHeader(const csFwHdr_t * hdr, const char * type)
{
	if (memcmp(otaHdrTag, hdr->tag, 4) != 0) {
		// Tag does not match
		gc_err("OTA header tag mismatch");
		return ESP_FAIL;
	}

	// Optionally verify expected type is in the header
	if (NULL != type) {
		if (memcmp(type, hdr->type, 4) != 0) {
			// Firmware type does not match
			gc_err("OTA firmware type mismatch");
			return ESP_FAIL;
		}
	}

	if (1 != hdr->hdrVersion) {
		// Header version does not match
		gc_err("OTA header version mismatch");
		return ESP_FAIL;
	}

	uint32_t	fileSz  = sizeof(*hdr) + hdr->dataLen;

	if (OTA_FW_MAX_SZ < fileSz) {
		// file too large
		gc_err("OTA file too large: %lu", fileSz);
		return ESP_FAIL;
	}

	uint32_t	calcCrc = crc32_le(0, (uint8_t *)hdr, sizeof(*hdr) - 4);

	if (calcCrc != hdr->hdrCrc) {
		gc_err("OTA header CRC check failed");
		gc_err("  Expected %08X, Got %08X", hdr->hdrCrc, calcCrc);
		return ESP_ERR_INVALID_CRC;
	}

	return ESP_OK;
}


void csFwFilePrintHeader(const csFwHdr_t * hdr)
{
	gc_dbg("OTA Firmware header");
	gc_dbg("  Signature       : %.4s", hdr->tag);
	gc_dbg("  Fw Type         : %.4s", hdr->type);
	gc_dbg("  Hdr Version     : %u",   hdr->hdrVersion);
	gc_dbg("  Fw Major Version: %u",   hdr->majorVer);
	gc_dbg("  Fw Minor Version: %u",   hdr->minorVer);
	gc_dbg("  Fw Patch Version: %u",   hdr->patchVer);
	gc_dbg("  Flags           : %08X", hdr->flags);
	gc_dbg("  Fw Length       : %lu",  hdr->dataLen);
	gc_dbg("  Fw CRC          : %08X", hdr->dataCrc);
	gc_dbg("  Header CRC      : %08X", hdr->hdrCrc);
}
