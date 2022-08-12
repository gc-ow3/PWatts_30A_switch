/*
********************************************************************************
* FILENAME:   xmodem_crc.c
*
* REVISION HISTORY:
*    REV LEVEL:   1.00
*    DATE:        2010-05-18
*    REASON:      Initial Release version of the module.
*
* MODULE DESCRIPTION:
*     Xmodem CRC calculation
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


/*
********************************************************************************
* FUNCTION:    csXmCrc16
*
* ARGUMENTS:
*   crc   - Current CRC value
*   data  - Byte of data to be added to CRC
*
* RETURNS:
*   U16  - updated CRC value
*
* DESCRIPTION:
*   Given a current CRC accumulator value and a data byte, update the CRC and
*   return the new value.
*
* AUTHOR INITIALS: WD
* CREATION DATE:   2010-01-15
********************************************************************************
*/
uint16_t csXmCrc16(uint16_t crc, uint8_t data)
{
	int i;

	crc = crc ^ ((uint16_t)data << 8);
	for (i = 0; i < 8; i++)
	{
		if (crc & 0x8000)
			crc = (crc << 1) ^ 0x1021;
		else
			crc <<= 1;
	}

	return crc;
}
