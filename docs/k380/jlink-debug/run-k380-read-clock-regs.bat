@echo off
setlocal

set "JLINK=C:\Progra~1\SEGGER\JLink_V970\JLink.exe"
set "SCRIPT=%~dp0k380-read-clock-regs.jlink"
set "LOGDIR=%~dp0logs"
set "LOG=%LOGDIR%\k380-read-clock-regs.log"

if not exist "%JLINK%" (
  echo JLink.exe not found: %JLINK%
  exit /b 1
)

if not exist "%LOGDIR%" mkdir "%LOGDIR%"

echo Reading K380 nRF CLOCK registers...
"%JLINK%" -CommanderScript "%SCRIPT%" > "%LOG%" 2>&1
set "RC=%ERRORLEVEL%"

type "%LOG%"
echo.
echo Log saved to: %LOG%
exit /b %RC%
