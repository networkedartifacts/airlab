# build a stub when the license-gated SDK is absent (see README.md), so that
# builds without it succeed with PM support disabled
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/bmv080/bmv080.h")
  message(WARNING "BMV080 SDK not found, building stub with PM support disabled")
  idf_component_register(SRCS "stub/bmv080.c" INCLUDE_DIRS "stub")
  return()
endif()

# header-only component exposing the prebuilt BMV080 SDK; the linker fragment
# moves the SDK's sizeable static state (~16 KB) to external RAM
idf_component_register(INCLUDE_DIRS "bmv080" LDFRAGMENTS "linker.lf")

# register the prebuilt static libraries
add_prebuilt_library(bmv080 "bmv080/lib_bmv080.a")
add_prebuilt_library(postProcessor "bmv080/lib_postProcessor.a")

# lib_bmv080.a references postProcessor_* symbols
target_link_libraries(bmv080 INTERFACE postProcessor)

# expose the libraries to anything that requires this component
target_link_libraries(${COMPONENT_LIB} INTERFACE bmv080)
