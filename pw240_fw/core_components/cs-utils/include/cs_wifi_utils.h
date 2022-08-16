/*
 * cs_wifi_utils.h
 *
 *  Created on: Nov 7, 2019
 *      Author: wesd
 *
 *      Modified on: Jun 15, 2022
 *      Author: jonw
 *
 */

#ifndef COMPONENTS_CS_UTILS_INCLUDE_CS_WIFI_UTILS_H_
#define COMPONENTS_CS_UTILS_INCLUDE_CS_WIFI_UTILS_H_

#include "cs_common.h"
#include "cs_heap.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief Scan for Wi-Fi access points and return a list
 *
 * \param [out] apRecs Pointer to pointer to be filled with array of APs
 * \param [out] apCount Pointer to where to store the number of found APs
 *
 *  The returned array of APs is allocated memory, the caller must use
 *  \ref csWifiReleaseApList() to release the memory once done with the list
 */
esp_err_t csWifiGetApList(wifi_ap_record_t ** apRecs, uint16_t * apCount);


/**
 * \brief Release apList that was create by \ref csWifiGetApList
 *
 * \param [in] Pointer to apList that was returned from csWifiGetApList
 *
 */
void csWifiReleaseApList(wifi_ap_record_t * apRecs);


/**
 * \brief Sort and filter the Wi-Fi scan list
 *
 * \param [in, out] pList Pointer to the list to access points
 * \param [in, out] pLength Pointer to list length
 *
 * This will do a case-sensitive sort of the Wi-Fi access point
 * list. It will filter out duplicate names, discarding those with lower
 * RSSI values.
 *
 */
void csWifiSortApList(wifi_ap_record_t * pList, uint16_t * pLength, const char * ssidFilter);

/**
 * \brief Print formatted access point list
 *
 * \param [in] title Pointer to title string. May be NULL to suppress the title
 * \param [in] apList Pointer to array of access point records
 * \param [in] apCount Number of records in the array
 */
void csWifiPrintApList(const char * title, wifi_ap_record_t * apList, uint16_t apCount);


#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_CS_UTILS_INCLUDE_CS_WIFI_UTILS_H_ */
