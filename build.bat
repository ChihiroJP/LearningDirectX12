@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
cd /d C:\Users\chiha\Documents\Project\LearningDirectX12
cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Debug
if %ERRORLEVEL% NEQ 0 (
    echo CMAKE_CONFIGURE_FAILED
    exit /b 1
)
cmake --build build
if %ERRORLEVEL% NEQ 0 (
    echo CMAKE_BUILD_FAILED
    exit /b 1
)
echo BUILD_SUCCESS
