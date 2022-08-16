/**
 * \file param_mgr.h
 *
 * \brief Core parameter management support
 *
 */
#ifndef __CS_CORE_PARAM_MGR_H__
#define __CS_CORE_PARAM_MGR_H__

#ifdef __cplusplus
extern "C" {
#endif

// CS core header files
#include "cs_common.h"

//******************************************************************************
// type definitions
//******************************************************************************

typedef const struct csParamTab_s	csParamTab_t;


//! Definitions of the object types accepted by the JSON parser
typedef enum {
	objtyp_u8,		//!< 8-bit signed integer
	objtyp_i32,		//!< 32-bit signed integer
	objtyp_u32,		//!< 32-bit unsigned integer
	objtyp_str,		//!< String
	objtyp_bool,	//!< Boolean (1|0) handled internally as int8_t
	objtyp_blob		//!< Binary
} objtype_t;

typedef enum {
	csNvSpace_standard = 0,
	csNvSpace_sticky			// Persists over factory reset
} csNvSpace_t;

// Prototype of function for special initialization of a value
typedef esp_err_t (* pmInitFunc_t)(nvs_handle nvsHandle, csParamTab_t * pTab);

// Prototype of function for custom validation of parameter value
typedef esp_err_t (* pmCheckFunc_t)(csParamTab_t * pTab, void * pValue);

// Structure used to build a list of parameters handled by this device.
struct csParamTab_s {
	const char *	title;		//!< For debug printing
	csNvSpace_t		nvSpace;	//!< Select NVS namespace
	const char *	nvKey;		//!< Parameter key
	void *			pVar;		//!< Pointer to the RAM copy
	objtype_t		objTyp;		//!< Object type (int, boolean, string, ...)
	int32_t			minVal;		//!< Minimum value
	int32_t			maxVal;		//!< Maximum value
	const char *	defVal;		//!< Default factory value (string form)
	pmInitFunc_t	init;		//!< Optional parameter initialization function
	pmCheckFunc_t	check;		//!< Optional validation function
};


esp_err_t paramMgrInit(void);

esp_err_t paramMgrParamsAdd(const char * src, csParamTab_t * pTab, int tabSz);

esp_err_t paramMgrStart(void);

void paramMgrSettingsDump(void);

esp_err_t paramMgrReset(void);

//esp_err_t paramMgrResetParam(const char * pKey);

esp_err_t paramMgrSetBool(const char * pName, bool value);

esp_err_t paramMgrSetU8(const char * pName, uint8_t value);

esp_err_t paramMgrSetI32(const char * pName, int32_t value);

esp_err_t paramMgrSetU32(const char * pName, uint32_t value);

esp_err_t paramMgrSetStr(const char * pName, const char *);

const csParamTab_t * paramMgrLookupParam(const char * name);

esp_err_t paramMgrSetBlob(const char * pKey, void * value, size_t len);

esp_err_t paramMgrGetBlob(const char * pKey, void * value, size_t * len);

esp_err_t paramMgrGetBlobSize(const char * pKey, size_t * len);

esp_err_t paramMgrDeleteBlob(const char * pKey);


#ifdef __cplusplus
}
#endif

#endif /* __CS_CORE_PARAM_MGR_H__ */
