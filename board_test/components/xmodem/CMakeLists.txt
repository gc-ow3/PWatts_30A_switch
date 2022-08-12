set(srcs
  "xmodem_crc.c"
  "xmodem_snd.c"
)

set(include_dirs "include")

idf_component_register(
  SRCS "${srcs}"
  INCLUDE_DIRS "${include_dirs}"
  PRIV_REQUIRES cs_common
)
