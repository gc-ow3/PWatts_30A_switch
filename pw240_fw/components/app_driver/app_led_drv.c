/**
 * \file cs_iwo_led_drv.c
 * \brief low-level control of LEDs
*/

#include "cs_heap.h"
#include "app_led_drv.h"
#include <driver/gpio.h>
#include <driver/ledc.h>

// Comment out the MOD_NAME line to disable debug prints from this file
#define MOD_NAME	"led_drv"
#include "mod_debug.h"


/*
********************************************************************************
* There are 4 LEDs in this product. Three of them are mono-color (blue) and
* one has Red/Green/Blue selection. This will use 6 channels of the ESP32
* LED driver.
********************************************************************************
*/

typedef struct {
	csLedNum_t				ledNum;
	const char *			ledName;
	bool					isDimmable;
	bool					isRgb;
	float					ledScale;
	ledc_channel_config_t	chanCfgRed;
	ledc_channel_config_t	chanCfgGrn;
	ledc_channel_config_t	chanCfgBlu;
} const ledInfo_t;


typedef struct {
	uint8_t		pctRed;
	uint8_t		pctGrn;
	uint8_t		pctBlu;
} colorPct_t;


// State information for individual LEDs
typedef struct {
	ledInfo_t *		pInfo;
	bool			isOn;
	colorPct_t		color;
} ledState_t;


typedef struct {
	bool		isRunning;
	float		pctScale;
	ledState_t	ledState[NUM_LEDS];
} ledDrvCtrl_t;


/*
********************************************************************************
* Constants
********************************************************************************
*/
#define NUM_LED_CHANS			(6)
#define LED_CHAN_WIFI_RED		(LEDC_CHANNEL_0)
#define LED_CHAN_WIFI_GRN		(LEDC_CHANNEL_1)
#define LED_CHAN_WIFI_BLU		(LEDC_CHANNEL_2)
#define LED_CHAN_STATUS			(LEDC_CHANNEL_3)
#define LED_CHAN_OUTLET_1		(LEDC_CHANNEL_4)
#define LED_CHAN_OUTLET_2		(LEDC_CHANNEL_5)

// Set all timers for 12-bit (0..4095) resolution
#define LED_TIMER_RESOLUTION	(LEDC_TIMER_12_BIT)
#define LED_FULL_DUTY			((1 << LED_TIMER_RESOLUTION) - 1)

#define PCT_DUTY(pct)	\
	((uint32_t)((float)LED_FULL_DUTY * (1.0 - ((float)pct / 100.0))))

#define NUM_LED_TIMERS			(3)

static const ledc_timer_config_t	ledTimerCfg[NUM_LED_TIMERS] = {
	// Timer 0 will be assigned to the three RGB Status LEDs
	{
		.timer_num       = LEDC_TIMER_0,
		.freq_hz         = 5000,
		.speed_mode      = LEDC_HIGH_SPEED_MODE,
		.duty_resolution = LED_TIMER_RESOLUTION
	},
	// Timer 1 will be assigned to the provisioning LED
	{
		.timer_num       = LEDC_TIMER_1,
		.freq_hz         = 5000,
		.speed_mode      = LEDC_HIGH_SPEED_MODE,
		.duty_resolution = LED_TIMER_RESOLUTION
	},
	// Timer 2 will be assigned to the two outlet LEDs
	{
		.timer_num       = LEDC_TIMER_2,
		.freq_hz         = 5000,
		.speed_mode      = LEDC_HIGH_SPEED_MODE,
		.duty_resolution = LED_TIMER_RESOLUTION
	}
};


