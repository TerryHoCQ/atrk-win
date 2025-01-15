@echo off
if "%PROCESSOR_ARCHITECTURE%" EQU "x86" (
    set APP_ps=%~dp0ps.exe
) else (
    set APP_ps=%~dp0ps_x64.exe
)
set APP_ports=%~dp0ports.exe
set APP_rawdir=%~dp0rawdir.exe

if "%~1"=="" (
    set OutputDir=%~dp0
) else (
    set OutputDir=%~1\\
)

:: Check if running as administrator
net session >nul 2>&1
if %errorLevel% == 0 (
    echo Running with administrator privileges. Continuing...
    goto :RUN_AS_ADMIN
) else (
    echo Not running as administrator.
    echo Press any key to continue, or close the window to exit...
    pause >nul
    echo User chose to continue. Running without administrator privileges...
    goto :RUN_WITHOUT_ADMIN
)

:RUN_AS_ADMIN
:RUN_WITHOUT_ADMIN

"%APP_ps%" -ocsv "%OutputDir%atrk-ps.csv"
echo.
"%APP_ports%" -ocsv "%OutputDir%atrk-ports.csv"
echo.
"%APP_rawdir%" fhf -ocsv "%OutputDir%atrk-fhf.csv" -depth 1 C: C:\Windows C:\Windows\System32 C:\Windows\System32\drivers

if "%~1"=="" (
pause
)
exit /b
