set(srcs
    "app_control.c"
    "app_params.c"
    "app_version.c"
    "main.c")

set(include_dirs "include")

idf_component_register(
	SRCS "${srcs}"
    INCLUDE_DIRS "${include_dirs}"
)
