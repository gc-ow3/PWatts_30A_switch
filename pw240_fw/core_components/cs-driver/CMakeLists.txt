set(srcs
    "cs_i2c_bus.c"
)

set(include_dirs "include")

idf_component_register(
	SRCS "${srcs}"
    INCLUDE_DIRS "${include_dirs}"
    REQUIRES main cs-lr-prov cs-utils app_update json mdns self-test
)
