/*
 * cs_der_to_pem.c
 *
 *  Created on: May 7, 2020
 *      Author: wesd
 */

#include "mbedtls/base64.h"
#include "cs_der_to_pem.h"
#include "cs_heap.h"
#include "cs_packer.h"

// Comment out the MOD_NAME line to disable debug prints from this file
#define MOD_NAME	"cs_der_to_heap"
#include "mod_debug.h"

static const char	certHead[] = {"-----BEGIN CERTIFICATE-----"};
static const char	certTail[] = {"-----END CERTIFICATE-----"};


char * csDerToPemCert(const uint8_t * der, int derLen)
{
	size_t	b64Len;

	// Figure out how much space is needed to store the Base64-encoded form of the data
	mbedtls_base64_encode(NULL, 0, &b64Len, (uint8_t *)der, (size_t)derLen);

	// Allocate space to hold the encoded data
	char *	b64Data = cs_heap_malloc(b64Len);
	if (!b64Data) {
		gc_err("Failed to allocate %d bytes for Base64 encoding", b64Len);
		return NULL;
	}

	// Do the encoding for real
	int		ret;
	size_t	outLen;

	// On success, a null-terminated string will be written to b64Data
	ret = mbedtls_base64_encode((uint8_t *)b64Data, b64Len, &outLen, (uint8_t *)der, (size_t)derLen);
	if (0 != ret) {
		gc_err("Failed to complete Base64 encoding");
		goto exitB64Data;
	}

	// Calculate number of newlines, including the head and tail lines
	// and a newline after every 64 Base64 characters
	int	numEols = 3 + outLen/64;

	//gc_dbg("outLen = %d, numEols = %d", outLen, numEols);

	int	certHeadLen = strlen(certHead);
	int	certTailLen = strlen(certTail);

	// Allocate space to hold the PEM plus termination
	int	pemSz = certHeadLen + b64Len + numEols + certTailLen + 1;
	uint8_t * pem = cs_heap_malloc(pemSz);
	if (!pem) {
		gc_err("Failed to allocate %d bytes for PEM", pemSz);
		goto exitB64Data;
	}

	csPacker_t	pack;
	csPackInit(&pack, pem, pemSz);

	// Copy the HEAD to the output buffer
	if (csPackStr(&pack, certHead) != ESP_OK) {
		gc_err("Failed to pack cert head");
	}

	// Copy the data, inserting a line break at 64-character intervals
	uint8_t *	src = (uint8_t *)b64Data;
	int			i;
	int			cpyLen;
	for (i = 0; i < outLen; i += cpyLen, src += cpyLen) {
		// Check for start of new line
		if (i % 64 == 0) {
			csPackU8(&pack, (uint8_t)'\n');
		}

		cpyLen = (outLen - i < 64) ? (outLen - i) : 64;

		if (csPackArray(&pack, src, cpyLen) != ESP_OK) {
			gc_err("Failed to pack %d bytes at offset %d", pack.idx);
			goto exitPem;
		}
	}
	csPackU8(&pack, (uint8_t)'\n');

	// Add the TAIL
	if (csPackStr(&pack, certTail) != ESP_OK) {
		gc_err("Failed to pack cert tail");
	}
	if (csPackStr(&pack, "\n") != ESP_OK) {
		gc_err("Failed to append final newline");
	}

	//gc_dbg("pack.arraySz %d", pack.arraySz);
	//gc_dbg("pack.idx     %d", pack.idx);

	// Check that the packing completed
	if (csPackStatus(&pack) != ESP_OK) {
		gc_err("PEM buffer overflow");
		goto exitPem;
	}

	cs_heap_free(b64Data);
	return (char *)pem;

exitPem:
	cs_heap_free(pem);
exitB64Data:
	cs_heap_free(b64Data);
	return NULL;
}
