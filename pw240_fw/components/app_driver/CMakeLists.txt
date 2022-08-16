set(srcs
    "app_led_drv.c"
    "cap1298_drv.c"
    "emtr_drv.c"
    "emtr_fw.c")

set(include_dirs "include")

idf_component_register(
	SRCS "${srcs}"
    INCLUDE_DIRS "${include_dirs}"
    REQUIRES main cs-core cs-driver xmodem self_test
)
