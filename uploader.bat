@echo off
:A
cls
pio run --target upload --upload-port COM5
if %ERRORLEVEL% neq 0 (
    echo Upload failed!
    pause
    exit /b
)
pause
goto A
