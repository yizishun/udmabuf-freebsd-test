NCPU := $(shell sysctl -n hw.ncpu)

.PHONY: all patch configure build run clean

all: patch build_and_run

patch:
	@echo ">>> Applying patches..."
	@if [ -d "patches" ] && [ "$$(ls -A patches/*.patch 2>/dev/null)" ]; then \
		cd $(QEMU_SRC) && git am ../patches/*.patch; \
	else \
		echo "No patches found in patches/, skipping."; \
	fi

build:
	@./scripts/build.sh $(QEMU_SRC) $(DRM_KMOD)

run:
	@./scripts/run.sh $(QEMU_SRC)

clean:
	rm -rf $(BUILD_DIR)