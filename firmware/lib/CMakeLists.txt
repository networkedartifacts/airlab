file(GLOB srcs ./*.c)
list(APPEND srcs "ulp/transfer.c")
idf_component_register(SRCS ${srcs} INCLUDE_DIRS "." REQUIRES naos driver esp_adc ulp tinyusb usb littlefs fatfs bmv080)

idf_component_get_property(tusb_lib espressif__tinyusb COMPONENT_LIB)
target_include_directories(${tusb_lib} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

set_source_files_properties(ulp.c PROPERTIES COMPILE_FLAGS -Wno-array-bounds)

set(ulp_app_name ulp_al)
set(ulp_sources "ulp/main.c" "ulp/transfer.c" "sensor_hal.c")
set(ulp_exp_dep_srcs "ulp.c")

ulp_embed_binary(${ulp_app_name} "${ulp_sources}" "${ulp_exp_dep_srcs}")
