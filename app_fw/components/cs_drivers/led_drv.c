/**
 * \file cs_iwo_led_drv.c
 * \brief low-level control of LEDs
*/

#include <esp_err.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include <driver/ledc.h>

#include "pinout.h"
#include "led_drv.h"

//static const char*	TAG = "led_drv";


/*
********************************************************************************
* There are 2 RGB LEDs in this product. This will use 6 channels of the ESP32
* LED driver.
********************************************************************************
*/

#define NUM_LEDS		(2)

typedef struct {
	ledId_t					id;
	const char *			name;
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
	ledInfo_t*	pInfo;
	ledMode_t	mode;
	colorPct_t	color;
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
#define LED_CHAN_SYS_RED		(LEDC_CHANNEL_0)
#define LED_CHAN_SYS_GRN		(LEDC_CHANNEL_1)
#define LED_CHAN_SYS_BLU		(LEDC_CHANNEL_2)
#define LED_CHAN_WIFI_RED		(LEDC_CHANNEL_3)
#define LED_CHAN_WIFI_GRN		(LEDC_CHANNEL_4)
#define LED_CHAN_WIFI_BLU		(LEDC_CHANNEL_5)

// Set all timers for 12-bit (0..4095) resolution
#define LED_TIMER_RESOLUTION	(LEDC_TIMER_12_BIT)
#define LED_FULL_DUTY			((1 << LED_TIMER_RESOLUTION) - 1)

#define PCT_DUTY(pct)	\
	((uint32_t)((float)LED_FULL_DUTY * (1.0 - ((float)pct / 100.0))))

#define NUM_LED_TIMERS			(2)

static const ledc_timer_config_t	ledTimerCfg[NUM_LED_TIMERS] = {
	// Timer 0 will be assigned to the three RGB Status LEDs
	{
		.timer_num       = LEDC_TIMER_0,
		.freq_hz         = 5000,
		.speed_mode      = LEDC_HIGH_SPEED_MODE,
		.duty_resolution = LED_TIMER_RESOLUTION
	},
	// Timer 1 will be assigned to the three Wi-Fi status LEDs
	{
		.timer_num       = LEDC_TIMER_1,
		.freq_hz         = 5000,
		.speed_mode      = LEDC_HIGH_SPEED_MODE,
		.duty_resolution = LED_TIMER_RESOLUTION
	},
};


static ledInfo_t	ledInfo[NUM_LEDS] = {
	// System status LED
	{
		.id         = ledId_system,
		.name       = "System",
		.isDimmable = false,
		.isRgb      = true,
		.ledScale   = 0.26,	// Avoid 0.25, 0.50, 0.75, 1.00
		.chanCfgRed = {
			.channel    = LED_CHAN_SYS_RED,
			.duty       = PCT_DUTY(0),
			.gpio_num   = LED1_R_GPIO,
			.hpoint     = 0,
			.intr_type  = LEDC_INTR_DISABLE,
			.speed_mode = LEDC_HIGH_SPEED_MODE,
			.timer_sel  = LEDC_TIMER_0
		},
		.chanCfgGrn = {
			.channel    = LED_CHAN_SYS_GRN,
			.duty       = PCT_DUTY(0),
			.gpio_num   = LED1_G_GPIO,
			.hpoint     = 0,
			.intr_type  = LEDC_INTR_DISABLE,
			.speed_mode = LEDC_HIGH_SPEED_MODE,
			.timer_sel  = LEDC_TIMER_0
		},
		.chanCfgBlu = {
			.channel    = LED_CHAN_SYS_BLU,
			.duty       = PCT_DUTY(0),
			.gpio_num   = LED1_B_GPIO,
			.hpoint     = 0,
			.intr_type  = LEDC_INTR_DISABLE,
			.speed_mode = LEDC_HIGH_SPEED_MODE,
			.timer_sel  = LEDC_TIMER_0
		}
	},
	// Wi-Fi status LED - RGB
	{
		.id         = ledId_ble,
		.name       = "BLE",
		.isDimmable = false,
		.isRgb      = true,
		.ledScale   = 0.26,	// Avoid 0.25, 0.50, 0.75, 1.00
		.chanCfgRed = {
			.channel    = LED_CHAN_WIFI_RED,
			.duty       = PCT_DUTY(0),
			.gpio_num   = LED2_R_GPIO,
			.hpoint     = 0,
			.intr_type  = LEDC_INTR_DISABLE,
			.speed_mode = LEDC_HIGH_SPEED_MODE,
			.timer_sel  = LEDC_TIMER_1
		},
		.chanCfgGrn = {
			.channel    = LED_CHAN_WIFI_GRN,
			.duty       = PCT_DUTY(0),
			.gpio_num   = LED2_G_GPIO,
			.hpoint     = 0,
			.intr_type  = LEDC_INTR_DISABLE,
			.speed_mode = LEDC_HIGH_SPEED_MODE,
			.timer_sel  = LEDC_TIMER_1
		},
		.chanCfgBlu = {
			.channel    = LED_CHAN_WIFI_BLU,
			.duty       = PCT_DUTY(0),
			.gpio_num   = LED2_B_GPIO,
			.hpoint     = 0,
			.intr_type  = LEDC_INTR_DISABLE,
			.speed_mode = LEDC_HIGH_SPEED_MODE,
			.timer_sel  = LEDC_TIMER_1
		}
	},
};


static void writeLedChannel(ledDrvCtrl_t * pCtrl, ledInfo_t * info, ledc_channel_t chan, uint8_t pct);
static int checkCall(ledDrvCtrl_t ** pCtrl, ledId_t ledId);

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
esp_err_t ledDrvInit(int pctBrightness)
{
	ledDrvCtrl_t *	pCtrl = pledCtrl;
	if (pCtrl) {
		return ESP_OK;
	}

	if ((pCtrl = calloc(1, sizeof(*pCtrl))) == NULL) {
		return ESP_ERR_NO_MEM;
	}

	if (pctBrightness < 0 || pctBrightness > 100) {
		return ESP_ERR_INVALID_ARG;
	}

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
		state->mode = ledMode_off;
		state->color.pctRed = 0;
		state->color.pctGrn = 0;
		state->color.pctBlu = 0;
	}

	pledCtrl = pCtrl;
	return ESP_OK;
}


