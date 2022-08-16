set(srcs
    "cs_lr_prov.c"
)

set(include_dirs "include")

idf_component_register(
	SRCS "${srcs}"
    INCLUDE_DIRS "${include_dirs}"
    REQUIRES main json esp_http_server bootloader_support mdns self-test
)