static ledInfo_t	ledInfo[NUM_LEDS] = {
	// Status LED - RGB
	{
		.ledNum     = csLedNum_wifi,
		.ledName    = "Wi-Fi status",
		.isDimmable = false,
		.isRgb      = true,
		.ledScale   = 0.26,	// Avoid 0.25, 0.50, 0.75, 1.00
		.chanCfgRed = {
			.channel    = LED_CHAN_WIFI_RED,
			.duty       = PCT_DUTY(0),
			.gpio_num   = GPIO_NUM_25,
			.hpoint     = 0,
			.intr_type  = LEDC_INTR_DISABLE,
			.speed_mode = LEDC_HIGH_SPEED_MODE,
			.timer_sel  = LEDC_TIMER_0
		},
		.chanCfgGrn = {
			.channel    = LED_CHAN_WIFI_GRN,
			.duty       = PCT_DUTY(0),
			.gpio_num   = GPIO_NUM_33,
			.hpoint     = 0,
			.intr_type  = LEDC_INTR_DISABLE,
			.speed_mode = LEDC_HIGH_SPEED_MODE,
			.timer_sel  = LEDC_TIMER_0
		},
		.chanCfgBlu = {
			.channel    = LED_CHAN_WIFI_BLU,
			.duty       = PCT_DUTY(0),
			.gpio_num   = GPIO_NUM_32,
			.hpoint     = 0,
			.intr_type  = LEDC_INTR_DISABLE,
			.speed_mode = LEDC_HIGH_SPEED_MODE,
			.timer_sel  = LEDC_TIMER_0
		}
	},
	// Touch status (Blue)
	{
		.ledNum     = csLedNum_status,
		.ledName    = "System status",
		.isDimmable = false,
		.isRgb      = false,
		.ledScale   = 0.40,	// Avoid 0.25, 0.50, 0.75, 1.00
		.chanCfgBlu = {
			.channel    = LED_CHAN_STATUS,
			.duty       = PCT_DUTY(0),
			.gpio_num   = GPIO_NUM_21,
			.hpoint     = 0,
			.intr_type  = LEDC_INTR_DISABLE,
			.speed_mode = LEDC_HIGH_SPEED_MODE,
			.timer_sel  = LEDC_TIMER_1
		},
	},
	{
		.ledNum     = csLedNum_socket1,
		.ledName    = "Outlet 1",
		.isDimmable = true,
		.isRgb      = false,
		.ledScale   = 0.26,	// Avoid 0.25, 0.50, 0.75, 1.00
		.chanCfgBlu = {
			.channel    = LED_CHAN_OUTLET_1,
			.duty       = PCT_DUTY(0),
			.gpio_num   = GPIO_NUM_22,
			.hpoint     = 0,
			.intr_type  = LEDC_INTR_DISABLE,
			.speed_mode = LEDC_HIGH_SPEED_MODE,
			.timer_sel  = LEDC_TIMER_2
		},
	},
	{
		.ledNum     = csLedNum_socket1,
		.ledName    = "Outlet 2",
		.isDimmable = true,
		.isRgb      = false,
		.ledScale   = 0.26,	// Avoid 0.25, 0.50, 0.75, 1.00
		.chanCfgBlu = {
			.channel    = LED_CHAN_OUTLET_2,
			.duty       = PCT_DUTY(0),
			.gpio_num   = GPIO_NUM_13,
			.hpoint     = 0,
			.intr_type  = LEDC_INTR_DISABLE,
			.speed_mode = LEDC_HIGH_SPEED_MODE,
			.timer_sel  = LEDC_TIMER_2
		},
	},
};


static void writeLedChannel(ledDrvCtrl_t * pCtrl, ledInfo_t * info, ledc_channel_t chan, uint8_t pct);
static void intLedOff(ledDrvCtrl_t * pCtrl, ledState_t * state);
static int checkCall(ledDrvCtrl_t * pCtrl, csLedNum_t ledNum);

static ledDrvCtrl_t *	pledCtrl;


/*!
 * \brief Initialize the LED driver
 *
 * This must be called before other gcLedDrvXXXX functions. It sets up the
 * GPIO pins that control the LEDs and initializes the control structure
 * for each LED.
 *
 * \return WM_SUCCESS LED driver started
 * \return (other) Failure
 *
 */
esp_err_t appLedDrvInit(int pctBrightness)
{
	ledDrvCtrl_t *	pCtrl = pledCtrl;
	if (NULL != pCtrl)
		return ESP_OK;

	if ((pCtrl = cs_heap_calloc(1, sizeof(*pCtrl))) == NULL)
		return ESP_ERR_NO_MEM;

	if (pctBrightness < 0 || pctBrightness > 100)
		return ESP_ERR_INVALID_ARG;

	pCtrl->pctScale = (float)pctBrightness / 100.0;

	ledInfo_t *		pInfo;
	int				ledIdx;

	for (ledIdx = 0, pInfo = ledInfo; ledIdx < NUM_LEDS; ledIdx++, pInfo++) {
		// Attach the info structure for each LED
		pCtrl->ledState[ledIdx].pInfo = pInfo;
	}

	// Configure the LED timers
	for (ledIdx = 0; ledIdx < NUM_LED_TIMERS; ledIdx++) {
		ledc_timer_config(&ledTimerCfg[ledIdx]);
	}

	// Configure the LED channels
	for (ledIdx = 0, pInfo = ledInfo; ledIdx < NUM_LEDS; ledIdx++, pInfo++) {
		if (pInfo->isRgb) {
			// RGB LED
			ledc_channel_config(&pInfo->chanCfgRed);
			ledc_channel_config(&pInfo->chanCfgGrn);
			ledc_channel_config(&pInfo->chanCfgBlu);
		} else {
			// Mono color (blue) LED
			ledc_channel_config(&pInfo->chanCfgBlu);
		}
	}

	// Install the fade control service
	ledc_fade_func_install(0);

	ledState_t *	state = pCtrl->ledState;
	for (ledIdx = 0, state = pCtrl->ledState; ledIdx < NUM_LEDS; ledIdx++, state++) {
		if (state->pInfo->isRgb) {
			// Set default RGB LED color to blue
			state->color.pctRed = 0;
			state->color.pctGrn = 0;
			state->color.pctBlu = 100;
		} else {
			state->color.pctBlu = 100;
		}

		intLedOff(pCtrl, state);
	}

	pledCtrl = pCtrl;
	return ESP_OK;
}


