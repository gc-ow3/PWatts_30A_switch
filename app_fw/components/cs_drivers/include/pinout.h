/* Copyright (C) Grid Connect, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 */
 
#ifndef __PINOUT_H__
#define __PINOUT_H__
 
#include <driver/gpio.h>

// EMTR reset active low
#define EMTR_RST_GPIO	(GPIO_NUM_18)

// Button1 input (On/Off) active low
#define BUTTON1_GPIO	(GPIO_NUM_32)

// Button2 input (Config) active low
#define BUTTON2_GPIO    (GPIO_NUM_35)

// Switch1 input (On/Off) active low
#define SWITCH1_GPIO    (GPIO_NUM_33)

// UART1 pins (EMTR command)
#define UART1_TX_GPIO	(GPIO_NUM_2)
#define UART1_RX_GPIO	(GPIO_NUM_4)

// UART2 pins (EMTR power signature)
#define UART2_RX_GPIO	(GPIO_NUM_34)

// LED1 RGB pins
#define LED1_R_GPIO     (GPIO_NUM_25)
#define LED1_G_GPIO     (GPIO_NUM_26)
#define LED1_B_GPIO     (GPIO_NUM_27)

// LED2 RGB pins
#define LED2_R_GPIO     (GPIO_NUM_21)
#define LED2_G_GPIO     (GPIO_NUM_22)
#define LED2_B_GPIO     (GPIO_NUM_23)

#endif
