set(srcs
    "app_pw_api.c"
)

set(include_dirs "include")

idf_component_register(
	SRCS "${srcs}"
    INCLUDE_DIRS "${include_dirs}"
    EMBED_TXTFILES certs/server_root_cert.pem
    REQUIRES main app_driver
)
