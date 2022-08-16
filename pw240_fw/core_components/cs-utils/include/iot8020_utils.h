/*
 * iot8020_utils.h
 *
 *  Created on: Feb 20, 2019
 *      Author: wesd
 */

#ifndef COMPONENTS_CS_UTILS_INCLUDE_IOT8020_UTILS_H_
#define COMPONENTS_CS_UTILS_INCLUDE_IOT8020_UTILS_H_

#ifdef __cplusplus
extern "C" {
#endif

const char * csIot8020UrlGet(void);

void csIoT8020SetStaging(bool useStaging);

bool csIot8020IsStaging(void);


#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_CS_UTILS_INCLUDE_IOT8020_UTILS_H_ */
