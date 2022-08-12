set(srcs
    "main.c"
    "cmd_tester.c"
    "cmd_self_test.c"
    "cmd_leds.c"
    "cmd_inputs.c"
    "cmd_serial.c"
    "cmd_ble.c"
    "cmd_emtr.c"
)

set(include_dirs "include")

idf_component_register(
	SRCS "${srcs}"
    INCLUDE_DIRS "${include_dirs}"
    REQUIRES bt console fatfs cs_common cs_drivers cs_utils uart_test emtr
)
