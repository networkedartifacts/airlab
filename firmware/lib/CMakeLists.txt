file(GLOB srcs ./*.c)
idf_component_register(SRCS ${srcs} INCLUDE_DIRS "." REQUIRES art32 naos driver esp_adc ulp esp_tinyusb littlefs)

set_source_files_properties(ulp.c PROPERTIES COMPILE_FLAGS -Wno-array-bounds)

set(ulp_app_name ulp_al)
set(ulp_sources "ulp/main.c" "sensor_hal.c")
set(ulp_exp_dep_srcs "ulp.c")

ulp_embed_binary(${ulp_app_name} "${ulp_sources}" "${ulp_exp_dep_srcs}")
