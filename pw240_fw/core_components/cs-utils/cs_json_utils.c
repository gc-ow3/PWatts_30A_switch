/*
 * cs_json_utils.c
 *
 *  Created on: Feb 20, 2019
 *      Author: wesd
 */

#include "cs_common.h"
#include "cs_json_utils.h"

// Comment out the MOD_NAME line to disable debug prints from this file
#define MOD_NAME	"cs_json_utils"
#include "mod_debug.h"


esp_err_t csJsonStrGet(cJSON * jInp, const char * const name, char * out, int outSz)
{
	if (!jInp || !name || !out)
		return ESP_ERR_INVALID_ARG;

	cJSON *		jObj;

	if ((jObj = cJSON_GetObjectItem(jInp, name)) == NULL) {
		gc_err("Missing: \"%s\"", name);
		return ESP_FAIL;
	}

	if (!cJSON_IsString(jObj)) {
		gc_err("Not a string: %s", name);
		return ESP_FAIL;
	}

	if (strlen(jObj->valuestring) >= outSz) {
		gc_err("String too long: %s", name);
		return ESP_FAIL;
	}
	strlcpy(out, jObj->valuestring, outSz);

	return ESP_OK;
}


cJSON * csJsonAddUint64(cJSON * jObj, const char * const name, uint64_t value)
{
	if (NULL == jObj ||  NULL == name) {
		gc_err("NULL parameter not allowed");
		return NULL;
	}

	// It's not clear that ESP's printf implementation supports 64-bit
	// so be safe, do it brute-force, and use cJSON's Add-Raw function

	// Largest possible 64-bit unsigned decimal number is
	// 18,446,744,073,709,551,615 which would be 20 characters without
	// the commas
	char	valStr[24];

	// Fill string buffer in reverse
	int	idx = sizeof(valStr) - 1;

	valStr[idx] = '\0';

	do {
		if (0 == idx) {
			gc_err("valStr overflow");
			return NULL;
		}

		valStr[--idx] = '0' + (value % 10);
		value /= 10;
	} while (value != 0);

	return cJSON_AddRawToObject(jObj, name, &valStr[idx]);
}


cJSON * csJsonAddUint32(cJSON * jObj, const char * const name, uint32_t value)
{
	return csJsonAddUint64(jObj, name, (uint64_t)value);
}
