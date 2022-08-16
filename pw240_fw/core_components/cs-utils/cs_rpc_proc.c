/*
 * rpc_proc.c
 *
 *  Created on: Aug 29, 2019
 *      Author: wesd
 */
#include "cs_rpc_proc.h"

// Comment out the MOD_NAME line to disable debug prints from this file
#define MOD_NAME	"cs_rpc_proc"
#include "mod_debug.h"


typedef struct {
	void *				callerCtx;
	cJSON *				json;
	uint32_t			cmdId;
	csRpcReplyFunc_t	replyFunc;
} csRpcCmdParam_t;


static csRpcMethodTab_t * findMethod(csRpcMethodTabList_t * tabList, const char * method);


esp_err_t csRpcProc(
	cJSON *					jRpc,
	csRpcMethodTabList_t *	tabList,
	csRpcReplyFunc_t		replyFunc,
	void *					callerCtx
)
{
	if (!jRpc || !tabList || !replyFunc) {
		return ESP_ERR_INVALID_ARG;
	}

	int		errCode;
	char *	errMsg;
	cJSON *	jObj;

	// Build the RPC context
	csRpcCmdParam_t	rpcCmd = {
		.json      = jRpc,
		.cmdId     = 0,
		.callerCtx = callerCtx,
		.replyFunc = replyFunc
	};

	csRpcCtx_t	rpcCtx = (csRpcCtx_t)&rpcCmd;

	const char *	strVal;

	if ((strVal = cJSON_GetStringValue(cJSON_GetObjectItem(jRpc, "jsonrpc"))) == NULL) {
		errCode = CS_RPC_ERR_NOT_RPC;
		errMsg  = "RPC missing version";
		goto errExit;
	}

	if (strcmp(strVal, "2.0") != 0) {
		errCode = CS_RPC_ERR_NOT_RPC;
		errMsg  = "RPC version wrong";
		goto errExit;
	}

	if ((jObj = cJSON_GetObjectItem(jRpc, "id")) == NULL) {
		errCode = CS_RPC_ERR_NO_ID;
		errMsg  = "RPC id missing";
		goto errExit;
	}

	if (!cJSON_IsNumber(jObj)) {
		errCode = CS_RPC_ERR_NO_ID;
		errMsg  = "RPC id not a number";
		goto errExit;
	}
	rpcCmd.cmdId = (uint32_t)jObj->valueint;

	// Read method name
	char *	methodStr;
	methodStr = cJSON_GetStringValue(cJSON_GetObjectItem(jRpc, "method"));
	if (NULL == methodStr) {
		errCode = CS_RPC_ERR_NO_METHOD;
		errMsg  = "RPC method missing";
		goto errExit;
	}

	// Look for method in the table(s)
	csRpcMethodTab_t *	method = findMethod(tabList, methodStr);
	if (NULL == method) {
		errCode = CS_RPC_ERR_NSUPP;
		errMsg  = "RPC method not supported";
		goto errExit;
	}

	// Check for presence of params
	cJSON *	jParams = cJSON_GetObjectItem(jRpc, "params");

	// If params are required and not present, reply with error
	if (method->paramsReqd && NULL == jParams) {
		errCode = CS_RPC_ERR_NO_PARAMS;
		errMsg  = "RPC method requires params";
		goto errExit;
	}

	// Call the handler which is responsible for sending replies
	// by calling either csRpcSendOkResponse() or csRpcSendErrResponse()
	return method->handler(callerCtx, rpcCtx, jParams);

errExit:
	// Send error response
	csRpcSendErrResponse(rpcCtx, errCode, errMsg);
	return ESP_FAIL;
}


