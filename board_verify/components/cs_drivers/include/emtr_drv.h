/*
 * emtr_drv.h
 *
 *  Created on: Jul 20, 2022
 *      Author: wesd
 */

#ifndef COMPONENTS_CS_DRIVERS_INCLUDE_EMTR_DRV_H_
#define COMPONENTS_CS_DRIVERS_INCLUDE_EMTR_DRV_H_

#include <esp_err.h>
#include <driver/uart.h>
#include <driver/gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	uart_port_t	uartPort;
	uint32_t	baudRate;
	gpio_num_t	rxGpio;
	gpio_num_t	txGpio;
	gpio_num_t	resetGpio;
} emtrDrvConf_t;


esp_err_t emtrDrvInit(emtrDrvConf_t* conf);

esp_err_t emtrDrvStart(void);


#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_CS_DRIVERS_INCLUDE_EMTR_DRV_H_ */
