/*
 * cs_heap.h
 *
 *  Created on: Feb 13, 2019
 *      Author: wesd
 */

#ifndef COMPONENTS_CS_UTILS_INCLUDE_CS_HEAP_H_
#define COMPONENTS_CS_UTILS_INCLUDE_CS_HEAP_H_

#include <stddef.h>
#include <esp_system.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CS_HEAP_FLAGS		(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

IRAM_ATTR void * cs_heap_malloc(size_t siz);

IRAM_ATTR void * cs_heap_calloc(size_t num, size_t siz);

IRAM_ATTR void * cs_heap_realloc(void * old, size_t siz);

void cs_heap_free(void * ptr);

void cs_heap_check(void);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_CS_UTILS_INCLUDE_CS_HEAP_H_ */
