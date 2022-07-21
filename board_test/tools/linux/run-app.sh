#!/bin/bash

. ./tools/linux/config.sh

VER=$1

if [ -z ${VER} ]; then
  APP_BIN=build/board_test.bin
else
  APP_BIN=bin/mcu/v${VER}/${PROJ}.bin
fi

esptool.py \
--chip esp32 \
--port ${TTYPORT} \
--baud 921600 \
--before default_reset \
--after hard_reset write_flash \
-z \
--flash_mode dio \
--flash_freq 40m \
--flash_size detect \
${ADDR_OTA} bin/ota_data_initial.bin \
${ADDR_APP} ${APP_BIN}

if [ $? == "0" ]; then
  minicom -D ${TTYPORT} --baud 115200
fi

