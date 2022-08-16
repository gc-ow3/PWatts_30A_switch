/*
 * utest_emtr.h
 *
 *  Created on: Jan 24, 2019
 *      Author: wesd
 */

#ifndef COMPONENTS_SELF_TEST_INCLUDE_CS_SELF_TEST_H_
#define COMPONENTS_SELF_TEST_INCLUDE_CS_SELF_TEST_H_

#include "esp_http_server.h"
#include "cJSON.h"
#include "cs_common.h"

#ifdef __cplusplus
extern "C" {
#endif


typedef struct {
	const char *	name;
	void			(* handler)(void * cbData, httpd_req_t * req, cJSON * jParam);
	bool			paramsRequired;
} const csSelfTestCmdTab_t;


typedef struct {
	httpd_handle_t				httpHandle;
	struct {
		const struct httpd_uri *	tab;
		int							tabSz;
		void *						cbData;
	} httpHandler;
	struct {
		csSelfTestCmdTab_t *	tab;
		int						tabSz;
		void *					cbData;
	} cmd;
} csSelfTestCfg_t;


esp_err_t csSelfTestInit(csSelfTestCfg_t * cfg);

esp_err_t csSelfTestTerm(void);

bool csSelfTestIsActive(void);

bool csSelfTestIsEnabled(void);

esp_err_t csSelfTestDisable(void);
esp_err_t csSelfTestEnable(void);

void csSelfTestSendOkResponse(httpd_req_t * pReq, const char * resp, cJSON * jData);

void csSelfTestSendErrResponse(httpd_req_t * pReq, const char * reason);


#ifdef __cplusplus
extern "C" {
#endif

#endif /* COMPONENTS_SELF_TEST_INCLUDE_CS_SELF_TEST_H_ */
