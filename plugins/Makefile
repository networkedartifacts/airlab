.PHONY: gfx hello gpio sen55 sprite http weather fmt

COMPILE := zig cc -target wasm32-wasi -Wl,-z,stack-size=65536 -Wl,--initial-memory=262144 -Wl,--max-memory=262144 -Os
UPLOAD ?= 0

define build_task
$1:
	$$(COMPILE) -o ./$1/main.wasm ./$1/main.c
	alpctl bundle ./$1 ./$1/$1.alp
	@if [ "$$(UPLOAD)" = "1" ]; then alpctl upload ./$1/$1.alp; fi
endef

$(foreach t,gfx hello gpio sen55 sprite http weather,$(eval $(call build_task,$t)))

all: gfx hello gpio sen55 sprite http weather

fmt:
	clang-format -i al.h **/*.c -style="{BasedOnStyle: Google, ColumnLimit: 120, SortIncludes: false}"
