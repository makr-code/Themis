@echo off
setlocal enabledelayedexpansion
call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
cmake --preset %1
endlocal
