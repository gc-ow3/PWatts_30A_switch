/*
 * cs_binhex.h
 *
 *  Created on: Feb 5, 2019
 *      Author: wesd
 */

#ifndef COMPONENTS_UTILS_INCLUDE_CS_BINHEX_H_
#define COMPONENTS_UTILS_INCLUDE_CS_BINHEX_H_

#include "esp_system.h"

#ifdef __cplusplus
extern "C" {
#endif

int csBinToHex8(uint8_t * inp, int inpLen, char * outp, int outSz);

int csHexToBin8(const char * inp, uint8_t * outp, int outSz);


#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_UTILS_INCLUDE_CS_BINHEX_H_ */
