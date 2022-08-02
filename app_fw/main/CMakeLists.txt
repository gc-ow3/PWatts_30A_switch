set(srcs
    "main.c"
)

idf_component_register(
	SRCS "${srcs}"
    REQUIRES cs_common cs_drivers cs_utils emtr
)
