/*
 * fw_file_check.h
 *
 *  Created on: Jul 24, 2019
 *      Author: wesd
 */

#ifndef COMPONENTS_FW_UPDATE_INCLUDE_FW_FILE_CHECK_H_
#define COMPONENTS_FW_UPDATE_INCLUDE_FW_FILE_CHECK_H_

#include "cs_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CS_FWUPG_FLAG_SIGNED		(1 << 0)
#define CS_FWUPG_FLAG_ENCRYPTED		(1 << 1)
#define CS_FWUPG_FLAG_COMPRESSED	(1 << 2)

/**
 * \brief Header for OTA firmware file
 */
typedef struct __attribute__((packed)) {
	char		tag[4];			// 'CSFW'
	char		type[4];		// 'emtr'
	uint8_t		hdrVersion;		// 1
	uint8_t		majorVer;
	uint8_t		minorVer;
	uint8_t		patchVer;
	uint32_t	flags;			// Bit flags
	uint32_t	dataLen;		// Length of content
	uint32_t	dataCrc;		// CRC32 of content
	uint8_t		pad[100];		// Pad to 124 bytes
	uint32_t	hdrCrc;			// CRC32 of header
} csFwHdr_t;

bool csFwFileIsValid(const uint8_t * fwFile, const char * fileType);

esp_err_t csFwFileCheckHeader(const csFwHdr_t * hdr, const char * type);

void csFwFilePrintHeader(const csFwHdr_t * hdr);


#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_FW_UPDATE_INCLUDE_FW_FILE_CHECK_H_ */
