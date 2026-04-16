find_path(VECTORSCAN_INCLUDE_DIR hs/hs.h)
find_library(VECTORSCAN_LIBRARY NAMES hs)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Vectorscan
    REQUIRED_VARS VECTORSCAN_LIBRARY VECTORSCAN_INCLUDE_DIR)

if(Vectorscan_FOUND AND NOT TARGET Vectorscan::hs)
    add_library(Vectorscan::hs UNKNOWN IMPORTED)
    set_target_properties(Vectorscan::hs PROPERTIES
        IMPORTED_LOCATION "${VECTORSCAN_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${VECTORSCAN_INCLUDE_DIR}")
endif()
