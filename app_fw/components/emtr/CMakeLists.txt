set(srcs
  "app_emtr_cal.c"
  "app_emtr_drv.c"
  "cs_emtr_drv.c"
  "emtr_fw.c"
  "emtr_pwr_sig.c"
)

set(include_dirs "include")

idf_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "${include_dirs}"
    REQUIRES cs_common cs_drivers cs_utils json xmodem
)
