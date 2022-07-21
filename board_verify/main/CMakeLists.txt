# See the build system documentation in IDF programming guide
# for more information about component CMakeLists.txt files.

set(srcs
  main.c
)

set(include_dirs "include")

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS  ${include_dirs}
    PRIV_INCLUDE_DIRS   # optional, add here private include directories
    REQUIRES cs_drivers
    PRIV_REQUIRES       # optional, list the private requirements
)
