/*
 * cs_str_utils.h
 *
 *  Created on: Feb 13, 2019
 *      Author: wesd
 */
#include "cs_common.h"

#ifndef COMPONENTS_CS_UTILS_INCLUDE_CS_STR_UTILS_H_
#define COMPONENTS_CS_UTILS_INCLUDE_CS_STR_UTILS_H_

#ifdef __cpluscplus
extern "C" {
#endif


void csStrUtilTrim(char * in);

bool csStrUtilIsHexString(const char * in);


#ifdef __cpluscplus
}
#endif

#endif /* COMPONENTS_CS_UTILS_INCLUDE_CS_STR_UTILS_H_ */
