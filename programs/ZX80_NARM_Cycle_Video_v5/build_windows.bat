@echo off
setlocal
if "%NARMCC%"=="" set "NARMCC=narmcc.exe"
cd /d "%~dp0source"
"%NARMCC%" zx80_narm.tc -I . -O1 --stack-bytes 12288 -o "..\zx80.narm"
if errorlevel 1 exit /b 1
echo Built zx80.narm
