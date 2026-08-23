file(GLOB srcs ./*.c)
list(APPEND srcs "ulp/transfer.c")
idf_component_register(SRCS ${srcs} INCLUDE_DIRS "." REQUIRES naos driver esp_adc ulp tinyusb usb littlefs fatfs sdk lvgl)

idf_component_get_property(tusb_lib espressif__tinyusb COMPONENT_LIB)
target_include_directories(${tusb_lib} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

# allocate the LVGL heap from PSRAM (pairs with CONFIG_LV_MEM_CUSTOM)
idf_component_get_property(lvgl_lib lvgl COMPONENT_LIB)
target_compile_definitions(${lvgl_lib} PRIVATE
  "LV_MEM_CUSTOM_ALLOC(s)=heap_caps_malloc(s,MALLOC_CAP_SPIRAM)"
  "LV_MEM_CUSTOM_REALLOC(p,s)=heap_caps_realloc(p,s,MALLOC_CAP_SPIRAM)"
  "LV_MEM_CUSTOM_FREE(p)=heap_caps_free(p)")

set_source_files_properties(ulp.c PROPERTIES COMPILE_FLAGS -Wno-array-bounds)

set(ulp_app_name ulp_al)
set(ulp_sources "ulp/main.c" "ulp/transfer.c" "sensor_hal.c")
set(ulp_exp_dep_srcs "ulp.c")

ulp_embed_binary(${ulp_app_name} "${ulp_sources}" "${ulp_exp_dep_srcs}")
