set(srcs
  "bin_hex.c"
  "cs_packer.c"
  "fw_file_check.c"
)

set(include_dirs "include")

idf_component_register(
  SRCS "${srcs}"
  INCLUDE_DIRS "${include_dirs}"
  PRIV_REQUIRES cs_common
)
