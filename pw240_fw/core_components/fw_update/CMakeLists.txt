set(srcs
    "cs_fw_update.c"
    "cs_local_ota.c"
    "cs_ota_rollback.c"
    "fw_file_check.c"
)

set(include_dirs "include")

idf_component_register(
	SRCS "${srcs}"
    INCLUDE_DIRS "${include_dirs}"
    REQUIRES main esp_https_ota json self-test
)
