set(srcs
    "cap1298_handler.c"
    "led_mgr.c"
    "outlet_mgr.c"
)

set(include_dirs "include")

idf_component_register(
	SRCS "${srcs}"
    INCLUDE_DIRS "${include_dirs}"
    REQUIRES main self_test
)
