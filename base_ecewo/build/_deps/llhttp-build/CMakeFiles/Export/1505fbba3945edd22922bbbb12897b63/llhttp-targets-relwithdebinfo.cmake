#----------------------------------------------------------------
# Generated CMake target import file for configuration "RELWITHDEBINFO".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "llhttp::llhttp_static" for configuration "RELWITHDEBINFO"
set_property(TARGET llhttp::llhttp_static APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(llhttp::llhttp_static PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELWITHDEBINFO "C"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/libllhttp.a"
  )

list(APPEND _cmake_import_check_targets llhttp::llhttp_static )
list(APPEND _cmake_import_check_files_for_llhttp::llhttp_static "${_IMPORT_PREFIX}/lib/libllhttp.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
