/*
 * spiram_calloc.c
 *
 *  Created on: Feb 12, 2019
 *      Author: wesd
 */

#include "cs_common.h"
#include "esp_heap_caps.h"
#include "cs_heap.h"

// Comment out the MOD_NAME line to disable debug prints from this file
#define MOD_NAME	"cs_heap"
#include "mod_debug.h"


/**
 * \brief Allocate heap memory from SPIRAM
 */
IRAM_ATTR void * cs_heap_malloc(size_t siz)
{
	void *	ret = heap_caps_malloc(siz, CS_HEAP_FLAGS);

	if (!ret) {
		gc_err("malloc for %d bytes failed", siz);
	}

	return ret;
}


/**
 * \brief Allocate and zero heap memory from SPIRAM
 */
IRAM_ATTR void * cs_heap_calloc(size_t num, size_t siz)
{
	void *	ret = heap_caps_calloc(num, siz, CS_HEAP_FLAGS);

	if (!ret) {
		gc_err("calloc for %d bytes failed", siz);
	}

	return ret;
}


/**
 * \brief Allocate and zero heap memory from SPIRAM
 */
IRAM_ATTR void * cs_heap_realloc(void * old, size_t siz)
{
	void *	ret = heap_caps_realloc(old, siz, CS_HEAP_FLAGS);

	if (!ret) {
		gc_err("realloc for %d bytes failed", siz);
	}

	return ret;
}


/**
 * \brief Release memory allocated by cs_heap_XXXX
 */
void cs_heap_free(void * ptr)
{
	heap_caps_free(ptr);
}


void cs_heap_check(void)
{
#if MOD_DEBUG
	heap_caps_check_integrity_all(true);
#endif

}
