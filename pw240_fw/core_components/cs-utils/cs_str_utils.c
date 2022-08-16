/*
 * str_utils.c
 *
 *  Created on: Feb 13, 2019
 *      Author: wesd
 */


#include "cs_common.h"

static const char hexDigits[] = {"0123456789abcdef"};


/**
 * \brief remove leading and trailing white space from a string
 */
void csStrUtilTrim(char * in)
{
	if (NULL == in)
		return;

	char *	out;

	for (out = in; '\0' != *in; in++) {
		if (*in <= ' ')
			continue;
		*out++ = *in;
	}

	*out = '\0';
}


bool csStrUtilIsHexString(const char * in)
{
	if (NULL == in || '\0' == *in)
		return false;

	while ('\0' != *in) {
		int	test = *in++;

		if (strchr(hexDigits, tolower(test)) == NULL)
			return false;
	}

	return true;
}
