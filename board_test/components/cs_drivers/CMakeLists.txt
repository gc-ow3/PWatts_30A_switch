set(srcs
  "led_drv.c"
  "inp_drv.c"
)

set(include_dirs "include")

idf_component_register(
  SRCS "${srcs}"
  INCLUDE_DIRS "${include_dirs}"
  REQUIRES cs_common
)
