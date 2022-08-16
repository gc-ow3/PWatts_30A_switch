/*
 * cs_wifi_utils.c
 *
 *  Created on: Nov 7, 2019
 *      Author: wesd
 *
 *      Modified on: Jun 15, 2022
 *      Author: jonw
 *
 */

#include "cs_common.h"
#include "cs_heap.h"
#include "cs_wifi_utils.h"

// Comment out the MOD_NAME line to disable debug prints from this file
#define MOD_NAME	"cs_wifi_utils"
#include "mod_debug.h"
//#define DEBUG_SORT

esp_err_t csWifiGetApList(wifi_ap_record_t ** apRecs, uint16_t * apCount)
{
	esp_err_t	status;

	*apRecs  = NULL;
	*apCount = 0;

	wifi_scan_config_t	scanCfg = {
		.ssid                 = NULL,
		.bssid                = NULL,
		.channel              = 0,
		.show_hidden          = true,
		.scan_type            = WIFI_SCAN_TYPE_ACTIVE,
//		.scan_time.active.min = 200,
//		.scan_time.active.max = 1000,
	};

	gc_dbg("Start AP scan");

#if CONFIG_IOT8020_DEBUG
	// Measure how long it took to scan
	uint64_t	tStartMs = timeMgrGetUptimeMs();
#endif

	int			tries;
	uint16_t	numRecs;

	for (tries = 0; tries < 4; tries++) {
		// Run the AP scan, wait for it to complete
		status = esp_wifi_scan_start(&scanCfg, true);
		if (ESP_OK != status) {
			gc_err("esp_wifi_scan_start error %X", status);
			return status;
		}
		(void)esp_wifi_scan_stop();

		// Retrieve the number of APs found
		esp_wifi_scan_get_ap_num(&numRecs);

		if (numRecs > 0)
			break;

		// No APs founds, try again in 500 ms
		gc_dbg("Scan found no Wi-Fi APs - retry");
		vTaskDelay(pdMS_TO_TICKS(500));
	}

#if CONFIG_IOT8020_DEBUG
	// Compute elapsed scan time
	uint32_t	tDiffMs = (uint32_t)(timeMgrGetUptimeMs() - tStartMs);

	gc_dbg("Found %u access points in %u.%u seconds", numRecs, tDiffMs/1000, tDiffMs%1000);
#endif

	if (numRecs > 0) {
		wifi_ap_record_t *	apList;

		// Allocate space to hold the wifi AP list array
		apList = cs_heap_calloc(numRecs, sizeof(wifi_ap_record_t));
		if (apList) {
			// Read the AP list
			status = esp_wifi_scan_get_ap_records(&numRecs, apList);
			if (ESP_OK == status) {
				// Pass back the results
				*apRecs  = apList;
				*apCount = numRecs;
			} else {
				gc_err("esp_wifi_scan_get_ap_records error %d", status);
				cs_heap_free(apList);
			}
		} else {
			gc_err("Failed to allocate memory for AP list");
			status = ESP_ERR_NO_MEM;
		}
	}

	return status;
}


void csWifiReleaseApList(wifi_ap_record_t * apRecs)
{
	if (apRecs) {
		cs_heap_free(apRecs);
	}
}


