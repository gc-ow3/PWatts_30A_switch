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
set TEMP=v%VERS%\temp.c

xxd -i -a -c 16 %INP% %TEMP%

echo // EMTR %VERS% > %OUTP%

exit /B 1

set /a count=0
for /f "tokens=*" %%G in (%OUTP) do (
)

if exist %OUTP% (echo firmware file written to %OUTP%)

::sed -i "1i // EMTR ${VERS}" ${OUTP}
::sed -i 's/^unsigned char .*$/const unsigned char emtrFwBin[] = {/' ${OUTP}
::sed -i 's/^unsigned int .*$/const unsigned int emtrFwBinLen = sizeof(emtrFwBin);/' ${OUTP}
