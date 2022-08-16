/*! \file time_mgr.c
 *
 * \brief Keep track of elapsed time since boot and UTC time
 *
 */

#include <sys/cdefs.h>
#include <sys/time.h>
#include <time.h>
#include "cs_heap.h"
#include "time_mgr.h"

// Comment out the MOD_NAME line to disable debug prints from this file
#define MOD_NAME	"cs_time_mgr"
#include "mod_debug.h"


////////////////////////////////////////////////////////////////////////////////
// Defines
////////////////////////////////////////////////////////////////////////////////

#define CS_TIMER_PERIOD_MS		(10)


////////////////////////////////////////////////////////////////////////////////
// Types
////////////////////////////////////////////////////////////////////////////////

typedef struct {
	esp_timer_handle_t	timer;
	uint32_t			uptime;
	uint64_t			uptimeMs;
	uint32_t			utcTime;
	uint32_t			lastUtcTimeSet;
	int32_t				localTimeAdjust;
	bool				localTimeSet;
	uint32_t			msCount;
} timeCtrl_t;


////////////////////////////////////////////////////////////////////////////////
// Local functions
////////////////////////////////////////////////////////////////////////////////
static void timerCb(void * arg);


////////////////////////////////////////////////////////////////////////////////
// Constant data
////////////////////////////////////////////////////////////////////////////////
static portMUX_TYPE	timeLock = portMUX_INITIALIZER_UNLOCKED;


////////////////////////////////////////////////////////////////////////////////
// Local data
////////////////////////////////////////////////////////////////////////////////

static timeCtrl_t *	timeCtrl;


/**
 * \brief Initialize the time manager
 *
 * \return ESP_OK Successful
 * \return (other) Failed
 *
 * Initialize control structure and resources used by the time manager
 *
 */
esp_err_t timeMgrInit(void)
{
	if (NULL != timeCtrl)
		return ESP_OK;

	timeCtrl_t *	pCtrl;

	if ((pCtrl = cs_heap_calloc(1, sizeof(*pCtrl))) == NULL)
		return ESP_ERR_NO_MEM;

	// Create the timer

	esp_timer_create_args_t timerCfg = {
		.callback        = timerCb,
		.arg             = pCtrl,
		.dispatch_method = ESP_TIMER_TASK,
		.name            = "timer-" MOD_NAME
	};

	esp_err_t	status;

	status = esp_timer_create(&timerCfg, &pCtrl->timer);
	if (ESP_OK != status) {
		return status;
	}

	timeCtrl = pCtrl;
	return ESP_OK;
}


/**
 * \brief Start the time manager
 *
 * \return ESP_OK Successful
 * \return (other) Failed
 *
 */
esp_err_t timeMgrStart(void)
{
	timeCtrl_t *	pCtrl = timeCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	esp_err_t	status;

	// Start the timer
	status = esp_timer_start_periodic(pCtrl->timer, 1000 * CS_TIMER_PERIOD_MS);
	if (ESP_OK != status) {
		return status;
	}

	return ESP_OK;
}


/*!
 * \brief Set the time zone adjustment
 *
 * \param [in] tzHours -23 to +23 hours
 *
 */
void timeMgrSetTimeZone(int tzHours)
{
	timeMgrSetTimeZoneSeconds((int32_t)tzHours * 3600);
}


/*!
 * \brief Set the time zone adjustment
 *
 * \param [in] offSeconds Number of seconds from UTC
 *
 */
void timeMgrSetTimeZoneSeconds(int32_t offSeconds)
{
	timeCtrl_t *	pCtrl = timeCtrl;
	if (NULL == pCtrl)
		return;

	struct timeval		now;

	now.tv_sec  = (time_t)pCtrl->utcTime;
	now.tv_usec = 0;

	portENTER_CRITICAL(&timeLock);

	pCtrl->localTimeAdjust = offSeconds;
	pCtrl->localTimeSet    = true;

	settimeofday(&now, NULL);

	portEXIT_CRITICAL(&timeLock);

	printLocalTime();
}


bool timeMgrTimeZoneIsSet(void)
{
	timeCtrl_t *	pCtrl = timeCtrl;
	if (NULL == pCtrl)
		return false;

	return pCtrl->localTimeSet;
}


/*!
 * \brief Set the internal UTC counter
 *
 * \param [in] uTime UTC time received from an external source
 *
 */
void timeMgrSetUtcTime(uint32_t uTime)
{

	timeCtrl_t *	pCtrl = timeCtrl;
	if (NULL == pCtrl)
		return;


	struct timeval	now;

	now.tv_sec  = (time_t)uTime;
	now.tv_usec = 0;

	portENTER_CRITICAL(&timeLock);

	pCtrl->lastUtcTimeSet = pCtrl->uptime;
	pCtrl->utcTime        = uTime;

	settimeofday(&now, NULL);

	portEXIT_CRITICAL(&timeLock);

	//gc_dbg("UTC time set to %lu", uTime);
	printLocalTime();
}


