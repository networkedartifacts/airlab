.PHONY: adc data gfx gpio hello http i2c info sen55 servo sprite weather

COMPILE := zig cc -target wasm32-wasi -Wl,-z,stack-size=65536 -Wl,--initial-memory=262144 -Wl,--max-memory=262144 -Os
UPLOAD ?= 0
LAUNCH ?= 0

define build_task
$1:
	$$(COMPILE) -o ./$1/main.wasm ./$1/main.c
	airlab plugin bundle ./$1 ./$1/$1.alp
	@if [ "$$(UPLOAD)" = "1" ]; then airlab plugin upload ./$1/$1.alp; fi
	@if [ "$$(LAUNCH)" = "1" ]; then airlab plugin launch $1.alp; fi
endef

$(foreach t,adc data gfx gpio hello http i2c info sen55 servo sprite weather,$(eval $(call build_task,$t)))

all: adc data gfx gpio hello http i2c info sen55 servo sprite weather

upload:
	$(MAKE) UPLOAD=1 all

fmt:
	clang-format -i al.h **/*.c -style="{BasedOnStyle: Google, ColumnLimit: 120, SortIncludes: false}"
