/*
 * app_params.c
 *
 *  Created on: Jan 4, 2019
 *      Author: wesd
 */

#include "cs_common.h"
#include "cs_platform.h"
#include "param_mgr.h"
#include "app_params.h"

const char paramKey_socket1On[]       = {"outlet1_on"};
const char paramKey_socket1Type[]     = {"outlet1_type"};
const char paramKey_socket1Name[]     = {"outlet1_name"};
const char paramKey_socket2On[]       = {"outlet2_on"};
const char paramKey_socket2Type[]     = {"outlet2_type"};
const char paramKey_socket2Name[]     = {"outlet2_name"};
const char paramKey_ledBrightness[]   = {"brightness"};
const char paramKey_targetMcuVers[]   = {"mcu_targ_ver"};
const char paramKey_buttonEnable[]    = {"btn_enable"};

/**
 * \brief Define parameters
 */
static csParamTab_t	paramTable[] = {
	{
		.title      = "Outlet 1 On",
		.nvSpace    = csNvSpace_standard,
		.nvKey      = paramKey_socket1On,
		.pVar       = &appParams.socket[0].on,
		.objTyp     = objtyp_bool,
		.minVal     = 0,
		.maxVal     = 1,
		.defVal     = "0",
		.init		= NULL,
		.check      = NULL
	},
	{
		.title      = "Outlet 1 Type",
		.nvSpace    = csNvSpace_standard,
		.nvKey      = paramKey_socket1Type,
		.pVar       = &appParams.socket[0].type,
		.objTyp     = objtyp_u32,
		.minVal     = 0,
		.maxVal     = 255,
		.defVal     = "0",
		.init		= NULL,
		.check      = NULL
	},
	{
		.title      = "Outlet 1 Name",
		.nvSpace    = csNvSpace_standard,
		.nvKey      = paramKey_socket1Name,
		.pVar       = appParams.socket[0].name,
		.objTyp     = objtyp_str,
		.minVal     = 0,
		.maxVal     = sizeof(appParams.socket[0].name),
		.defVal     = "Outlet A",
		.init		= NULL,
		.check      = NULL
	},
	{
		.title      = "Outlet 2 On",
		.nvSpace    = csNvSpace_standard,
		.nvKey      = paramKey_socket2On,
		.pVar       = &appParams.socket[1].on,
		.objTyp     = objtyp_bool,
		.minVal     = 0,
		.maxVal     = 1,
		.defVal     = "0",
		.init		= NULL,
		.check      = NULL
	},
	{
		.title      = "Outlet 2 Type",
		.nvSpace    = csNvSpace_standard,
		.nvKey      = paramKey_socket2Type,
		.pVar       = &appParams.socket[1].type,
		.objTyp     = objtyp_u32,
		.minVal     = 0,
		.maxVal     = 255,
		.defVal     = "0",
		.init		= NULL,
		.check      = NULL
	},
	{
		.title      = "Outlet 2 Name",
		.nvSpace    = csNvSpace_standard,
		.nvKey      = paramKey_socket2Name,
		.pVar       = appParams.socket[1].name,
		.objTyp     = objtyp_str,
		.minVal     = 0,
		.maxVal     = sizeof(appParams.socket[1].name),
		.defVal     = "Outlet B",
		.init		= NULL,
		.check      = NULL
	},
	{
		.title      = "LED brightness",
		.nvSpace    = csNvSpace_standard,
		.nvKey      = paramKey_ledBrightness,
		.pVar       = &appParams.brightness,
		.objTyp     = objtyp_u8,
		.minVal     = 0,
		.maxVal     = 100,
		.defVal     = "100",	// 100 percent
		.init		= NULL,
		.check      = NULL
	},
	{
		.title      = "Enable buttons",
		.nvSpace    = csNvSpace_standard,
		.nvKey      = paramKey_buttonEnable,
		.pVar       = &appParams.buttonsEnabled,
		.objTyp     = objtyp_bool,
		.minVal     = 0,
		.maxVal     = 1,
		.defVal     = "1",
		.init		= NULL,
		.check      = NULL
	},
};
#define paramTableSz	(sizeof(paramTable) / sizeof(csParamTab_t))


// Global parameter structure
appParams_t	appParams;


/**
 * \brief Initialize product parameters
 */
esp_err_t appParamsLoad(void)
{
	// Load the parameters
	return paramMgrParamsAdd(appInfo.model, paramTable, paramTableSz);
}
