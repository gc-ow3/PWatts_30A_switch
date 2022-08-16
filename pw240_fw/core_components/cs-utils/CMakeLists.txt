set(srcs
    "bin_hex.c"
    "cs_der_to_pem.c"
    "cs_heap.c"
    "cs_json_utils.c"
    "cs_packer.c"
    "cs_rpc_proc.c"
    "cs_str_utils.c"
    "cs_wifi_utils.c"
)

set(include_dirs "include")

idf_component_register(
	SRCS "${srcs}"
    INCLUDE_DIRS "${include_dirs}"
    REQUIRES main cs-core json self-test
)
