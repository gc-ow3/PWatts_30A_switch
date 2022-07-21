/* Copyright (C) Grid Connect, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 */

#ifndef _CMD_ATCA_H_
#define _CMD_ATCA_H_

#include "atca_iface.h"

extern int	atcaIsInitialized;

void initialize_atca(ATCAIfaceCfg * conf);

void register_atca();

#endif
