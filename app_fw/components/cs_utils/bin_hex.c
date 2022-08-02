/*
 * bin_hex.c
 *
 *  Created on: Feb 5, 2019
 *      Author: wesd
 */

#include <string.h>

#include "cs_binhex.h"


/*!
 * \brief Convert an array of bytes to a hex string
 *
 * \param [in] inp pointer to byte array
 * \param [in] inpLen number of bytes to convert
 * \param [out] outp pointer to string buffer
 * \param [in] outSz size of the output buffer
 *
 * \return >= 0 Length of output string
 * \return -1 Output buffer too small
 *
 * The output buffer size must be at least (inpLen * 2) + 1
 */
int csBinToHex8(uint8_t * inp, int inpLen, char * outp, int outSz)
{
	if (NULL == inp || NULL == outp)
		return -1;

	if (outSz < (inpLen * 2) + 1)
		return -1;

	int		inIdx;
	int		outLen = 0;

	for (inIdx = 0; inIdx < inpLen; inIdx++, inp++, outLen += 2) {
		sprintf(outp + outLen, "%02x", *inp);
	}

	return outLen;
}


/*!
 * \brief Convert a hex string to an an array of bytes
 *
 * \param [in] inp pointer to hex string
 * \param [out] outp pointer to byte buffer
 * \param [in] outSz size of the output buffer
 *
 * \return >= 0 Length of output data
 * \return -1 Failed
 *
 * Length of the input string must be a multiple of 2
 *
 */
int csHexToBin8(const char * inp, uint8_t * outp, int outSz)
{
	if (NULL == inp || NULL == outp)
		return -1;

	// Get length of the input string and make sure it's even
	int		inpLen = strlen(inp);
	if (inpLen & 1)
		return -1;

	// Make sure the output buffer is large enough
	if ((inpLen / 2) > outSz)
		return -1;

	int		inIdx;
	int		outLen = 0;

	for (inIdx = 0; inIdx < inpLen; inIdx += 2, outLen++, outp++) {
		int	nibIdx;

		*outp = 0;
		for (nibIdx = 0; nibIdx < 2; nibIdx++, inp++) {
			*outp <<= 4;

			if (*inp >= '0' && *inp <= '9')
				*outp += (uint8_t)(*inp - '0');
			else if (*inp >= 'A' && *inp <= 'F')
				*outp += 10 + (uint8_t)(*inp - 'A');
			else if (*inp >= 'a' && *inp <= 'f')
				*outp += 10 + (uint8_t)(*inp - 'a');
			else
				return -1;
		}
	}

	return outLen;
}
