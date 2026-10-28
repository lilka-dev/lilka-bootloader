PORT ?= /dev/cu.usbmodem11201
KEIRA_DIR ?= ../0014_keira
KEIRA_BIN ?= $(KEIRA_DIR)/.pio/build/v2/firmware.bin
KEIRA_DATA ?= $(KEIRA_DIR)/data

IDF_IMAGE := espressif/idf:v4.4.7
DOCKER := docker run --rm -v "$(CURDIR)":/project -e HOME=/tmp $(IDF_IMAGE)
ESPTOOL := pio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32s3 --port $(PORT) -b 921600

BL_BUILD := bootloader/build-docker
RG_APPS := launcher retro-core prboom-go snes9x gwenesis fmsx gbsp
IMAGE := build/lilka_boot.img
RG_IMAGE := build/retro-go_lilka.img

help: ## Show this help
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | awk 'BEGIN {FS = ":.*?## "}; {printf "\033[36m%-16s\033[0m %s\n", $$1, $$2}'

bootloader: ## Build the boot menu, partition table and blank otadata
	./build.sh bootloader partition-table
	$(DOCKER) bash -c '. /opt/esp/idf/export.sh >/dev/null && \
		python /opt/esp/idf/components/partition_table/gen_empty_partition.py 0x2000 /project/$(BL_BUILD)/ota_data_blank.bin'

retro-go-patch: ## Apply Keira integration patch to the retro-go submodule
	cd retro-go && (git apply --check --reverse ../patches/retro-go-lilka-keira.patch 2>/dev/null || git apply ../patches/retro-go-lilka-keira.patch)

retro-go: retro-go-patch ## Build build/retro-go_lilka.img, installed from Keira (file manager or catalog)
	$(DOCKER) bash -c '. /opt/esp/idf/export.sh >/dev/null && git config --global --add safe.directory "*" && \
		cd /project/retro-go && python rg_tool.py --target lilka build-img $(RG_APPS)'
	mkdir -p build && cp "$$(ls -t retro-go/retro-go_*_lilka.img | head -1)" $(RG_IMAGE)

spiffs: ## Build Keira's SPIFFS image sized for the shared layout
	mkdir -p build && rm -rf build/spiffs-data && cp -R "$(KEIRA_DATA)" build/spiffs-data
	$(DOCKER) bash -c '. /opt/esp/idf/export.sh >/dev/null && python /opt/esp/idf/components/spiffs/spiffsgen.py \
		$$(python -c "import csv;print([r for r in csv.reader(open(\"/project/partitions.csv\")) if r and r[0].strip()==\"spiffs\"][0][4].strip())") \
		/project/build/spiffs-data /project/build/spiffs.bin'

image: ## Compose build/lilka_boot.img (bootloader + partition table + Keira + SPIFFS)
	mkdir -p build
	python3 -I tools/mkimage.py --partitions partitions.csv \
		--bootloader $(BL_BUILD)/bootloader/bootloader.bin \
		--partition-table $(BL_BUILD)/partition_table/partition-table.bin \
		--otadata $(BL_BUILD)/ota_data_blank.bin \
		-o $(IMAGE) \
		app0=$(KEIRA_BIN) spiffs=build/spiffs.bin

all: bootloader retro-go spiffs image ## Build everything

flash: ## Erase the board and write build/lilka_boot.img
	$(ESPTOOL) write_flash --erase-all --flash_mode keep --flash_freq keep --flash_size keep 0x0 $(IMAGE)

flash-bootloader: ## Write only the boot menu
	$(ESPTOOL) write_flash --flash_mode keep --flash_freq keep --flash_size keep 0x0 $(BL_BUILD)/bootloader/bootloader.bin

monitor: ## Reset the board and show its console for 15 s
	$$(pio system info --json-output | python3 -c "import json,sys;print(json.load(sys.stdin)['python_exe']['value'])") \
		tools/serial_log.py $(PORT) 15

.PHONY: help bootloader retro-go-patch retro-go spiffs image all flash flash-bootloader monitor
