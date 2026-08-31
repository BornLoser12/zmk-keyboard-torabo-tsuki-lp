# Keep the pinned ZMK version; apply only the Studio transport selection fix.
find_package(Git REQUIRED)
get_filename_component(zmk_root "${APPLICATION_SOURCE_DIR}" DIRECTORY)
get_filename_component(studio_usb_patch
  "${CMAKE_CURRENT_LIST_DIR}/../patches/studio-usb-independent.patch" ABSOLUTE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${studio_usb_patch}")

execute_process(
  COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${studio_usb_patch}"
  WORKING_DIRECTORY "${zmk_root}"
  RESULT_VARIABLE already_applied
  OUTPUT_QUIET ERROR_QUIET)
if(NOT already_applied EQUAL 0)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --check "${studio_usb_patch}"
    WORKING_DIRECTORY "${zmk_root}"
    RESULT_VARIABLE check_result
    ERROR_VARIABLE check_error)
  if(NOT check_result EQUAL 0)
    message(FATAL_ERROR "Studio USB patch does not match this ZMK source: ${check_error}")
  endif()
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply "${studio_usb_patch}"
    WORKING_DIRECTORY "${zmk_root}"
    RESULT_VARIABLE apply_result
    ERROR_VARIABLE apply_error)
  if(NOT apply_result EQUAL 0)
    message(FATAL_ERROR "Could not apply Studio USB patch: ${apply_error}")
  endif()
endif()
message(STATUS "Torabo Studio USB transport: independent of HID output")
