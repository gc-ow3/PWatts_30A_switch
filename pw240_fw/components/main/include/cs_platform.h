/**
* \file cs_platform.h
* 
* \brief Platform-specific header for CS-IWO
* 
* To be referenced from cs_common.h

*/

#ifndef _MAIN_INCLUDE_CS_PLATFORM_
#define _MAIN_INCLUDE_CS_PLATFORM_

#include "cs_common.h"
#include "cs_str_utils.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NUM_SOCKETS				(2)

/*
 * Application task priorities
 */
#define TASK_PRIO_CAP1298_DRV		(tskIDLE_PRIORITY + 20)
#define TASK_PRIO_CAP1298_HNDLR		(tskIDLE_PRIORITY + 18)
#define TASK_PRIO_LED_MGR			(tskIDLE_PRIORITY + 16)
#define TASK_PRIO_EMTR_DRV			(tskIDLE_PRIORITY + 14)
#define TASK_PRIO_LOCAL_API			(tskIDLE_PRIORITY + 11)
#define TASK_PRIO_REBOOTER			(tskIDLE_PRIORITY + 11)
#define TASK_PRIO_CRON				(tskIDLE_PRIORITY + 10)


//******************************************************************************
//******************************************************************************
// Data types
//******************************************************************************
//******************************************************************************

typedef struct {
	bool	cap1298Present;
	bool	emtrPresent;
	int		failCount;
} appSelfTest_t;

extern appSelfTest_t	appSelfTest;

//******************************************************************************
//******************************************************************************
// Constant values
//******************************************************************************
//******************************************************************************
csCoreInit0_t appInfo;

extern const char appFwVer[];


#ifdef __cplusplus
}
#endif

#endif	// _MAIN_INCLUDE_CS_PLATFORM_
