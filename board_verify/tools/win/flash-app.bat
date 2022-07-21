@echo off

call .\tools\win\config.bat

set VERS=%1
set DIR_BIN=.\bin

if not defined VERS (
  echo "Specify version"
  exit /B 1
)

set BIN_APP=%DIR_BIN%\mcu\v%VERS%\%PROJ%.bin

if not exist %BIN_APP% (
  echo "Not found: %BIN_APP%"
  exit /B 1
)

esptool ^
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
