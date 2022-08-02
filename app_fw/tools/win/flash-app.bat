@echo off

call .\tools\win\config.bat

set MODE=%1
set VERS=%2

if not defined MODE (
  echo "Specify mode: dev|prod"
  exit /B 1
) else if "%MODE%" == "dev" (
  echo "Flash development images"
  set DIR_BIN=.\bin\dev
) else if "%MODE%" == "prod" (
  echo "Flash production images"
  set DIR_BIN=.\bin\prod
) else (
  echo "Specify mode: dev|prod"
  exit /B 1
)

if not defined VERS (
  echo "Specify version"
  exit /B 1
)

set BIN_APP=%DIR_BIN%\mcu\v%VERS%\%PROJ%.bin

if not exist %BIN_APP% (
  echo "Not found: %BIN_APP%"
  exit /B 1
)

esptool.py ^
--chip esp32 ^
--port %TTYPORT% ^
--baud 921600 ^
--before default_reset ^
--after hard_reset ^
write_flash ^
--flash_mode dio ^
--flash_freq 40m ^
--flash_size detect ^
%ADDR_OTA% %DIR_BIN%\ota_data_initial.bin ^
%ADDR_APP% %BIN_APP%
