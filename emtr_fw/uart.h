/*
 * uart.h
 *
 *  Created on: Sep 30, 2016
 *      Author: cristian
 */

#ifndef PROJECT_UART_H_
#define PROJECT_UART_H_

#include "drivers.h"
#include "framework.h"

/*
 * TX_BUF_SIZE can hold maxim 32 signature samples (32 * 6 = 192)
 * SOP + CMD + LEN + [ TSTAMP:4 + PAGE:1 + SAMPLES:192 ] + CS + EOP
 */
//#define TX_BUF_SIZE		(1 + 1 + 1 + (4 + 1 + 192) + 1 + 1)
/*
 * TX_BUF_SIZE can hold maxim 32 signature samples (32 * 4 = 128)
 * SOP + CMD + LEN + [ TSTAMP:4 + PAGE:1 + SAMPLES:128 ] + CS + EOP
 */
#define TX_BUF_SIZE		(1 + 1 + 1 + (4 + 1 + 128) + 1 + 1)
#define RX_BUF_SIZE		8

#define SOP				0x1b
#define EOP				0x0a

extern volatile unsigned char rxBuf[RX_BUF_SIZE];
extern volatile unsigned char rxFlag, txFlag;
extern volatile unsigned char rxTimeOut;
extern volatile volatile int8 rxIdx;

void uart_init(void);
void uart_dbg_init(void);

int CheckRxBuf(void);

void Send0x00id(void);
void Send0x01Status(void);
void Send0x02Totals(void);
void Send0x03InstantValues(void);
void Send0x12FactoryTest(void);

void SendEpoch(void);
void SendWattHours(void);
void SendResults(void);
void SendStatus(void);
void SendOK(void);
//void SendLogPage(uint8 * page);
void SendCalibrationData(uint8_t code);
void SendChipID(void);
void SendCmdOK(uint8 cmd);
void SendCmdERROR(uint8 cmd);
void SendDET(uint8 result);
void SendFwVersion(void);
void SendAFE(int32 *result);
void SendCycles(void);
void SendHcciFlag(void);

void SendRawUI(uint8 channel, uint32 u, uint32 i);

void SendTimestamp(uint8 channel);

void SendRamData(uint8 cmd, uint8 page);

void SendMem(uint8 * addr, int len);

void uart_dbg_putc(uint8 c);
void uart_dbg_putc_cs(uint8 c, volatile uint8 * cs);
void uart_dbg_putx(uint8 c);
void wwSendStatus(void);

void sendPsigHeader(uint8 reason, uint16 packets, uint8 step);
void sendPsigPacket(void);
void sendSystemTestResults(void);

void dbgOnUart(char * msg, int len);

void uart_task_regular_cycle(void);
void uart_task_system_test(void);
void uart_task_factory_test(void);
void uart_task_power_up_test(void);

#endif /* PROJECT_UART_H_ */
