/*
 * cs_json_utils.h
 *
 *  Created on: Feb 20, 2019
 *      Author: wesd
 */

#ifndef COMPONENTS_CS_UTILS_INCLUDE_CS_JSON_UTILS_H_
#define COMPONENTS_CS_UTILS_INCLUDE_CS_JSON_UTILS_H_

#include "cs_common.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t csJsonStrGet(cJSON * jInp, const char * const name, char * out, int outSz);

cJSON * csJsonAddUint32(cJSON * jObj, const char * const name, uint32_t value);

cJSON * csJsonAddUint64(cJSON * jObj, const char * const name, uint64_t value);


#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_CS_UTILS_INCLUDE_CS_JSON_UTILS_H_ */