/*!
 * \brief Return current UTC time
 *
 * \return 32-bit count of seconds since the epoch starting
 * January 1, 1970
 *
 */
uint32_t timeMgrGetUtcTime(void)
{
	timeCtrl_t *	pCtrl = timeCtrl;
	if (NULL == pCtrl)
		return 0;

	uint32_t		retVal;

	portENTER_CRITICAL(&timeLock);
	retVal = pCtrl->utcTime;
	portEXIT_CRITICAL(&timeLock);

	return retVal;
}


/*!
 * \brief Return current local time
 *
 * \return
 *
 */
uint32_t timeMgrGetLocalTime(void)
{
	timeCtrl_t *	pCtrl = timeCtrl;
	if (NULL == pCtrl)
		return 0;

	uint32_t		retVal;
	int32_t			tzOffset = pCtrl->localTimeSet ? pCtrl->localTimeAdjust : 0;

	portENTER_CRITICAL(&timeLock);
	retVal = pCtrl->utcTime + tzOffset;
	portEXIT_CRITICAL(&timeLock);

	return retVal;
}


/**
 * \brief Check if UTC has been set since most recent boot
 *
 * \return true UTC is set
 * \return false UCT is not set
 *
 */
bool timeMgrUtcIsSet(void)
{
	timeCtrl_t *	pCtrl = timeCtrl;
	if (NULL == pCtrl)
		return false;

	return (pCtrl->lastUtcTimeSet > 0) ? true : false;
}


/*!
 * \brief Return elapsed time since last update of UTC
 *
 * \return >=0 Number of seconds since UTC was synchronized with
 * an external time source
 * \return <0 UTC has not been set since most recent boot
 *
 * This can be used to determine if UTC has been set, and how
 * much time has elapsed since it was most recently sync'd with
 * an external time source.
 *
 */
int32_t timeMgrGetUtcAge(void)
{
	timeCtrl_t *	pCtrl = timeCtrl;
	if (NULL == pCtrl)
		return 0;

	int32_t			retVal;

	portENTER_CRITICAL(&timeLock);
	if (pCtrl->lastUtcTimeSet == 0) {
		retVal = -1;
	} else {
		retVal = (int32_t)(pCtrl->uptime - pCtrl->lastUtcTimeSet);
	}
	portEXIT_CRITICAL(&timeLock);

	return retVal;
}


/*!
 * \brief Return number of seconds since boot
 *
 * \return 32-bit count of seconds since the device booted
 *
 */
uint32_t timeMgrGetUptime(void)
{
	timeCtrl_t *	pCtrl = timeCtrl;
	if (NULL == pCtrl)
		return 0;

	uint32_t		retVal;

	portENTER_CRITICAL(&timeLock);
	retVal = pCtrl->uptime;
	portEXIT_CRITICAL(&timeLock);

	return retVal;
}


/*!
 * \brief Return number of milliseconds since boot
 *
 * \return 64-bit count of milliseconds since the device booted
 *
 */
uint64_t timeMgrGetUptimeMs(void)
{
	timeCtrl_t *	pCtrl = timeCtrl;
	if (NULL == pCtrl)
		return 0;

	uint64_t		retVal;

	portENTER_CRITICAL(&timeLock);
	retVal = pCtrl->uptimeMs;
	portEXIT_CRITICAL(&timeLock);

	return retVal;
}


/*!
 * \brief Timer callback function
 *
 * \param [in] arg Not used
 *
 */
static void timerCb(void * arg)
{
	timeCtrl_t *	pCtrl = (timeCtrl_t *)arg;

	portENTER_CRITICAL(&timeLock);

	pCtrl->uptimeMs += CS_TIMER_PERIOD_MS;

	if ((pCtrl->msCount += CS_TIMER_PERIOD_MS) >= 1000) {
		pCtrl->msCount = 0;

		// Increment the seconds counters
		pCtrl->uptime  += 1;
		pCtrl->utcTime += 1;
	}
	portEXIT_CRITICAL(&timeLock);
}


#if (MOD_DEBUG)

static const char *	wday[7] = {
	"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};
static const char *	month[12] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};


void printLocalTime(void)
{
	timeCtrl_t *	pCtrl = timeCtrl;
	if (NULL == pCtrl)
		return;

	struct tm	t;
	time_t		curTime = (time_t)timeMgrGetLocalTime();

	gmtime_r(&curTime, &t);

	char	buf[80];
	int		len;

	len = snprintf(
		buf, sizeof(buf),
		"Local time %s %s %d %02d:%02d:%02d %d (GMT %d)",
		wday[t.tm_wday], month[t.tm_mon], t.tm_mday,
		t.tm_hour, t.tm_min, t.tm_sec,
		t.tm_year + 1900, pCtrl->localTimeAdjust / 3600
	);
	if (len < sizeof(buf)) {
		gc_dbg("%s", buf);
	} else {

	}
}

#else

void printLocalTime(void)
{
}

#endif
