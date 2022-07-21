/*
 */

#ifndef __UART_LOOP_H__
#define __UART_LOOP_H__

#include <freertos/FreeRTOS.h>
#include <esp_err.h>
#include <esp_log.h>
#include <driver/uart.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t uartLoopStart(uart_port_t port, uint32_t baud);

esp_err_t uartLoopStop(uart_port_t port);

esp_err_t uartSpyStart(uart_port_t port, uint32_t baud);

esp_err_t uartSpyStop(uart_port_t port);

#ifdef __cplusplus
}
#endif

#endif /* __UART_LOOP_H__ */
