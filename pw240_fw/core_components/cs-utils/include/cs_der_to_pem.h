/*
 * cs_der_to_pem.h
 *
 *  Created on: May 7, 2020
 *      Author: wesd
 */

#ifndef COMPONENTS_CS_UTILS_INCLUDE_CS_DER_TO_PEM_H_
#define COMPONENTS_CS_UTILS_INCLUDE_CS_DER_TO_PEM_H_

#include "cs_common.h"
#include "cs_heap.h"

/**
 * \brief Convert DER form of a certificate to PEM form
 *
 * \param [in] der Buffer holding DER certificate
 * \param [in] derLen Number of DER bytes
 *
 * \return Allocated memory holding PEM certificate string
 * \return NULL if conversion failed
 *
 * \note The returned PEM is allocated memory, call cs_heap_free() to
 * release it when done with the data
 *
 */
char * csDerToPemCert(const uint8_t * der, int derLen);

#endif /* COMPONENTS_CS_UTILS_INCLUDE_CS_DER_TO_PEM_H_ */
