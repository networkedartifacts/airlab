.PHONY: hello gpio sen55

COMPILE := zig cc -target wasm32-wasi -Wl,-z,stack-size=65536 -Wl,--initial-memory=262144 -Wl,--max-memory=262144 -Os

hello:
	$(COMPILE) -o ./hello/main.wasm ./hello/main.c
	alpctl bundle ./hello plugin.alp

gpio:
	$(COMPILE) -o ./gpio/main.wasm ./gpio/main.c
	alpctl bundle ./gpio plugin.alp

sen55:
	$(COMPILE) -o ./sen55/main.wasm ./sen55/main.c
	alpctl bundle ./sen55 plugin.alp

upload:
	alpctl upload plugin.alp

fmt:
	clang-format -i al.h **/*.c -style="{BasedOnStyle: Google, ColumnLimit: 120, SortIncludes: false}"
