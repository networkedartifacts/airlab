.PHONY: basic big-type history-bars striped-columns simple-scale gauge-dials mini-sparklines dieters-ruler black-white

COMPILE := zig cc -mcpu=mvp -target wasm32-wasi -Wl,-z,stack-size=65536 -Wl,--initial-memory=262144 -Wl,--max-memory=262144 -Os
UPLOAD ?= 0
LAUNCH ?= 0

define build_task
$1:
	$$(COMPILE) -o ./$1/main.wasm ./$1/main.c
	airlab plugin bundle ./$1 ./$1/na.$1.alp
	@if [ "$$(UPLOAD)" = "1" ]; then airlab plugin upload ./$1/na.$1.alp; fi
	@if [ "$$(LAUNCH)" = "1" ]; then airlab plugin launch -b screen na.$1.alp; fi
endef

all: basic big-type history-bars striped-columns simple-scale gauge-dials mini-sparklines dieters-ruler black-white

$(foreach t,basic big-type history-bars striped-columns simple-scale gauge-dials mini-sparklines dieters-ruler black-white,$(eval $(call build_task,$t)))

upload:
	$(MAKE) UPLOAD=1 all