/**
 * \brief Start the LED driver
 */
esp_err_t appLedDrvStart(void)
{
	ledDrvCtrl_t *	pCtrl = pledCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;
	if (pCtrl->isRunning)
		return ESP_OK;

	pCtrl->isRunning = true;
	return ESP_OK;
}

/*!
 * \brief Turn on a LED
 *
 * \param [in] ledNum Select LED to turn on
 *
 * The LED will be turned on
 *
 * \return ESP_OK LED turned on
 * \return ESP_FAIL Driver not initialized
 * \return ESP_ERR_INVALID_ARG invalid LED number
 *
 */
esp_err_t appLedDrvTurnOn(csLedNum_t ledNum)
{
	ledDrvCtrl_t *	pCtrl = pledCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	int	status;
	if ((status = checkCall(pCtrl, ledNum)) != ESP_OK) {
		return status;
	}

	int	ledIdx = ledNum - 1;

	ledState_t *	state = &pCtrl->ledState[ledIdx];
	ledInfo_t *		info  = state->pInfo;

	state->isOn = true;

	if (info->isRgb) {
		writeLedChannel(pCtrl, info, info->chanCfgRed.channel, state->color.pctRed);
		writeLedChannel(pCtrl, info, info->chanCfgGrn.channel, state->color.pctGrn);
		writeLedChannel(pCtrl, info, info->chanCfgBlu.channel, state->color.pctBlu);
	} else {
		writeLedChannel(pCtrl, info, info->chanCfgBlu.channel, state->color.pctBlu);
	}

	return ESP_OK;
}


/*!
 * \brief Turn off a LED
 *
 * \param [in] led Select LED to turn off
 *
 * The LED will be turned off
 *
 * \return ESP_OK LED turned off
 * \return ESP_FAIL Driver not initialized
 * \return ESP_ERR_INVALID_ARG invalid LED number
 *
 */
esp_err_t appLedDrvTurnOff(csLedNum_t ledNum)
{
	ledDrvCtrl_t *	pCtrl = pledCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	int	status;
	if ((status = checkCall(pCtrl, ledNum)) != ESP_OK) {
		return status;
	}

	int				ledIdx = ledNum - 1;
	ledState_t *	state = &pCtrl->ledState[ledIdx];

	intLedOff(pCtrl, state);

	return ESP_OK;
}


/**
 * \brief Set all LEDs to the same percent brightness
 */
esp_err_t appLedDrvSetBrightness(int value)
{
	ledDrvCtrl_t *	pCtrl = pledCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	esp_err_t		status;
	if ((status = checkCall(pCtrl, csLedNum_status)) != ESP_OK)
		return status;

	if (value < 0 || value > 100)
		return ESP_ERR_INVALID_ARG;

	pCtrl->pctScale = ((float)value) / 100.0;

	// Apply change to active LEDs

	int				ledIdx;
	ledState_t *	state;

	for (ledIdx = 0, state = pCtrl->ledState; ledIdx < NUM_LEDS; ledIdx++, state++) {
		ledInfo_t *	info = state->pInfo;

		if (!state->isOn)
			continue;

		if (state->pInfo->isRgb) {
			writeLedChannel(pCtrl, info, info->chanCfgRed.channel, state->color.pctRed);
			writeLedChannel(pCtrl, info, info->chanCfgGrn.channel, state->color.pctGrn);
			writeLedChannel(pCtrl, info, info->chanCfgBlu.channel, state->color.pctBlu);
		} else {
			writeLedChannel(pCtrl, info, info->chanCfgBlu.channel, state->color.pctBlu);
		}
	}

	return ESP_OK;
}


