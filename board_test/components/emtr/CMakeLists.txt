set(srcs
  "app_emtr_cal.c"
  "app_emtr_drv.c"
  "cs_emtr_drv.c"
  "emtr_pwr_sig.c"
  "emtr_status.c"
)

set(include_dirs "include")

idf_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "${include_dirs}"
    REQUIRES cs_utils
)