void csWifiSortApList(wifi_ap_record_t * pList, uint16_t * pLength, const char * ssidFilter)
{
	wifi_ap_record_t	swap;
	wifi_ap_record_t *	pSlot;
	wifi_ap_record_t *	pTest;
	wifi_ap_record_t *	pHigh;
	unsigned int		i1;
	unsigned int		i2;

#ifdef DEBUG_SORT
	csWifiPrintApList("Before sort", pList, *pLength);
#endif

	// Sort by ascending SSID
	pSlot = pList;
	for (i1 = 0; i1 < *pLength - 1; i1++, pSlot++) {
		pHigh = pSlot;
		pTest = pSlot + 1;

		for (i2 = 0; i2 < *pLength - i1 - 1; i2++, pTest++) {
			if (strcmp((char *)pTest->ssid, (char *)pHigh->ssid) < 0) {
				pHigh = pTest;
			}
		}

		if (pHigh != pSlot) {
			swap   = *pSlot;
			*pSlot = *pHigh;
			*pHigh = swap;
		}
	}

	// Next sort matching SSIDs by descending RSSI
	pSlot = pList;
	for (i1 = 0; i1 < *pLength - 1; i1++, pSlot++) {
		pHigh = pSlot;
		pTest = pSlot + 1;

		for (i2 = 0; i2 < *pLength - i1 - 1; i2++, pTest++) {
			int	ssid1Len = strlen((char *)pSlot->ssid);
			int	ssid2Len = strlen((char *)pTest->ssid);

			if (ssid1Len != ssid2Len) {
				break;
			}
			if (memcmp(pSlot->ssid, pTest->ssid, ssid1Len) != 0) {
				break;
			}

			if (pTest->rssi > pHigh->rssi) {
				pHigh = pTest;
			}
		}

		if (pHigh != pSlot) {
			swap   = *pSlot;
			*pSlot = *pHigh;
			*pHigh = swap;
		}
	}

#ifdef DEBUG_SORT
	csWifiPrintApList("Sorted by SSID", pList, *pLength);
#endif
#ifdef SORT_REMOVE_DUPLICATES
	// Now step through the list, removing duplicate SSIDs
	pSlot = pList;
	for (i1 = 0; i1 < *pLength - 1; i1++, pSlot++) {
		pTest = pSlot + 1;
		for (i2 = i1 + 1; i2 < *pLength; ) {
			int	ssid1Len = strlen((char *)pSlot->ssid);
			int	ssid2Len = strlen((char *)pTest->ssid);
			int	shiftCt;


			if (ssid1Len != ssid2Len) {
				break;
			}
			if (memcmp(pSlot->ssid, pTest->ssid, ssid1Len) != 0) {
				break;
			}

			shiftCt = (*pLength - i2 - 1);
			if (shiftCt) {
				memcpy(pTest, pTest + 1, shiftCt * sizeof(wifi_ap_record_t));
			}

			*pLength -= 1;
		}
	}

#ifdef DEBUG_SORT
	csWifiPrintApList("Sorted with duplicates removed", pList, *pLength);
#endif
#endif
	// Sort by descending RSSI
	pSlot = pList;
	for (i1 = 0; i1 < *pLength - 1; i1++, pSlot++) {
		pHigh = pSlot;
		pTest = pSlot + 1;

		for (i2 = 0; i2 < *pLength - i1 - 1; i2++, pTest++) {

			if (pTest->rssi > pHigh->rssi) {
				pHigh = pTest;
			}
		}

		if (pHigh != pSlot) {
			swap   = *pSlot;
			*pSlot = *pHigh;
			*pHigh = swap;
		}
	}

#ifdef DEBUG_SORT
	csWifiPrintApList("Sorted by RSSI", pList, *pLength);
#endif
	if(ssidFilter){
		// Now step through the list, filtering out SSIDs we are not looking for
		pSlot = pList;
		for (i1 = 0; i1 < *pLength; i1++, pSlot++) {
			pTest = pSlot + 1;
			for (i2 = i1 + 1; i2 <= *pLength; ) {
				int	ssid1Len = strlen((char *)pSlot->ssid);
				int	ssid2Len = strlen(ssidFilter);
				int	shiftCt;


				if (ssid1Len >= ssid2Len && strstr((char *)pSlot->ssid, ssidFilter)) {
					break;
				}

				shiftCt = (*pLength - i2);
				if (shiftCt) {
					memcpy(pSlot, pSlot + 1, shiftCt * sizeof(wifi_ap_record_t));
				}

				*pLength -= 1;
			}
		}

#ifdef DEBUG_SORT
		csWifiPrintApList("Filtered", pList, *pLength);
#endif
	}
}


void csWifiPrintApList(const char * title, wifi_ap_record_t * apList, uint16_t apCount)
{
	gc_dbg("");
	gc_dbg("Wi-Fi AP list (%u records)", apCount);
	if (title)
		gc_dbg("%s", title);
	gc_dbg("Chan  RSSI  SSID");
	gc_dbg("----  ----  --------------------------------");
	int					i;
	wifi_ap_record_t *	rec;
	for (i = 0, rec = apList; i < apCount; i++, rec++) {
		gc_dbg("%4u  %4d  %s", rec->primary, rec->rssi, rec->ssid);
	}
	gc_dbg("");
}
