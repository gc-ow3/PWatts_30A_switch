/*
 * app_params.h
 *
 *  Created on: Jan 3, 2019
 *      Author: wesd
 */

#ifndef MAIN_INCLUDE_APP_PARAMS_H_
#define MAIN_INCLUDE_APP_PARAMS_H_

#include "cs_common.h"
#include "param_mgr.h"

#ifdef __cplusplus
extern "C" {
#endif


#define CS_OUTLET_NAME_SZ		(30)
#define NUM_SOCKETS				(2)
#define CS_REBOOTER_URL_SZ		(50)

typedef struct {
	bool		on;
	uint32_t	type;
	char		name[CS_OUTLET_NAME_SZ + 1];
	bool		rebooterEnabled;  //enables reboot for this outlet if offline state is determined
	bool		rebooterAnyFailLogic; //if true, than offline detection occurs when any url fails rather than all urls
	uint8_t		rebooterOffSeconds; //seconds to delay power on when rebooting socket
	char		rebooterUrls[5][CS_REBOOTER_URL_SZ+1];//The url array this socket will use to determine the device is offline
} appSocket_t;

//! RAM copy of configuration parameters
typedef struct {
	bool		buttonsEnabled;
	uint8_t		brightness;
	uint8_t 	offlineTriggerMinutes; //minutes after first ping failure to determine offline state
	uint8_t 	detectionDelayMinutes;
	uint8_t		rebooterMaxReboots; //maximum reboots without resolution
	appSocket_t	socket[NUM_SOCKETS];
} appParams_t;


/**
 * \brief Name strings for stored parameters
 */
extern const char paramKey_socket1On[];
extern const char paramKey_socket1Type[];
extern const char paramKey_socket1Name[];
extern const char paramKey_socket2On[];
extern const char paramKey_socket2Type[];
extern const char paramKey_socket2Name[];
extern const char paramKey_ledBrightness[];
extern const char paramKey_targetMcuVers[];
extern const char paramKey_buttonEnable[];


/**
 * \brief Application parameters
 */
extern appParams_t	appParams;


/**
 * \brief Load parameters from storage into the appParams structure
 */
esp_err_t appParamsLoad(void);


#ifdef __cplusplus
}
#endif

#endif /* MAIN_INCLUDE_APP_PARAMS_H_ */
