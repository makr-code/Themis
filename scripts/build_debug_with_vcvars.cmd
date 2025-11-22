@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
cmake --build build-msvc-ninja-debug -j
endlocal
