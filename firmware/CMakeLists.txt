# minimal required cmake version
cmake_minimum_required(VERSION 3.7)

# you can set your own project name
project(airlab)

# this should not be changed
set(CMAKE_C_STANDARD 99)

# add your source files
set(SOURCE_FILES
        lib/al/accel.h
        lib/al/buttons.h
        lib/al/buzzer.h
        lib/al/clock.h
        lib/al/core.h
        lib/al/epd.h
        lib/al/led.h
        lib/al/power.h
        lib/al/sample.h
        lib/al/sensor.h
        lib/al/touch.h
        lib/ulp/main.c
        lib/accel.c
        lib/buttons.c
        lib/buzzer.c
        lib/clock.c
        lib/core.c
        lib/internal.h
        lib/epd.c
        lib/led.c
        lib/power.c
        lib/sample.c
        lib/sensor.c
        lib/sensor_gas.c
        lib/sensor_gas.h
        lib/sensor_hal.c
        lib/sensor_hal.h
        lib/touch.c
        lib/ulp.c
        src/dat.c
        src/dat.h
        src/dev.h
        src/fnt.h
        src/fnt.c
        src/gfx.c
        src/gfx.h
        src/gui.c
        src/gui.h
        src/hmi.c
        src/hmi.h
        src/img.c
        src/img.h
        src/lvx.c
        src/lvx.h
        src/main.c
        src/rec.c
        src/rec.h
        src/scr.c
        src/scr.h
        src/sig.c
        src/sig.h
        src/stm.c
        src/stm.h
        test/dat.c
)

# create a fake library target
add_library(${CMAKE_PROJECT_NAME} ${SOURCE_FILES})

# include naos include paths
add_subdirectory(naos)

# include ULP libraries
target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC
        naos/tree/esp-idf/components/ulp/ulp_riscv/ulp_core/include
        naos/tree/build/esp-idf/lib/ulp_al
)
