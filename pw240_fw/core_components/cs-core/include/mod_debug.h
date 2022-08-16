/*
 * mod_debug.h
 *
 */

#ifndef __CS_CORE_MOD_DEBUG_H__
#define __CS_CORE_MOD_DEBUG_H__

#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_IOT8020_DEBUG) && defined(MOD_NAME)
#define MOD_DEBUG	1
#else
#define MOD_DEBUG	0
#endif

#ifndef MOD_ERR
#define MOD_ERR		MOD_DEBUG
#endif

#if (MOD_DEBUG || MOD_ERR)

// Apply a default module name if none is given
#ifndef MOD_NAME
#define	MOD_NAME	"cs"
#endif

#include <stdint.h>
#include <stdio.h>
#include "time_mgr.h"

#endif

#if (MOD_DEBUG)
#define gc_dbg(_fmt_, ...)										\
	do {													\
		uint32_t	nowMs = (uint32_t)(esp_timer_get_time()/1000);		\
		ets_printf("[%lu.%03lu: " MOD_NAME "_dbg]" _fmt_ "\r\n", nowMs/1000, nowMs%1000, ##__VA_ARGS__);	\
	} while (0)

#else	// !MOD_DEBUG

#define gc_dbg(_fmt_, ...)	do {} while (0)

#endif	// MOD_DEBUG


#if (MOD_ERR)
#define gc_err(_fmt_, ...)										\
	do {													\
		uint32_t	nowMs = (uint32_t)(esp_timer_get_time()/1000);		\
		ets_printf("[%lu.%03lu: " MOD_NAME "_err]" _fmt_ "\r\n", nowMs/1000, nowMs%1000, ##__VA_ARGS__);	\
	} while (0)

#else	// !MOD_ERR

#define gc_err(_fmt_, ...)	do {} while (0)

#endif	// MOD_ERR

#if MOD_DEBUG || MOD_ERR
#define gc_hexDump(pData, len)					\
	do {										\
		hexDump((pData), (len));				\
	} while (0)

#define gc_hexDump2(title, pData, len, space)		\
	do {											\
		hexDump2((title), (pData), (len), (space));	\
	} while (0)

#define gc_textDump(title, pStr, len)			\
	do {										\
		textDump((title), (pStr), (len));		\
	} while (0)

void hexDump(const uint8_t * pData, int len);
void hexDump2(const char * title, uint8_t * pData, int len, bool space);
void textDump(const char * pTitle, const char * pStr, int len);


#else	// !(MOD_DEBUG || MOD_ERR)

#define gc_hexDump(pData, len)
#define gc_hexDump2(title, pData, len, space)
#define gc_textDump(title, pStr, len)

#endif	// (MOD_DEBUG || MOD_ERR)

#ifdef __cplusplus
}
#endif

#endif	// __CS_CORE_MOD_DEBUG_H__
