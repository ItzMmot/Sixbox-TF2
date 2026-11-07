@echo off
setlocal
pushd "%~dp0"
set "GAME_EXE=tf_win64.exe"
set "INJECTOR_PATH=DLL_Injector-x64-Release.exe"
set "DLL_PATH=SixBoxx64ReleaseFreetypeAVX2.dll"
if not exist "%INJECTOR_PATH%" (
    echo SixBox: missing injector: %INJECTOR_PATH%
    exit /b 2
)
if not exist "%DLL_PATH%" (
    echo SixBox: missing DLL: %DLL_PATH%
    exit /b 3
)
"%INJECTOR_PATH%" "%DLL_PATH%" "%GAME_EXE%"
exit /b %errorlevel%
