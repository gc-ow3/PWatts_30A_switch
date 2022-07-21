/* Copyright (C) Grid Connect, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Evandro Copercini <evandro@gridconnect.com>, May 2019
 */

#ifndef _CMD_ADC_H_
#define _CMD_ADC_H_

#include "../../components/app_drivers/include/cs_i2c_bus.h"

void initialize_adc(csI2cBusConf_t *conf);

void register_adc();

#endif
