set(srcs
	"uart_test.c"
)

set(include_dirs "include")

idf_component_register(
	SRCS "${srcs}"
    INCLUDE_DIRS "${include_dirs}"
    REQUIRES cs_drivers
)
