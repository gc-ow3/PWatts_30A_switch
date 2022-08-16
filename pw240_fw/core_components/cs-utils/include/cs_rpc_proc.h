/*
 * cs_rpc_proc.h
 *
 */

#ifndef COMPONENTS_CS_UTILS_INCLUDE_CS_RPC_PROC_H_
#define COMPONENTS_CS_UTILS_INCLUDE_CS_RPC_PROC_H_

// Standard C headers
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>

#include "cs_heap.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif


#define CS_RPC_ERR_BASE			(0)
#define CS_RPC_ERR_NOT_RPC		(CS_RPC_ERR_BASE +  1)	// Not 'jsonrpc'
#define CS_RPC_ERR_NO_ID		(CS_RPC_ERR_BASE +  2)	// Missing command id
#define CS_RPC_ERR_NO_METHOD	(CS_RPC_ERR_BASE +  3)	// Missing method
#define CS_RPC_ERR_NO_PARAMS	(CS_RPC_ERR_BASE +  4)	// Missing params
#define CS_RPC_ERR_NSUPP		(CS_RPC_ERR_BASE +  5)	// Method not supported
#define CS_RPC_ERR_NYET			(CS_RPC_ERR_BASE +  6)	// Method not yet implemented
#define CS_RPC_ERR_MISS_PARAM	(CS_RPC_ERR_BASE +  7)	// Missing expected parameter
#define CS_RPC_ERR_PARAM_LEN	(CS_RPC_ERR_BASE +  8)	// Parameter too large
#define CS_RPC_ERR_READ_FAIL	(CS_RPC_ERR_BASE +  9)	// Failed to read parameter
#define CS_RPC_ERR_INTERNAL		(CS_RPC_ERR_BASE + 10)	// Internal error
#define CS_RPC_ERR_BAD_JSON		(CS_RPC_ERR_BASE + 11)	// Improperly formatted JSON
#define CS_RPC_ERR_NO_MEM		(CS_RPC_ERR_BASE + 12)	// Cannot allocate memory
#define CS_RPC_ERR_SERVER_NACK	(CS_RPC_ERR_BASE + 13)	// Server rejected request
#define CS_RPC_ERR_TIMEOUT		(CS_RPC_ERR_BASE + 14)	// Timed out waiting for response
#define CS_RPC_ERR_PARAM_VAL	(CS_RPC_ERR_BASE + 15)	// Invalid parameter value

// RPC internal context handle
typedef void *		csRpcCtx_t;

// Function types

typedef void (* csRpcReplyFunc_t)(void * callerCtx, const char * msg, int len);

typedef esp_err_t (* csRpcCmdHandler_t)(void * callerCtx, csRpcCtx_t rpcCtx, cJSON * jParams);


/**
 * \brief This defines a table of methods and handlers
 */
typedef struct {
	const char *		method;
	csRpcCmdHandler_t	handler;
	bool				paramsReqd;
} const csRpcMethodTab_t;


/**
 * \brief This defines a linked list of method tables
 */
typedef struct csRpcMethodTabList_s	csRpcMethodTabList_t;

struct csRpcMethodTabList_s {
	csRpcMethodTabList_t *	next;
	csRpcMethodTab_t *		tab;
	int						tabSz;
};


esp_err_t csRpcProc(
	cJSON *					jRpc,
	csRpcMethodTabList_t *	tabList,
	csRpcReplyFunc_t		replyFunc,
	void *					callerCtx
);

void csRpcSendOkResponse(csRpcCtx_t rpcCtx, const char * respData);

void csRpcSendErrResponse(csRpcCtx_t rpcCtx, uint32_t errCode, const char * errMsg);

esp_err_t csRpcReadParamStr(
	csRpcCtx_t	rpcCtx,
	cJSON *		jParams,
	bool		required,
	char *		name,
	char *		buf,
	int			bufSz
);

const char * csRpcGetParamStrRef(
	csRpcCtx_t	rpcCtx,
	cJSON *		jParams,
	bool		required,
	char *		name
);

esp_err_t csRpcReadParamInt(
	csRpcCtx_t	rpcCtx,
	cJSON *		jParams,
	bool		required,
	char *		name,
	int *		value
);

esp_err_t csRpcReadParamFloat(
	csRpcCtx_t	rpcCtx,
	cJSON *		jParams,
	bool		required,
	char *		name,
	float *		value
);

esp_err_t csRpcReadParamDouble(
	csRpcCtx_t	rpcCtx,
	cJSON *		jParams,
	bool		required,
	char *		name,
	double *	value
);

esp_err_t csRpcReadParamBool(
	csRpcCtx_t	rpcCtx,
	cJSON *		jParams,
	bool		required,
	char *		name,
	bool *		value
);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_CS_UTILS_INCLUDE_CS_RPC_PROC_H_ */
