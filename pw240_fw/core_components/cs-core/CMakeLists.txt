set(srcs
    "cs_control.c"
    "cs_core_version.c"
    "cs_core.c"
    "cs_events.c"
    "cs_framework.c"
    "debug_utils.c"
    "event_callback.c"
    "mfg_data.c"
    "param_mgr.c"
    "time_mgr.c"
)

set(include_dirs "include")

idf_component_register(
	SRCS "${srcs}"
    INCLUDE_DIRS "${include_dirs}"
    REQUIRES main cs-lr-prov cs-utils provision app_update json mdns self-test
)