/**
 * \brief Start the LED driver
 */
esp_err_t ledDrvStart(void)
{
	ledDrvCtrl_t *	pCtrl = pledCtrl;
	if (!pCtrl) {
		return ESP_FAIL;
	}
	if (pCtrl->isRunning) {
		return ESP_OK;
	}

	// ToDo ?

	pCtrl->isRunning = true;
	return ESP_OK;
}

/*!
 * \brief Set LED mode
 *
 * \param [in] ledId Select LED to switch
 * \param [in] mode Select mode
 *
 * \return ESP_OK LED turned on
 * \return ESP_FAIL Driver not initialized
 * \return ESP_ERR_INVALID_ARG invalid LED number
 *
 */
esp_err_t ledDrvSetMode(ledId_t ledId, ledMode_t mode)
{
	ledDrvCtrl_t *	pCtrl;
	esp_err_t		status;

	if ((status = checkCall(&pCtrl, ledId)) != ESP_OK) {
		return status;
	}

	int	ledIdx = ledId - 1;

	ledState_t*	state = &pCtrl->ledState[ledIdx];
	ledInfo_t*	info = state->pInfo;

	switch (mode)
	{
	case ledMode_off:
		state->color.pctRed = 0;
		state->color.pctGrn = 0;
		state->color.pctBlu = 0;
		break;
	case ledMode_red:
		state->color.pctRed = 100;
		state->color.pctGrn = 0;
		state->color.pctBlu = 0;
		break;
	case ledMode_grn:
		state->color.pctRed = 0;
		state->color.pctGrn = 100;
		state->color.pctBlu = 0;
		break;
	case ledMode_blu:
		state->color.pctRed = 0;
		state->color.pctGrn = 0;
		state->color.pctBlu = 100;
		break;
	default:
		return ESP_ERR_INVALID_ARG;
	}

	writeLedChannel(pCtrl, info, info->chanCfgRed.channel, state->color.pctRed);
	writeLedChannel(pCtrl, info, info->chanCfgGrn.channel, state->color.pctGrn);
	writeLedChannel(pCtrl, info, info->chanCfgBlu.channel, state->color.pctBlu);

	return ESP_OK;
}


/**
 * \brief Set all LEDs to the same percent brightness
 */
esp_err_t ledDrvSetBrightness(int value)
{
	ledDrvCtrl_t*	pCtrl;
	esp_err_t		status;

	if ((status = checkCall(&pCtrl, 1)) != ESP_OK) {
		return status;
	}

	if (value < 0 || value > 100) {
		return ESP_ERR_INVALID_ARG;
	}

	pCtrl->pctScale = ((float)value) / 100.0;

	// Apply change to active LEDs

	int			ledIdx;
	ledState_t*	state;

	for (ledIdx = 0, state = pCtrl->ledState; ledIdx < NUM_LEDS; ledIdx++, state++) {
		ledInfo_t *	info = state->pInfo;

		if (ledMode_off == state->mode) {
			continue;
		}

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

	ESP_LOGD(TAG,
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
static esp_err_t checkCall(ledDrvCtrl_t ** pCtrl, ledId_t ledId)
{
	*pCtrl = pledCtrl;

	if (!*pCtrl) {
		return ESP_FAIL;
	}

	if (!(*pCtrl)->isRunning) {
		return ESP_ERR_INVALID_STATE;
	}

	if (ledId < 1 || ledId > NUM_LEDS) {
		return ESP_ERR_INVALID_ARG;
	}

	return ESP_OK;
}
