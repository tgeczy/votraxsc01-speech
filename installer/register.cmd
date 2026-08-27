@echo off
rem Registers the Votrax SC-01 SAPI voices for both registry views.
rem Safe to run any number of times; registration only creates voice
rem tokens for ROM files it actually finds beside each DLL.
if exist "%~dp0x64\votrax_sapi.dll" %windir%\System32\regsvr32.exe /s "%~dp0x64\votrax_sapi.dll"
if exist "%~dp0x86\votrax_sapi.dll" if exist %windir%\SysWOW64\regsvr32.exe %windir%\SysWOW64\regsvr32.exe /s "%~dp0x86\votrax_sapi.dll"
exit /b 0
