@echo off

if "%~1" == "" (
  echo Must specify version
  exit /B 1
)

set VERS=%1

:: version string with '_' instead of '.' as separators
set VERSX=%VERS:.=_%

set INP=v%VERS%\pw_cable-ota-hdr-%VERSX%.bin

if not exist %INP% (
  echo File %INP% not found
  exit /B 1
)

set OUTP=v%VERS%\emtr_fw.c

python gen_c_file.py emtrFwBin %INP% %OUTP%
