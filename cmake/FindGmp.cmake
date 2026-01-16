if (NOT GMP_FOUND)
  set(GMP_SEARCH_PATH "" CACHE PATH "Search path for gmp.")

  # Add common Homebrew paths to search
  set(GMP_SEARCH_PATHS ${GMP_SEARCH_PATH})
  if (APPLE)
    list(APPEND GMP_SEARCH_PATHS
      "/opt/homebrew/opt/gmp"  # ARM Mac
      "/usr/local/opt/gmp")     # Intel Mac
  endif()

  find_path(GMP_INCLUDE_DIR NAMES gmp.h PATHS ${GMP_SEARCH_PATHS} PATH_SUFFIXES include)
  find_library(GMP_LIB NAMES gmp PATHS ${GMP_SEARCH_PATHS} PATH_SUFFIXES lib)

  mark_as_advanced(GMP_SEARCH_PATH
    GMP_INCLUDE_DIR GMP_LIB)

  include (FindPackageHandleStandardArgs)
  find_package_handle_standard_args(Gmp
    REQUIRED_VARS GMP_INCLUDE_DIR GMP_LIB )
endif()
