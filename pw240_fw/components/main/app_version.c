/*
 * app_version.c
 *
 *  Created on: Apr 13, 2022
 *      Author: jonw
 */
#include "cs_platform.h"

/**
 * \brief version of application firmware
 *
 * See release notes below
 *
 */
const char	appFwVer[] = {"0.2.1"};


//******************************************************************************
// Release notes
// v0.1.3
// - Change MAC address to lower-case letters
// - Lower stack usage for sending events
// - Wrap mfg_data partition access in a semaphore
// - Fix some events being sent at the wrong times
//
// v0.1.2 - Updated Local API to include the following
//  - Event notifications to event URI
//  - Consumption data in status report
//  - Factory reset and reboot commands in settings response
//
// v0.1.1 - Alpha tested MVP with MFG mode back door
//  - Factory reset via Outlet 2 plug/unlpug will put the device into PW-MFG mode without deleting the PW mfg namespace
//  - MVP tested against hub test firmware (cloud registration disabled)
//
// v0.1.0 - Full untested MVP
//   - OTA over LR (fw_update component updated)
//   - New local API over HTTP (app_pw_api component)
//   - LR provisioning complete
//
// v0.0.1 - Initial commit. It only scans for LR access points starting with "PWGW" and print them to the debug port
//   - Begin tracking releases
//   - HomeKit, AWS, and SDDP removed from IWO project
//   - esp-idf updated to v4.4.1
//   - cs-lr-prov component added (this is a starting point and will be updated in next release)
//   - cs_wifi_utils modified to allow for SSID filtering
//
//******************************************************************************
