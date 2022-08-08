/*
 * uart.c
 *
 *  Created on: Sep 30, 2016
 *      Author: cristian
 */

#include <stdio.h>
#include "string.h"

#include "uart.h"
#include "i2c.h"
#include "framework.h"

#define 	DISCONNECT_NEUTRAL			GPIO_Set(GPIOG, PIN4)
#define 	CONNECT_NEUTRAL				GPIO_Clr(GPIOG, PIN4)

#define		CONNECT_LINE_RELAY			GPIO_Clr(GPIOF, PIN2)
#define		DISCONNECT_LINE_RELAY		GPIO_Set(GPIOF, PIN2)


volatile unsigned char rxBuf[RX_BUF_SIZE];
volatile volatile int8 rxIdx;
static volatile uint8 rxSC;
volatile unsigned char rxFlag, txFlag;
volatile unsigned char rxTimeOut;

static volatile unsigned char txBuf[TX_BUF_SIZE];
static volatile int txIdx, txLen;
static volatile uint8 txCS;

static void uart_callback (UART_CALLBACK_SRC module, UART_CALLBACK_TYPE type,
                           int32 status);


uint8 volatile reg;

const uint8 hex[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
void uart_dbg_init(void)
{
	reg = UART2_PFIFO;
	UART2_PFIFO = reg | 0x08;
	UART2_PFIFO = reg | 0x88;
	UART_Init   (UART2, UART_MODULE_POLLMODE_CONFIG(230400*8,48e6));
//	UART_Init   (UART2, UART_MODULE_POLLMODE_CONFIG(230400,48e6));
	PORT_Init   (PORTE, PORT_MODULE_ALT3_MODE, PIN6|PIN7);
}

void uart_dbg_putc_cs(uint8 c, volatile uint8 * cs)
{
	while(!UART_TxIdle(UART2));
	UART2_D = c;
	if (cs != NULL) *cs ^= c;
}

void uart_dbg_putc(uint8 c)
{
	while(!UART_TxIdle(UART2));
	UART2_D = c;
}

void uart_dbg_putx(uint8 c)
{
	uart_dbg_putc(hex[c>>4]);
	uart_dbg_putc(hex[c&0x0f]);
}

void uart_init(void)
{
	reg = UART1_PFIFO;
	UART1_PFIFO = reg | 0x08;
	UART1_PFIFO = reg | 0x88;
  PORT_Init   (PORTI, PORT_MODULE_ALT2_MODE, PIN0|PIN1);
  UART_Init   (UART1, UART_MODULE_INTRMODE_CONFIG(230400*4, 48e6));
  //UART_Init   (UART1, UART_MODULE_INTRMODE_CONFIG(115200, 48e6));
  //UART_Init   (UART1, UART_MODULE_POLLMODE_CONFIG(115200, 48e6));
  rxIdx=-1;
  UART_InstallCallback (UART0_UART1, PRI_LVL1, uart_callback);
}

int CheckRxBuf(void)
{
	uint8 cs;
	cs = 0;

	if (rxBuf[0] != SOP)
		return 0;

	if (rxBuf[7] != EOP)
		return 0;

	cs = rxBuf[1] ^ rxBuf[2] ^ rxBuf[3] ^ rxBuf[4] ^ rxBuf[5];

	if (cs == rxBuf[6])
		return 1;
	return 0;
}

static inline void StartUartTX(uint8 len)
{
	txBuf[2] = len - 5;
	txIdx = 0;
	txLen = len;
	txFlag = 1;
	txCS = 0;
	UART_TxIsrEnable (UART1);
	while (txFlag == 1);
}

void Send0x00id(void)
{
#ifdef CMD_DEBUG
	MY_DEBUG("[0x00] %c-%d.%d.%d\n", FW_VER_ID, VER_MAJOR, VER_MINOR, VER_PATCH);
#else
	uint8 idx = 0;
	txBuf[idx++]=SOP;
	txBuf[idx++]=0x00;				// cmd code
	txBuf[idx++]=0x00;				// payload len
	txBuf[idx++]=FW_VER_ID;
	txBuf[idx++]=VER_MAJOR;
	txBuf[idx++]=VER_MINOR;
	txBuf[idx++]=VER_PATCH;
	txBuf[idx++]=0x00;				// CS control sum, filled in by the interrupt code
	txBuf[idx++]=EOP;

	StartUartTX(idx);
#endif
}

void Send0x01Status(void)
{
#ifdef CMD_DEBUG
	MY_DEBUG("[0x01] %d %d %d\n", inst_vals.relay_status, inst_vals.alarm_status, inst_vals.temperature);
#else
	uint8 idx = 0;
	txBuf[idx++]=SOP;
	txBuf[idx++]=0x01;				// cmd code
	txBuf[idx++]=0x00;				// payload len
	txBuf[idx++]=inst_vals.relay_status;
	txBuf[idx++]=inst_vals.alarm_status;
	txBuf[idx++]=inst_vals.temperature;
	txBuf[idx++]=0x00;				// CS control sum, filled in by the interrupt code
	txBuf[idx++]=EOP;

	StartUartTX(idx);
#endif
}

void Send0x02Totals(void)
{
#ifdef CMD_DEBUG
	MY_DEBUG("[0x02] epoch=%u on=%u cycles=%u wh=%u\n", emtr_struct.epoch, emtr_struct.pump_epoch, emtr_struct.relay_cycles, emtr_struct.wh);
#else
	uint8 idx = 0;
	txBuf[idx++]=SOP;
	txBuf[idx++]=0x02;				// cmd code
	txBuf[idx++]=0x00;				// payload len
	txBuf[idx++]=(emtr_struct.epoch>>24)&0xff;
	txBuf[idx++]=(emtr_struct.epoch>>16)&0xff;
	txBuf[idx++]=(emtr_struct.epoch>> 8)&0xff;
	txBuf[idx++]=(emtr_struct.epoch>> 0)&0xff;
	txBuf[idx++]=(emtr_struct.pump_epoch>>24)&0xff;
	txBuf[idx++]=(emtr_struct.pump_epoch>>16)&0xff;
	txBuf[idx++]=(emtr_struct.pump_epoch>> 8)&0xff;
	txBuf[idx++]=(emtr_struct.pump_epoch>> 0)&0xff;
	txBuf[idx++]=(emtr_struct.relay_cycles>>24)&0xff;
	txBuf[idx++]=(emtr_struct.relay_cycles>>16)&0xff;
	txBuf[idx++]=(emtr_struct.relay_cycles>> 8)&0xff;
	txBuf[idx++]=(emtr_struct.relay_cycles>> 0)&0xff;
	txBuf[idx++]=(emtr_struct.wh>>24)&0xff;
	txBuf[idx++]=(emtr_struct.wh>>16)&0xff;
	txBuf[idx++]=(emtr_struct.wh>> 8)&0xff;
	txBuf[idx++]=(emtr_struct.wh>> 0)&0xff;
	txBuf[idx++]=0x00;				// CS control sum, filled in by the interrupt code
	txBuf[idx++]=EOP;

	StartUartTX(idx);
#endif
}

void Send0x03InstantValues(void)
{
#ifdef CMD_DEBUG
	MY_DEBUG("[0x03] U=%d I=%d, P=%d PF=%d pwr_on=%u relay_on=%u\n", inst_vals.u, inst_vals.i, inst_vals.p, inst_vals.pf, inst_vals.pwrOn, inst_vals.relayOn);
#else
	uint8 idx=0;
	txBuf[idx++]=SOP;
	txBuf[idx++]=0x03;
	txBuf[idx++]=0x00;				// payload len
	txBuf[idx++]=(inst_vals.u>>8)&0xff;
	txBuf[idx++]=(inst_vals.u>>0)&0xff;
	txBuf[idx++]=(inst_vals.i>>8)&0xff;
	txBuf[idx++]=(inst_vals.i>>0)&0xff;
	txBuf[idx++]=(inst_vals.p>> 8)&0xff;
	txBuf[idx++]=(inst_vals.p>> 0)&0xff;
	txBuf[idx++]=(inst_vals.pf>> 8)&0xff;
	txBuf[idx++]=(inst_vals.pf>> 0)&0xff;
	txBuf[idx++]=(inst_vals.pwrOn>>24)&0xff;
	txBuf[idx++]=(inst_vals.pwrOn>>16)&0xff;
	txBuf[idx++]=(inst_vals.pwrOn>> 8)&0xff;
	txBuf[idx++]=(inst_vals.pwrOn>> 0)&0xff;
	txBuf[idx++]=(inst_vals.relayOn>>24)&0xff;
	txBuf[idx++]=(inst_vals.relayOn>>16)&0xff;
	txBuf[idx++]=(inst_vals.relayOn>> 8)&0xff;
	txBuf[idx++]=(inst_vals.relayOn>> 0)&0xff;
	txBuf[idx++]=0x00;				// CS control sum, filled in by the interrupt code
	txBuf[idx++]=EOP;

	StartUartTX(idx);
#endif
}

void Send0x08CycleType(void)
{
	uint8 idx = 0;
	txBuf[idx++]=SOP;
	txBuf[idx++]=0x08;				// cmd code
	txBuf[idx++]=0x00;				// payload len
	txBuf[idx++]=global_cycle_type;
	txBuf[idx++]=0x00;				// CS control sum, filled in by the interrupt code
	txBuf[idx++]=EOP;

	StartUartTX(idx);
}

void Send0x12FactoryTest(void)
{
	uint8 idx = 0;
	txBuf[idx++]=SOP;
	txBuf[idx++]=0x12;				// cmd code
	txBuf[idx++]=0x00;				// payload len
	txBuf[idx++]=factory_test.tmr;
	txBuf[idx++]=factory_test.id;
	txBuf[idx++]=0x00;				// CS control sum, filled in by the interrupt code
	txBuf[idx++]=EOP;

	StartUartTX(idx);
}

void Send0x1CAtmPressCalib(void)
{
	uint8 idx = 0;
	txBuf[idx++]=SOP;
	txBuf[idx++]=0x1C;				// cmd code
	txBuf[idx++]=0x00;				// payload len
	txBuf[idx++]=(calib_data.atm_pres_mV >> 8) & 0xff;
	txBuf[idx++]=(calib_data.atm_pres_mV >> 0) & 0xff;
	txBuf[idx++]=0x00;				// CS control sum, filled in by the interrupt code
	txBuf[idx++]=EOP;

	StartUartTX(idx);
}


void SendEpoch(void)
{
	uint8 idx = 0;
	txBuf[idx++]=SOP;
	txBuf[idx++]=0x00;				// cmd code 0x00
	txBuf[idx++]=0x00;				// payload len
	txBuf[idx++]=(emtr_struct.epoch>>24)&0xff;
	txBuf[idx++]=(emtr_struct.epoch>>16)&0xff;
	txBuf[idx++]=(emtr_struct.epoch>> 8)&0xff;
	txBuf[idx++]=(emtr_struct.epoch>> 0)&0xff;
	txBuf[idx++]=0x00;				// CS control sum, filled in by the interrupt code
	txBuf[idx++]=EOP;

	StartUartTX(idx);
}

void SendCycles(void)
{
	uint8 idx = 0;
	txBuf[idx++]=SOP;
	txBuf[idx++]=0x04;				// cmd code 0x04
	txBuf[idx++]=0x00;				// payload len
	txBuf[idx++]=(emtr_struct.relay_cycles>>24)&0xff;
	txBuf[idx++]=(emtr_struct.relay_cycles>>16)&0xff;
	txBuf[idx++]=(emtr_struct.relay_cycles>> 8)&0xff;
	txBuf[idx++]=(emtr_struct.relay_cycles>> 0)&0xff;
	txBuf[idx++]=(emtr_struct.test_cycles>>24)&0xff;
	txBuf[idx++]=(emtr_struct.test_cycles>>16)&0xff;
	txBuf[idx++]=(emtr_struct.test_cycles>> 8)&0xff;
	txBuf[idx++]=(emtr_struct.test_cycles>> 0)&0xff;
	txBuf[idx++]=(emtr_struct.pump_epoch>>24)&0xff;
	txBuf[idx++]=(emtr_struct.pump_epoch>>16)&0xff;
	txBuf[idx++]=(emtr_struct.pump_epoch>> 8)&0xff;
	txBuf[idx++]=(emtr_struct.pump_epoch>> 0)&0xff;
	txBuf[idx++]=0x00;				// CS control sum, filled in by the interrupt code
	txBuf[idx++]=EOP;

	StartUartTX(idx);
}

#if 0
void SendStatus(void)
{
	uint8 idx = 0;
	txBuf[idx++]=SOP;
	txBuf[idx++]=0x01;
	txBuf[idx++]=0x00;				// payload len
	txBuf[idx++]=inst_vals.status;
	txBuf[idx++]=(inst_vals.temperature>> 8)&0xff;
	txBuf[idx++]=(inst_vals.temperature>> 0)&0xff;
	txBuf[idx++]=(emtr_struct.epoch>>24)&0xff;
	txBuf[idx++]=(emtr_struct.epoch>>16)&0xff;
	txBuf[idx++]=(emtr_struct.epoch>> 8)&0xff;
	txBuf[idx++]=(emtr_struct.epoch>> 0)&0xff;
	txBuf[idx++]=0x00;				// CS control sum, filled in by the interrupt code
	txBuf[idx++]=EOP;

	StartUartTX(idx);
}
#endif

void SendCmdOK(uint8 cmd)
{
	uint8 idx = 0;
	txBuf[idx++]=SOP;
	txBuf[idx++]=cmd;				// cmd code 0x00
	txBuf[idx++]=0x00;				// payload len
	txBuf[idx++]=0x01;				// OK
	txBuf[idx++]=0x00;				// CS control sum, filled in by the interrupt code
	txBuf[idx++]=EOP;

	StartUartTX(idx);
}

void SendCmdERROR(uint8 cmd)
{
	uint8 idx = 0;
	txBuf[idx++]=SOP;
	txBuf[idx++]=cmd;				// cmd code 0x00
	txBuf[idx++]=0x00;				// payload len
	txBuf[idx++]=0xff;				// ERROR
	txBuf[idx++]=0x00;				// CS control sum, filled in by the interrupt code
	txBuf[idx++]=EOP;

	StartUartTX(idx);
}

void SendAFE(int32 *result)
{
	uint8 idx = 0;
	int i;
	txBuf[idx++]=SOP;
	txBuf[idx++]=0x36;				// cmd code 0x00
	txBuf[idx++]=0x00;				// payload len
	for (i=0; i<4; i++)
	{
		txBuf[idx++]=(result[i]>>24)&0xff;
		txBuf[idx++]=(result[i]>>16)&0xff;
		txBuf[idx++]=(result[i]>>8)&0xff;
		txBuf[idx++]=(result[i]>>0)&0xff;
	}
	txBuf[idx++]=0x00;				// CS control sum, filled in by the interrupt code
	txBuf[idx++]=EOP;

	StartUartTX(idx);
}

void SendDET(uint8 result)
{
	uint8 idx = 0;
	txBuf[idx++]=SOP;
	txBuf[idx++]=0x36;				// cmd code 0x00
	txBuf[idx++]=0x00;				// payload len
	txBuf[idx++]=result;
	txBuf[idx++]=0x00;				// CS control sum, filled in by the interrupt code
	txBuf[idx++]=EOP;

	StartUartTX(idx);
}

void SendFwVersion(void)
{
	uint8 idx = 0;
	txBuf[idx++]=SOP;
	txBuf[idx++]=0x05;				// cmd code 0x00
	txBuf[idx++]=0x00;				// payload len
	txBuf[idx++]=VER_MAJOR;
	txBuf[idx++]=VER_MINOR;
	txBuf[idx++]=VER_PATCH;
	txBuf[idx++]=0x00;				// CS control sum, filled in by the interrupt code
	txBuf[idx++]=EOP;

	StartUartTX(idx);
}

void SendHcciFlag(void)
{
	uint8 idx = 0;
	txBuf[idx++]=SOP;
	txBuf[idx++]=0x22;				// cmd code 0x22
	txBuf[idx++]=0x00;				// payload len
	txBuf[idx++]=calib_data.hcci;
	txBuf[idx++]=0x00;				// CS control sum, filled in by the interrupt code
	txBuf[idx++]=EOP;

	StartUartTX(idx);
}

void SendSysTest(uint8_t code)
{
	uint8 idx = 0;
	txBuf[idx++]=SOP;
	txBuf[idx++]=code;				// cmd code 0x22
	txBuf[idx++]=0x00;				// payload len
	txBuf[idx++]=calib_data.sys_test;
	txBuf[idx++]=0x00;				// CS control sum, filled in by the interrupt code
	txBuf[idx++]=EOP;

	StartUartTX(idx);
}


void SendCalibrationData(uint8_t code)
{
#ifdef CMD_DEBUG
	MY_DEBUG("[%02X] u_gain=%08X i_gain=%08X\n", code, calib_data.u_gain, calib_data.i_gain);
#else
	uint8 idx=0;
	int i;
	txBuf[idx++]=SOP;
	txBuf[idx++]=code;
	txBuf[idx++]=0x00;				// payload len
	txBuf[idx++]=(calib_data.u_gain>>24)&0xff;
	txBuf[idx++]=(calib_data.u_gain>>16)&0xff;
	txBuf[idx++]=(calib_data.u_gain>> 8)&0xff;
	txBuf[idx++]=(calib_data.u_gain>> 0)&0xff;
	txBuf[idx++]=(calib_data.i_gain>>24)&0xff;
	txBuf[idx++]=(calib_data.i_gain>>16)&0xff;
	txBuf[idx++]=(calib_data.i_gain>> 8)&0xff;
	txBuf[idx++]=(calib_data.i_gain>> 0)&0xff;
	txBuf[idx++]=(calib_data.ilk_1_ma>>24)&0xff;
	txBuf[idx++]=(calib_data.ilk_1_ma>>16)&0xff;
	txBuf[idx++]=(calib_data.ilk_1_ma>> 8)&0xff;
	txBuf[idx++]=(calib_data.ilk_1_ma>> 0)&0xff;
	txBuf[idx++]=(calib_data.ilk_0_1_ma>>24)&0xff;
	txBuf[idx++]=(calib_data.ilk_0_1_ma>>16)&0xff;
	txBuf[idx++]=(calib_data.ilk_0_1_ma>> 8)&0xff;
	txBuf[idx++]=(calib_data.ilk_0_1_ma>> 0)&0xff;
	txBuf[idx++]=calib_data.hcci;
	txBuf[idx++]=(calib_data.atm_pres_mV >> 8) & 0xff;
	txBuf[idx++]=(calib_data.atm_pres_mV >> 0) & 0xff;
	txBuf[idx++]=0x00;				// CS control sum
	txBuf[idx++]=EOP;
	StartUartTX(idx);
#endif
}

void SendRawUI(uint8 channel, uint32 u, uint32 i)
{
	uint8 cmd;

	if (channel == 0)
		cmd = 0x15;
	else
		cmd = 0x25;
	uint8 idx=0;
	txBuf[idx++]=SOP;
	txBuf[idx++]=cmd;
	txBuf[idx++]=0x00;				// payload len
	txBuf[idx++]=(u>>24)&0xff;
	txBuf[idx++]=(u>>16)&0xff;
	txBuf[idx++]=(u>> 8)&0xff;
	txBuf[idx++]=(u>> 0)&0xff;
	txBuf[idx++]=(i>>24)&0xff;
	txBuf[idx++]=(i>>16)&0xff;
	txBuf[idx++]=(i>> 8)&0xff;
	txBuf[idx++]=(i>> 0)&0xff;
	txBuf[idx++]=0x00;				// CS control sum, filled in by the interrupt code
	txBuf[idx++]=EOP;

	StartUartTX(idx);
}

void SendOK(void)
{
#ifdef CMD_DEBUG
	MY_DEBUG("[0x??] OK\n");
#else
	uint8 idx=0;
	txBuf[idx++]=SOP;
	txBuf[idx++]=0xF0;
	txBuf[idx++]=0x00;				// payload len
	txBuf[idx++]=0x00;				// CS control sum, filled in by the interrupt code
	txBuf[idx++]=EOP;

	StartUartTX(idx);
#endif
}

void SendWattHours(void)
{
	uint8 idx=0;
	txBuf[idx++]=SOP;
	txBuf[idx++]=0x02;
	txBuf[idx++]=0x00;				// payload len
	txBuf[idx++]=(emtr_struct.wh>>24)&0xff;
	txBuf[idx++]=(emtr_struct.wh>>16)&0xff;
	txBuf[idx++]=(emtr_struct.wh>> 8)&0xff;
	txBuf[idx++]=(emtr_struct.wh>> 0)&0xff;
	txBuf[idx++]=0x00;				// CS control sum, filled in by the interrupt code
	txBuf[idx++]=EOP;

	StartUartTX(idx);
}

void SendResults(void)
{
	uint8 idx=0;
	txBuf[idx++]=SOP;
	txBuf[idx++]=0x03;
	txBuf[idx++]=0x00;				// payload len
	txBuf[idx++]=(inst_vals.u>>8)&0xff;
	txBuf[idx++]=(inst_vals.u>>0)&0xff;
	txBuf[idx++]=(inst_vals.i>>8)&0xff;
	txBuf[idx++]=(inst_vals.i>>0)&0xff;
	txBuf[idx++]=(inst_vals.p>> 8)&0xff;
	txBuf[idx++]=(inst_vals.p>> 0)&0xff;
	txBuf[idx++]=(inst_vals.pf>> 8)&0xff;
	txBuf[idx++]=(inst_vals.pf>> 0)&0xff;
	txBuf[idx++]=0x00;				// CS control sum, filled in by the interrupt code
	txBuf[idx++]=EOP;

	StartUartTX(idx);
}


void SendChipID(void)
{
	int i;
	uint8 * ptr_id;

	uint8 idx=0;
	txBuf[idx++]=SOP;
	txBuf[idx++]=0x07;
	txBuf[idx++]=0x00;				// payload len
	ptr_id = (uint8 *)0x4003F024;	//System Device ID
	for (i=0; i<4; i++)
		txBuf[idx++]=ptr_id[i];
	ptr_id = (uint8 *)0x4003F054;	//Chip Unique ID
	for (i=0; i<16; i++)
		txBuf[idx++]=ptr_id[i];
	txBuf[idx++]=0x00;				// CS control sum, filled in by the interrupt code
	txBuf[idx++]=EOP;

	StartUartTX(idx);
}

#if 0
void SendTimestamp(uint8 channel)
{
	uint8 cmd;
	switch (channel)
	{
	case 0:
		cmd = 0x12;
		break;
	case 1:
		cmd = 0x22;
		break;
	default:
		cmd = 0xff;
	}
	uint8 idx = 0;
	txBuf[idx++]=SOP;
	txBuf[idx++]=cmd;				// cmd code 0x12 or 0x22
	txBuf[idx++]=0x00;				// payload len
	txBuf[idx++]=(power_sig.chan_sig[channel].epoch>>24)&0xff;
	txBuf[idx++]=(power_sig.chan_sig[channel].epoch>>16)&0xff;
	txBuf[idx++]=(power_sig.chan_sig[channel].epoch>> 8)&0xff;
	txBuf[idx++]=(power_sig.chan_sig[channel].epoch>> 0)&0xff;
	txBuf[idx++]=(power_sig.chan_sig[channel].reason);
	txBuf[idx++]=0x00;				// CS control sum, filled in by the interrupt code
	txBuf[idx++]=EOP;

	StartUartTX(idx);
}

void SendRamData(uint8 channel, uint8 page)
{
	uint8 i;
	uint8 cmd;
	switch (channel)
	{
	case 0:
		cmd = 0x13;
		break;
	case 1:
		cmd = 0x23;
		break;
	default:
		cmd = 0xff;
	}
	uint8 idx=0;
	txBuf[idx++]=SOP;
	txBuf[idx++]=cmd;				// cmd code 0x13 or 0x23
	txBuf[idx++]=0x00;				// payload len
	txBuf[idx++]=(power_sig.chan_sig[channel].epoch>>24)&0xff;
	txBuf[idx++]=(power_sig.chan_sig[channel].epoch>>16)&0xff;
	txBuf[idx++]=(power_sig.chan_sig[channel].epoch>> 8)&0xff;
	txBuf[idx++]=(power_sig.chan_sig[channel].epoch>> 0)&0xff;
	txBuf[idx++]=page;
	for (i=0; i<32; i++)
	{
		txBuf[idx++]=(power_sig.chan_sig[channel].samples[page*32 + i][0][0])&0xff;
		txBuf[idx++]=(power_sig.chan_sig[channel].samples[page*32 + i][0][1])&0xff;
		txBuf[idx++]=(power_sig.chan_sig[channel].samples[page*32 + i][1][0])&0xff;
		txBuf[idx++]=(power_sig.chan_sig[channel].samples[page*32 + i][1][1])&0xff;
	}
	txBuf[idx++]=0x00;				// CS control sum, filled in by the interrupt code
	txBuf[idx++]=EOP;

	StartUartTX(idx);
}
#endif

#define DUMP_ENABLE 0
void SendMem(uint8 * addr, int len)
{
#if (DUMP_ENABLE)
	uint8 idx=0;
	int i;

	for (i=0; i<len; i++)
		txBuf[idx++]=addr[i];

	txIdx = 0;
	txLen = idx;
	txFlag = 1;
	UART_TxIsrEnable (UART1);
	while (txFlag == 1);
#endif
}

static unsigned char volatile c;
/* uart callback function definition                                          */
static void uart_callback (UART_CALLBACK_SRC module, UART_CALLBACK_TYPE type,
                           int32 int_status)
{
  if (module == UART1_CALLBACK)
  {
    if (type == UART_TX_CALLBACK)
    {
      if (txIdx<txLen)
      {
    	  if (txIdx == 1)
    		  txCS = 0;
    	  if (txIdx == txLen-2)
    		  txBuf[txIdx] = txCS;
    	  else
    		  txCS ^= txBuf[txIdx];
    	  UART_PutChar (UART1, txBuf[txIdx++]);
      }
      else
      {
    	  UART_TxIsrDisable (UART1);
    	  txFlag = 0;
      }
    }
    if (type == UART_RX_CALLBACK)
    {
    	c = UART_GetChar (UART1);
    	rxTimeOut = 20;
    	if (rxIdx < 0)
    		if (c == SOP)
    		{
    			rxIdx = 0;
    			rxSC = 0;
    			rxBuf[rxIdx++]=c;
    			return;
    		}
    	if (rxIdx == 1)	/* CMD_CODE */
    	{
			rxBuf[rxIdx++]=c;
			rxSC ^= c;
			return;
    	}
    	if (rxIdx == 2) /* payload[0] */
    	{
			rxBuf[rxIdx++]=c;
			rxSC ^= c;
			return;
    	}
    	if (rxIdx == 3) /* payload[1] */
    	{
			rxBuf[rxIdx++]=c;
			rxSC ^= c;
			return;
    	}
    	if (rxIdx == 4) /* payload[2] */
    	{
			rxBuf[rxIdx++]=c;
			rxSC ^= c;
			return;
    	}
    	if (rxIdx == 5) /* payload[3] */
    	{
			rxBuf[rxIdx++]=c;
			rxSC ^= c;
			return;
    	}
    	if (rxIdx == 6)	/* SC */
    	{
			rxBuf[rxIdx++]=c;
			return;
    	}
    	if (rxIdx == 7)
    	{
        	rxTimeOut = 0;
			rxBuf[rxIdx]=c;
			rxIdx = -1;
			rxFlag = 1;
			return;
    	}
    }
  }
}

void sendSystemTestResults(void)
{
#ifdef USE_UART2
//	system_test_results.payload = 11;
	system_test_results.payload = 12;
	uart_dbg_putc(SOP);
	uart_dbg_putc_cs('T', &system_test_results.cs);
	uart_dbg_putc_cs((system_test_results.payload >> 8) & 0xff, &system_test_results.cs);
	uart_dbg_putc_cs((system_test_results.payload >> 0) & 0xff, &system_test_results.cs);
//	uart_dbg_putc_cs((system_test_results.gfci_level >> 0) & 0xff, &system_test_results.cs);
	uart_dbg_putc_cs((system_test_results.leackage_current >> 8) & 0xff, &system_test_results.cs);
	uart_dbg_putc_cs((system_test_results.leackage_current >> 0) & 0xff, &system_test_results.cs);
	uart_dbg_putc_cs((system_test_results.power_line_fault >> 0) & 0xff, &system_test_results.cs);
	uart_dbg_putc_cs((system_test_results.temperature >> 0) & 0xff, &system_test_results.cs);
	uart_dbg_putc_cs((system_test_results.water_level_start >> 0) & 0xff, &system_test_results.cs);
	uart_dbg_putc_cs((system_test_results.cycle_len >> 8) & 0xff, &system_test_results.cs);
	uart_dbg_putc_cs((system_test_results.cycle_len >> 0) & 0xff, &system_test_results.cs);
	uart_dbg_putc_cs((system_test_results.u >> 8) & 0xff, &system_test_results.cs);
	uart_dbg_putc_cs((system_test_results.u >> 0) & 0xff, &system_test_results.cs);
	uart_dbg_putc_cs((system_test_results.i >> 8) & 0xff, &system_test_results.cs);
	uart_dbg_putc_cs((system_test_results.i >> 0) & 0xff, &system_test_results.cs);
	uart_dbg_putc_cs((system_test_results.pf >> 0) & 0xff, &system_test_results.cs);
	uart_dbg_putc(system_test_results.cs);
	uart_dbg_putc(EOP);
#endif
}

/*
 * reason = 0:pump_off, 1:pump_on, 2:on_demand
 * packets = [0...2000]
 * step = 0:12KHz, 1:6KHz, 2:4KHz, 3:3KHz, 4:2.4KHz, 5:2KHz
 */
void sendPsigHeader(uint8 reason, uint16 packets, uint8 step)
{
	if (packets > 2000)
		packets = 2000;
	if (packets == 0)
		packets = 200;
	if (step > 5)
		step = 5;
	power_sig.step = step;
	power_sig.cs = 0;
	power_sig.payload = packets * 4;
#if 1
	power_sig.payload += 30;
#endif
	//power_sig.water_level 	= inst_vals.water_level;
	//power_sig.gfci_level  	= inst_vals.gfci_level;
	power_sig.epoch			= emtr_struct.epoch;
	power_sig.relay_cycles 	= emtr_struct.relay_cycles;
	power_sig.pump_epoch 	= emtr_struct.pump_epoch;
	if (power_sig.cycle_len == 0) {
		power_sig.cycle_len = 1;
		power_sig.cycle_i = inst_vals.i;
		power_sig.cycle_u = inst_vals.u;
		power_sig.cycle_pf = inst_vals.pf;
		power_sig.cycle_t = inst_vals.temperature;
	}
	else {
		power_sig.cycle_i = power_sig.cycle_i/power_sig.cycle_len;
		power_sig.cycle_u = power_sig.cycle_u/power_sig.cycle_len;
		power_sig.cycle_pf = power_sig.cycle_pf/power_sig.cycle_len;
		power_sig.cycle_t = power_sig.cycle_t/power_sig.cycle_len;
	}
	if (reason != 0) {
		power_sig.cycle_len = 0;
		power_sig.cycle_i = 0xffffffff;
		power_sig.cycle_u = 0xffffffff;
		power_sig.cycle_pf = 0xffffffff;
		power_sig.cycle_t = 0xffffffff;
	}
	if (reason == 0) {
		MY_DEBUG("==== PUMP STOP ====");
		MY_DEBUG("- I  = %d\n", power_sig.cycle_i);
		MY_DEBUG("- U  = %d\n", power_sig.cycle_u);
		MY_DEBUG("- PF = %d\n", power_sig.cycle_pf);
		MY_DEBUG("- T  = %d\n", power_sig.cycle_t);
	}

#ifdef USE_UART2
	uart_dbg_putc(SOP);
	uart_dbg_putc_cs('S', &power_sig.cs);
	uart_dbg_putc_cs((power_sig.payload >> 8) & 0xff, &power_sig.cs);
	uart_dbg_putc_cs((power_sig.payload >> 0) & 0xff, &power_sig.cs);
	uart_dbg_putc_cs(reason, &power_sig.cs);
	uart_dbg_putc_cs((power_sig.relay_cycles >> 24) & 0xff, &power_sig.cs);
	uart_dbg_putc_cs((power_sig.relay_cycles >> 16) & 0xff, &power_sig.cs);
	uart_dbg_putc_cs((power_sig.relay_cycles >>  8) & 0xff, &power_sig.cs);
	uart_dbg_putc_cs((power_sig.relay_cycles >>  0) & 0xff, &power_sig.cs);
#if 1
	// uint32:cycle_len
	power_sig.cycle_len += power_sig.cycle_wait;
	uart_dbg_putc_cs((power_sig.cycle_len >> 24) & 0xff, &power_sig.cs);
	uart_dbg_putc_cs((power_sig.cycle_len >> 16) & 0xff, &power_sig.cs);
	uart_dbg_putc_cs((power_sig.cycle_len >>  8) & 0xff, &power_sig.cs);
	uart_dbg_putc_cs((power_sig.cycle_len >>  0) & 0xff, &power_sig.cs);
	// uint16:cycle_i
	uart_dbg_putc_cs((power_sig.cycle_i >>  8) & 0xff, &power_sig.cs);
	uart_dbg_putc_cs((power_sig.cycle_i >>  0) & 0xff, &power_sig.cs);
	// uint16:cycle_u
	uart_dbg_putc_cs((power_sig.cycle_u >>  8) & 0xff, &power_sig.cs);
	uart_dbg_putc_cs((power_sig.cycle_u >>  0) & 0xff, &power_sig.cs);
	// uint8:cycle_pf
	uart_dbg_putc_cs((power_sig.cycle_pf >>  0) & 0xff, &power_sig.cs);
	// uint8:cycle_t
	uart_dbg_putc_cs((power_sig.cycle_t >>  0) & 0xff, &power_sig.cs);
#endif
	uart_dbg_putc_cs((power_sig.epoch >> 24) & 0xff, &power_sig.cs);
	uart_dbg_putc_cs((power_sig.epoch >> 16) & 0xff, &power_sig.cs);
	uart_dbg_putc_cs((power_sig.epoch >>  8) & 0xff, &power_sig.cs);
	uart_dbg_putc_cs((power_sig.epoch >>  0) & 0xff, &power_sig.cs);
	uart_dbg_putc_cs((power_sig.pump_epoch >> 24) & 0xff, &power_sig.cs);
	uart_dbg_putc_cs((power_sig.pump_epoch >> 16) & 0xff, &power_sig.cs);
	uart_dbg_putc_cs((power_sig.pump_epoch >>  8) & 0xff, &power_sig.cs);
	uart_dbg_putc_cs((power_sig.pump_epoch >>  0) & 0xff, &power_sig.cs);
	uart_dbg_putc_cs((power_sig.water_level >> 0) & 0xff, &power_sig.cs);
	power_sig.gfci_level = 0;
	uart_dbg_putc_cs((power_sig.gfci_level >> 0) & 0xff, &power_sig.cs);
	uart_dbg_putc_cs(power_sig.step, &power_sig.cs);
	while(!UART_TxIdle(UART2));
	power_sig.packets = packets; //this will trigger power_sig transmission in the afe_callback interrupt
#endif
}


void sendPsigPacket(void)
{
#ifdef USE_UART2
	uint32 u, i;
	if (power_sig.packets == 0)
		return;
	u = u1q;
	i = i1q;
	power_sig.cs ^= (u>>16)&0xff; UART2_D= (u>>16)&0xff;
	power_sig.cs ^= (u>> 8)&0xff; UART2_D= (u>> 8)&0xff;
	power_sig.cs ^= (i>>16)&0xff; UART2_D= (i>>16)&0xff;
	power_sig.cs ^= (i>> 8)&0xff; UART2_D= (i>> 8)&0xff;
	power_sig.packets--;
	if (power_sig.packets == 0) {
		UART2_D = power_sig.cs;
		UART2_D = EOP;
	}
#endif
}


void dbgOnUart(char * msg, int len) {
	int i=0;
	DisableInterrupts ();
	while (len>0) {
		while(!UART_TxIdle(UART1));
		UART1_D = msg[i++];
		len--;
	};
	while(!UART_TxIdle(UART1));
	EnableInterrupts ();
}


void uart_task_regular_cycle(void) {
	uint32_t tmp32;
	if (rxFlag==1) {
		inst_vals.wdog_timer = 0;
		switch (rxBuf[1]) {
		case 0x00:
			Send0x00id();
			break;
		case 0x01:
			Send0x01Status();
			break;
		case 0x02:
			Send0x02Totals();
			break;
		case 0x03:
			Send0x03InstantValues();
			break;
		case 0x04:
			if (power_sig.packets > 0) {
				// power sig in progress
				SendCmdERROR(0x04);
			}
			else {
				uint16 packets;
				SendOK();
				packets = rxBuf[2];
				packets = (packets << 8) + rxBuf[3];
				sendPsigHeader(2, packets, rxBuf[4]); // reason 2 = on demand
			}
			break;
		case 0x05:
			if (rxBuf[2] == 0x00) {
				relayClr();
			}
			else {
				relaySet();
			}
			SendOK();
			break;
		case 0x07:
			SendChipID();
			break;
		case 0x08:	//???
			Send0x08CycleType();
			break;
		case 0x0D:
			SendCalibrationData(0x0d);
			break;
		case 0x0E:	// write calibration data to eeprom
			i2c_noirq_write_calib_data();
			SendCalibrationData(0x0e);
			break;
		case 0x0F:
			SendOK();
			i2c_noirg_eeprom_erase();
			SystemReset();
			break;
#if 0
		case 0x12:  // factory test related
			if (rxBuf[2] != 0x00) {
				//global_cycle_type = factory_test_cycle;
				switchToCycleType(factory_test_cycle);
				inst_vals.relay_status = FACTORY_TEST;
				factory_test.tmr = rxBuf[2];
				factory_test.id  = rxBuf[3];
				factory_test.khz = 0;
				start_factory_test(factory_test.id);
			}
			Send0x12FactoryTest();
			break;
#endif
		case 0x1A:
			tmp32 = rxBuf[2];
			tmp32 = (tmp32 << 8) | rxBuf[3];
			tmp32 = (tmp32 << 8) | rxBuf[4];
			tmp32 = (tmp32 << 8) | rxBuf[5];
			calib_data.u_gain = tmp32;
			SendOK();
			break;
		case 0x1B:
			tmp32 = rxBuf[2];
			tmp32 = (tmp32 << 8) | rxBuf[3];
			tmp32 = (tmp32 << 8) | rxBuf[4];
			tmp32 = (tmp32 << 8) | rxBuf[5];
			calib_data.i_gain = tmp32;
			SendOK();
			break;
		case 0x41:
			// write calibration value for 0.1 mA in RAM memory
			tmp32 = rxBuf[2];
			tmp32 = (tmp32 << 8) | rxBuf[3];
			tmp32 = (tmp32 << 8) | rxBuf[4];
			tmp32 = (tmp32 << 8) | rxBuf[5];
			calib_data.ilk_0_1_ma = tmp32;
			SendOK();
			break;
		case 0x42:
			// write calibration value for 1 mA in RAM memory
			tmp32 = rxBuf[2];
			tmp32 = (tmp32 << 8) | rxBuf[3];
			tmp32 = (tmp32 << 8) | rxBuf[4];
			tmp32 = (tmp32 << 8) | rxBuf[5];
			calib_data.ilk_1_ma = tmp32;
			SendOK();
			break;
		default:
			SendCmdERROR(rxBuf[1]);
		}
		rxFlag=0;
	} // end if (rxFlag == 1)
}


/*
Command 0x00 GetID
1b 00 00 00 00 00 00 0a

Command 0x00 GetStatus
1b 01 00 00 00 00 00 0a


*/
