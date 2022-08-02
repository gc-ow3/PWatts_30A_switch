@echo off

call .\tools\win\config.bat

set DIR_BIN=.\build

set BIN_APP=%DIR_BIN%\%PROJ%.bin

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

if %ERRORLEVEL% EQU 0 (
  echo "Launch terminal"
  ttermpro /F=TT_Board_Test.INI /C=%COMPORT%
) else (
  echo "Flash failed"
  exit /B 1
)