bool appLedDrvLedIsRGB(csLedNum_t ledNum)
{
	if (ledNum < 1 || ledNum > NUM_LEDS)
		return false;

	return ledInfo[ledNum - 1].isRgb;
}


/**
 * \brief Set the color for the selected LED
 */
esp_err_t appLedDrvSetColor(csLedNum_t ledNum, csLedColor_t color)
{
	ledDrvCtrl_t *	pCtrl = pledCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	esp_err_t		status;
	if ((status = checkCall(pCtrl, ledNum)) != ESP_OK) {
		return status;
	}

	int	ledIdx = ledNum - 1;

	ledState_t *	state = &pCtrl->ledState[ledIdx];
	ledInfo_t *		info  = state->pInfo;

	if (!info->isRgb)
		return ESP_ERR_INVALID_ARG;

	switch (color)
	{
	case csLedColor_red:
		state->color.pctRed = 100;
		state->color.pctGrn = 0;
		state->color.pctBlu = 0;
		break;

	case csLedColor_green:
		state->color.pctRed = 0;
		state->color.pctGrn = 100;
		state->color.pctBlu = 0;
		break;

	case csLedColor_blue:
		state->color.pctRed = 0;
		state->color.pctGrn = 0;
		state->color.pctBlu = 100;
		break;

	case csLedColor_yellow:
		state->color.pctRed = 100;
		state->color.pctGrn = 80;
		state->color.pctBlu = 0;
		break;

	default:
		// Not a valid color option
		return ESP_ERR_INVALID_ARG;
	}

	if (state->isOn) {
		// Apply the RGB duty cycles
		writeLedChannel(pCtrl, info, info->chanCfgRed.channel, state->color.pctRed);
		writeLedChannel(pCtrl, info, info->chanCfgGrn.channel, state->color.pctGrn);
		writeLedChannel(pCtrl, info, info->chanCfgBlu.channel, state->color.pctBlu);
	}

	return ESP_OK;
}


/*!
 * \brief (Internal) Turn off a LED
 *
 * \param [in] led Select LED to turn off
 *
 * The LED will be turned off
 *
 * \return WM_SUCCESS LED turned on
 * \return -WM_E_INVAL invalid LED number
 *
 */
static void intLedOff(ledDrvCtrl_t * pCtrl, ledState_t * state)
{
	ledInfo_t *	info = state->pInfo;

	state->isOn = false;

	if (info->isRgb) {
		writeLedChannel(pCtrl, info, info->chanCfgRed.channel, 0);
		writeLedChannel(pCtrl, info, info->chanCfgGrn.channel, 0);
		writeLedChannel(pCtrl, info, info->chanCfgBlu.channel, 0);
	} else {
		writeLedChannel(pCtrl, info, info->chanCfgBlu.channel, 0);
	}
}


static void writeLedChannel(ledDrvCtrl_t * pCtrl, ledInfo_t * info, ledc_channel_t chan, uint8_t pct)
{
	float	scaledPct;

	if (info->isDimmable) {
		scaledPct = ((float)pct * info->ledScale) * (float)pCtrl->pctScale;
	} else {
		scaledPct = ((float)pct * info->ledScale);
	}

	uint32_t	duty = PCT_DUTY(scaledPct);

#if 0
	const char *	color;

	switch(chan)
	{
	case LED_CHAN_WIFI_RED:
		color = "RED";
		break;
	case LED_CHAN_WIFI_GRN:
		color = "GRN";
		break;
	case LED_CHAN_WIFI_BLU:
		color = "BLU";
		break;
	default:
		color = "BLU";
		break;
	}

	int		iComp = (int)scaledPct;
	int		fComp = ((int)(scaledPct * 100.0)) - (iComp * 100);

	gc_dbg(
		"LED \"%s\" (%s) scaledPct: %d.%d, duty: 0x%03x",
		info->ledName,
		color,
		iComp, fComp,
		duty
	);
#endif

	ledc_set_duty_and_update(LEDC_HIGH_SPEED_MODE, chan, duty, 0);
}


/**
 * \brief Common API checking
 */
static esp_err_t checkCall(ledDrvCtrl_t * pCtrl, csLedNum_t ledNum)
{
	if (!pCtrl->isRunning)
		return ESP_FAIL;

	if (ledNum < 1 || ledNum > NUM_LEDS)
		return ESP_ERR_INVALID_ARG;

	return ESP_OK;
}
