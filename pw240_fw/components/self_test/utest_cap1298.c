/*
 * utest_cap1298.c
 *
 *  Created on: Jan 24, 2019
 *      Author: wesd
 */

#include "utest_cap1298.h"


esp_err_t utestCap1298(const cap1298DrvConf_t * conf)
{
	return cap1298DrvInit(conf);
}
