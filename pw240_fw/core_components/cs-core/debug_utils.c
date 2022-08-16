/*
 * debug_utils.c
 *
 *  Created on: Jan 3, 2019
 *      Author: wesd
 */

#include <stdint.h>
#include <string.h>
#include "esp_system.h"
#include "mod_debug.h"

/*!
 * \brief Hexdump utility for debug support
 *
 * \param [in] pData Pointer to data to be displayed
 * \param [in] len Number of bytes to print
 *
 * Prints 16 hex bytes per line, decodes printable ASCII. Unprintable
 * bytes are shown as '.'
 *
 * 74 65 3a 20 46 72 69 2c 20 32 34 20 4a 75 6e 20  te: Fri, 24 Jun
 * 32 30 31 36 20 30 34 3a 32 36 3a 35 32 20 47 4d  2016 04:26:52 GM
 * 54 0d 0a 00                                      T...
 *
 */
void hexDump(const uint8_t * pData, int len)
{
#ifdef CONFIG_IOT8020_DEBUG
	int		i;
	int		colCt = 0;
	char	cBuf[17];

	for (i = 0; i < len; i++) {
		if (pData[i] > 0x1F && pData[i] < 0x7F) {
			cBuf[colCt] = (char)pData[i];
		} else {
			cBuf[colCt] = '.';
		}

		printf("%02X ", pData[i]);

		if ((++colCt == 16) || (i == len-1)) {
			cBuf[colCt] = '\0';
			for (; colCt < 16; colCt++)
				printf("   ");
			colCt = 0;
			printf("  %s\r\n", cBuf);
		}
	}
#endif
}


/*!
 * \brief Alternate hexdump utility for debug support
 *
 * \param [in] pData Optional pointer to title string, NULL to exclude the title
 * \param [in] pData Pointer to data to be displayed
 * \param [in] len Number of bytes to print
 *
 * This prints a sequence of hex digits to the console with no formatting,
 * and a page break every 64 characters (32 bytes)
 * If the title is given, the hex lines will be indented two spaces
 *
 */
void hexDump2(const char * title, uint8_t * pData, int len, bool space)
{
#ifdef CONFIG_IOT8020_DEBUG
	int		i;
	int		colCt = 0;

	if (title) {
		printf("%s (%d bytes)\r\n", title, len);
		printf("  ");
	}

	for (i = 0; i < len; i++) {
		printf("%02x", pData[i]);
		if (space)
			printf(" ");

		if (++colCt == 32 || i == (len - 1)) {
			colCt = 0;
			printf("\r\n");
			printf("  ");
		}
	}
#endif
}


void textDump(const char * pTitle, const char * pStr, int len)
{
#ifdef CONFIG_IOT8020_DEBUG
	int		i;
	int		j = 0;

	if (-1 == len) {
		len = strlen(pStr);
	}

	if (pTitle) {
		printf("%s (%d bytes):", pTitle, len);
	}

	for (i = 0; pStr[i] && i < len; i++) {
		if (pStr[i] == '\n') {
			printf("\r\n");
			j = 0;
		} else {
			printf("%c", pStr[i]);
			if (++j >= 80)
			{
				printf("\r\n");
				j = 0;
			}
		}
	}

	if (j) {
		printf("\r\n");
	}
#endif
}
