LVGL := naos/tree/components/lvgl
SDKCONFIG := naos/tree/sdkconfig
UNITY := test/unity
UNITY_VERSION := v2.6.0

fmt:
	naos format
	clang-format -i ./lib/*.c ./lib/*.h ./lib/al/*.h ./lib/ulp/*.c -style="{BasedOnStyle: Google, ColumnLimit: 120, SortIncludes: false}"

test: $(UNITY)/unity.c
	mkdir -p test/build
	grep -E '^CONFIG_LV_(TXT|COLOR)' $(SDKCONFIG) | sed -e 's/^CONFIG_/#define DEVICE_/' -e 's/=y$$/ 1/' -e 's/=/ /' > test/build/device_conf.h
	cc -std=c11 -Wall -o test/build/tests \
		-DLV_CONF_INCLUDE_SIMPLE \
		-I test -I test/build -I $(UNITY) -I src -I $(LVGL) \
		test/*.c $(UNITY)/unity.c src/fnt.c \
		$(LVGL)/src/misc/lv_txt.c $(LVGL)/src/misc/lv_mem.c $(LVGL)/src/misc/lv_log.c \
		$(LVGL)/src/misc/lv_utils.c $(LVGL)/src/misc/lv_gc.c \
		$(LVGL)/src/font/lv_font.c $(LVGL)/src/font/lv_font_fmt_txt.c
	test/build/tests

$(UNITY)/unity.c:
	mkdir -p $(UNITY)
	curl -fsSL -o $(UNITY)/unity.c https://raw.githubusercontent.com/ThrowTheSwitch/Unity/$(UNITY_VERSION)/src/unity.c
	curl -fsSL -o $(UNITY)/unity.h https://raw.githubusercontent.com/ThrowTheSwitch/Unity/$(UNITY_VERSION)/src/unity.h
	curl -fsSL -o $(UNITY)/unity_internals.h https://raw.githubusercontent.com/ThrowTheSwitch/Unity/$(UNITY_VERSION)/src/unity_internals.h

.PHONY: fmt test
