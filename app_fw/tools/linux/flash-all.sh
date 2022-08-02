#!/bin/bash

. ./tools/linux/config.sh

VERS=$1

if [ -z ${VERS} ]
then
  echo "Specify version"
  exit 1
fi

esptool.py \
--chip esp32 \
--port ${TTYPORT} \
--baud 921600 \
--before default_reset \
--after hard_reset \
write_flash \
--flash_mode dio \
--flash_freq 40m \
--flash_size detect \
${ADDR_BOOT} ${BIN_ROOT}/bootloader.bin \
${ADDR_PART} ${BIN_ROOT}/partitions.bin \
${ADDR_OTA} ${BIN_ROOT}/ota_data_initial.bin \
${ADDR_APP} ${BIN_ROOT}/mcu/v${VERS}/${PROJ}.bin