void csRpcSendOkResponse(csRpcCtx_t rpcCtx, const char * respData)
{
	if (!rpcCtx)
		return;

	csRpcCmdParam_t *	rpc = (csRpcCmdParam_t *)rpcCtx;

	// Check for optional response data string
	if (NULL == respData) {
		// Apply the default string
		respData = "\"success\"";
	}

	cJSON *	jRoot = cJSON_CreateObject();

	cJSON_AddStringToObject(jRoot, "jsonrpc", "2.0");
	cJSON_AddNumberToObject(jRoot, "id", rpc->cmdId);
	cJSON_AddRawToObject(jRoot, "result", respData);

	char *	jStr = cJSON_PrintUnformatted(jRoot);
	cJSON_Delete(jRoot);

	if (jStr) {
		rpc->replyFunc(rpc->callerCtx, jStr, strlen(jStr));
		cJSON_free(jStr);
	} else {
		gc_err("Failed to build response");
	}
}


void csRpcSendErrResponse(csRpcCtx_t rpcCtx, uint32_t errCode, const char * errMsg)
{
	if (!rpcCtx)
		return;

	csRpcCmdParam_t *	rpc = (csRpcCmdParam_t *)rpcCtx;

	if (NULL == errMsg) {
		errMsg = "";
	}

	cJSON *	jRoot = cJSON_CreateObject();

	cJSON_AddStringToObject(jRoot, "jsonrpc", "2.0");
	cJSON_AddNumberToObject(jRoot, "id", rpc->cmdId);

	cJSON * jErr = cJSON_AddObjectToObject(jRoot, "error");
	cJSON_AddNumberToObject(jErr, "code", errCode);
	cJSON_AddStringToObject(jErr, "message", errMsg);

	char *	jStr = cJSON_PrintUnformatted(jRoot);
	cJSON_Delete(jRoot);

	if (jStr) {
		rpc->replyFunc(rpc->callerCtx, jStr, strlen(jStr));
		cJSON_free(jStr);
	} else {
		gc_err("Failed to build response");
	}
}


esp_err_t csRpcReadParamStr(
	csRpcCtx_t	rpcCtx,
	cJSON *		jParams,
	bool		required,
	char *		name,
	char *		ret,
	int			retSz
)
{
	if (!rpcCtx)
		return ESP_ERR_INVALID_ARG;

	const char *	value = csRpcGetParamStrRef(rpcCtx, jParams, required, name);
	if (NULL == value) {
		return ESP_FAIL;
	}

	int		errCode;
	char	errMsg[60];

	if (strlen(value) >= retSz) {
		errCode   = CS_RPC_ERR_NO_MEM;
		snprintf(errMsg, sizeof(errMsg), "\"%s\" is too large", name);
		goto errExit;
	}

	strlcpy(ret, value, retSz);
	return ESP_OK;

errExit:
	if (required)
		csRpcSendErrResponse(rpcCtx, errCode, errMsg);
	return ESP_FAIL;
}


const char * csRpcGetParamStrRef(
	csRpcCtx_t	rpcCtx,
	cJSON *		jParams,
	bool		required,
	char *		name
)
{
	if (!rpcCtx)
		return NULL;

	int		errCode;
	cJSON *	jObj;
	char	errMsg[60];

	if ((jObj = cJSON_GetObjectItem(jParams, name)) == NULL) {
		errCode   = CS_RPC_ERR_MISS_PARAM;
		snprintf(errMsg, sizeof(errMsg), "'%s' is missing", name);
		goto errExit;
	}

	if (!cJSON_IsString(jObj)) {
		errCode   = CS_RPC_ERR_PARAM_VAL;
		snprintf(errMsg, sizeof(errMsg), "'%s' is not a string", name);
		goto errExit;
	}

	return jObj->valuestring;

errExit:
	if (required)
		csRpcSendErrResponse(rpcCtx, errCode, errMsg);
	return NULL;
}


esp_err_t csRpcReadParamInt(
	csRpcCtx_t	rpcCtx,
	cJSON *		jParams,
	bool		required,
	char *		name,
	int *		value
)
{
	if (!rpcCtx)
		return ESP_ERR_INVALID_ARG;

	int		errCode;
	cJSON *	jObj;
	char	errMsg[60];

	if ((jObj = cJSON_GetObjectItem(jParams, name)) == NULL) {
		errCode   = CS_RPC_ERR_MISS_PARAM;
		snprintf(errMsg, sizeof(errMsg), "'%s' is missing", name);
		goto errExit;
	}

	if (!cJSON_IsNumber(jObj)) {
		errCode   = CS_RPC_ERR_PARAM_VAL;
		snprintf(errMsg, sizeof(errMsg), "'%s' is not a number", name);
		goto errExit;
	}

	*value = jObj->valueint;
	return ESP_OK;

errExit:
	if (required)
		csRpcSendErrResponse(rpcCtx, errCode, errMsg);
	return ESP_FAIL;
}


