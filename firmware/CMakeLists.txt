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
        lib/al/core.h
        lib/al/led.h
        lib/accel.c
        lib/accel.h
        lib/buttons.c
        lib/buttons.h
        lib/buzzer.c
        lib/buzzer.h
        lib/core.c
        lib/led.c
        lib/led.h
        src/btn.c
        src/btn.h
        src/cap.c
        src/cap.h
        src/dat.c
        src/dat.h
        src/dev.h
        src/epd.c
        src/epd.h
        src/fnt.h
        src/fnt.c
        src/gfx.c
        src/gfx.h
        src/img.c
        src/img.h
        src/lvx.c
        src/lvx.h
        src/main.c
        src/pwr.c
        src/pwr.h
        src/rec.c
        src/rec.h
        src/rtc.c
        src/rtc.h
        src/scr.c
        src/scr.h
        src/sig.c
        src/sig.h
        src/sns.c
        src/sns.h
        src/sns_gas.c
        src/sns_gas.h
        src/stm.c
        src/stm.h
        src/sys.c
        src/sys.h
        test/dat.c
)

# create a fake library target
add_library(${CMAKE_PROJECT_NAME} ${SOURCE_FILES})

# include naos include paths
add_subdirectory(naos)
