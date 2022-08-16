set(srcs
    "app_self_test.c"
    "utest_cap1298.c"
)

set(include_dirs "include")

idf_component_register(
	SRCS "${srcs}"
    INCLUDE_DIRS "${include_dirs}"
    REQUIRES main
)
