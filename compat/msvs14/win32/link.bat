@echo off
@set PATH=%PATH%;%VS140COMNTOOLS%..\..\VC\bin\;%VS140COMNTOOLS%..\..\Common7\IDE

if "%ProgramFiles%" == "%ProgramFiles(x86)%" goto x64_PATH
if "%ProgramFiles%" == "%ProgramW6432%" goto x86_PATH

:x64_PATH
@link.exe /libpath:"%VS140COMNTOOLS%..\..\VC\lib" /libpath:"%ProgramFiles(x86)%\Microsoft SDKs\Windows\v7.0A\Lib" %*
goto :eof

:x86_PATH
@link.exe /libpath:"%VS140COMNTOOLS%..\..\VC\lib" /libpath:"%ProgramFiles%\Microsoft SDKs\Windows\v7.0A\Lib" %*
goto :eof