esp_err_t csRpcReadParamFloat(
	csRpcCtx_t	rpcCtx,
	cJSON *		jParams,
	bool		required,
	char *		name,
	float *		value
)
{
	if (!rpcCtx)
		return ESP_ERR_INVALID_ARG;

	int		errCode;
	cJSON *	jObj;
	char	errMsg[60];

	if ((jObj = cJSON_GetObjectItem(jParams, name)) == NULL) {
		errCode   = CS_RPC_ERR_MISS_PARAM;
		snprintf(errMsg, sizeof(errMsg), "'%s' is missing", name);
		goto errExit;
	}

	if (!cJSON_IsNumber(jObj)) {
		errCode   = CS_RPC_ERR_PARAM_VAL;
		snprintf(errMsg, sizeof(errMsg), "'%s' is not a number", name);
		goto errExit;
	}

	*value = (float)jObj->valuedouble;
	return ESP_OK;

errExit:
	if (required)
		csRpcSendErrResponse(rpcCtx, errCode, errMsg);
	return ESP_FAIL;
}


esp_err_t csRpcReadParamDouble(
	csRpcCtx_t	rpcCtx,
	cJSON *		jParams,
	bool		required,
	char *		name,
	double *	value
)
{
	if (!rpcCtx)
		return ESP_ERR_INVALID_ARG;

	int		errCode;
	cJSON *	jObj;
	char	errMsg[60];

	if ((jObj = cJSON_GetObjectItem(jParams, name)) == NULL) {
		errCode   = CS_RPC_ERR_MISS_PARAM;
		snprintf(errMsg, sizeof(errMsg), "'%s' is missing", name);
		goto errExit;
	}

	if (!cJSON_IsNumber(jObj)) {
		errCode   = CS_RPC_ERR_PARAM_VAL;
		snprintf(errMsg, sizeof(errMsg), "'%s' is not a number", name);
		goto errExit;
	}

	*value = jObj->valuedouble;
	return ESP_OK;

errExit:
	if (required)
		csRpcSendErrResponse(rpcCtx, errCode, errMsg);
	return ESP_FAIL;
}


esp_err_t csRpcReadParamBool(
	csRpcCtx_t	rpcCtx,
	cJSON *		jParams,
	bool		required,
	char *		name,
	bool *		value
)
{
	if (!rpcCtx)
		return ESP_ERR_INVALID_ARG;

	int		errCode;
	cJSON *	jObj;
	char	errMsg[60];

	if ((jObj = cJSON_GetObjectItem(jParams, name)) == NULL) {
		errCode   = CS_RPC_ERR_MISS_PARAM;
		snprintf(errMsg, sizeof(errMsg), "'%s' is missing", name);
		goto errExit;
	}

	if (!cJSON_IsBool(jObj)) {
		errCode   = CS_RPC_ERR_PARAM_VAL;
		snprintf(errMsg, sizeof(errMsg), "'%s' is not a bool", name);
		goto errExit;
	}

	*value = cJSON_IsTrue(jObj) ? true : false;
	return ESP_OK;

errExit:
	if (required)
		csRpcSendErrResponse(rpcCtx, errCode, errMsg);
	return ESP_FAIL;
}


static csRpcMethodTab_t * findMethod(csRpcMethodTabList_t * tabList, const char * method)
{
	// Step through the linked-list of method tables
	while (tabList)
	{
		csRpcMethodTab_t *	tab   = tabList->tab;
		int					tabSz = tabList->tabSz;

		// Look for method match in this table
		int	i;
		for (i = 0; i < tabSz; i++, tab++) {
			if (strcmp(method, tab->method) == 0) {
				return tab;
			}
		}

		tabList = tabList->next;
	}

	return NULL;
}
