foreach(required_var IN ITEMS
    LOD3DS_OUTPUT_FILE LOD3DS_RE2C LOD3DS_LEMON LOD3DS_ZIPDIR
    LOD3DS_ARITHCHK LOD3DS_QNAN)
  if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
    message(FATAL_ERROR "Missing ${required_var}")
  endif()
endforeach()

configure_file(
  "${CMAKE_CURRENT_LIST_DIR}/ImportExecutables.cmake.in"
  "${LOD3DS_OUTPUT_FILE}"
  @ONLY
)
