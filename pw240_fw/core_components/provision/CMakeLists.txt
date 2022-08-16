set(srcs
    "cs_prov_support.c"
)

set(include_dirs "include")

idf_component_register(
	SRCS "${srcs}"
    INCLUDE_DIRS "${include_dirs}"
    REQUIRES main self-test
)
