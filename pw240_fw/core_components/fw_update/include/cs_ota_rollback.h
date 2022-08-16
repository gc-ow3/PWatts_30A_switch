/*
 * cs_ota_rollback.h
 *
 *  Created on: Feb 17, 2020
 *      Author: wesd
 */

#ifndef COMPONENTS_FW_UPDATE_INCLUDE_CS_OTA_ROLLBACK_H_
#define COMPONENTS_FW_UPDATE_INCLUDE_CS_OTA_ROLLBACK_H_

#include "cs_common.h"

typedef enum {
	csOtaType_null   = 0,
	csOtaType_remote = 1,//from bridge
	csOtaType_local  = 2,//from soft ap
} csOtaType_t;


/**
 * \brief Indicate if a new firmware image has been installed via OTA update
 */
bool csOtaUpdateIsPresent(csOtaType_t * otaType);

/**
 * \brief Check if the system has started after an OTA update and mark it valid
 *
 * The application must call this at a point where it stable - such as after
 * connecting to wifi
 *
 */
void csOtaUpdateAccept(void);


/**
 * \brief Check if the system has started after an OTA update and mark it invalid
 *
 * This will cause a rollback to the prior firmware version
 */
void csOtaUpdateReject(void);

void csOtaTypeSet(csOtaType_t otaType);

csOtaType_t csOtaTypeGet(void);


#endif /* COMPONENTS_FW_UPDATE_INCLUDE_CS_OTA_ROLLBACK_H_ */
