/*
 * app_pw_api.h
 *
 *  Created on: Jul 1, 2022
 *      Author: jonw
 */

#ifndef COMPONENTS_APP_PW_API_INCLUDE_APP_PW_API_H_
#define COMPONENTS_APP_PW_API_INCLUDE_APP_PW_API_H_

#ifdef __cplusplus
extern "C" {
#endif


esp_err_t appPWApiInit(void);

esp_err_t appPWApiStart(void);

esp_err_t appPWApiStop(void);

int appPWApiRegister();
esp_err_t appPWApiOTA();

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_APP_LOCAL_API_INCLUDE_APP_LOCAL_API_H_ */
