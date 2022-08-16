/*
 * mfg_data.h
 *
 *  Created on: Dec 3, 2018
 *      Author: wesd
 */

#ifndef __CS_CORE_INC_MFG_DATA_H__
#define __CS_CORE_INC_MFG_DATA_H__

#include "cs_common.h"
#include "nvs_flash.h"

#ifdef __cplusplus
extern "C" {
#endif


//! Types of data stored in manufacturing data
typedef enum {
	mfgDataType_bin,
	mfgDataType_str,
	mfgDataType_u8
} mfgDataType_t;


//! Structure to build a table of manufacturing data to be loaded
typedef struct {
	const char *	key;		//!< The name in the nvs partition
	mfgDataType_t	type;		//!< Type of data
	void *			value;		//!< The RAM space to be loaded
	int				maxLen;		//!< The size of the RAM space
	bool			alloc;		//!< true to dynamically allocate space
	const char *	defVal;		//!< default value
} const mfgDataTable_t;

typedef struct {
	bool		isValid;
	char		serialNum[20];
	char		hwVersion[8];
	uint8_t		macAddrBase[6];
	char *		tlsKeyB64;
	char *		tlsCertB64;
	char *		prod_metadata;
} coreMfgData_t;

extern coreMfgData_t coreMfgData;


esp_err_t mfgDataInit(void);

esp_err_t mfgDataDeinit();

esp_err_t mfgDataLoadCore();

esp_err_t mfgDataLoad(mfgDataTable_t * mfgTab, int tabSz);

bool mfgDataIsEnabled(void);
esp_err_t mfgDataDisable(void);
esp_err_t mfgDataEnable(void);

esp_err_t mfgDataSave();

void mfgDataCorePrint(void);

void mfgDataAppPrint(mfgDataTable_t * tab, int tabSz);

esp_err_t mfgDataStore(const uint8_t * data, int dataLen);

esp_err_t mfgNvsKeysStore(const uint8_t * data, int dataLen);


#ifdef __cplusplus
}
#endif

#endif /* __CS_CORE_INC_MFG_DATA_H__ */
