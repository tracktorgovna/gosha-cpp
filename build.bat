@echo off
setlocal EnableDelayedExpansion

set "VCVARS="
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat"
)

if defined VCVARS (
    call "%VCVARS%"
) else (
    echo Visual Studio не найдена. Продолжаю — если уже в Developer Command Prompt, всё OK.
)

rem rc.exe — часть Windows SDK, ищем вручную если не в PATH
where rc.exe >nul 2>&1
if %ERRORLEVEL% neq 0 (
    set "RC_EXE="
    for /d %%v in ("C:\Program Files (x86)\Windows Kits\10\bin\10.*") do (
        if exist "%%v\x64\rc.exe" set "RC_EXE=%%v\x64\rc.exe"
    )
    for /d %%v in ("C:\Program Files\Windows Kits\10\bin\10.*") do (
        if exist "%%v\x64\rc.exe" set "RC_EXE=%%v\x64\rc.exe"
    )
    if defined RC_EXE (
        echo rc.exe найден: !RC_EXE!
        set "PATH=!PATH!;%~dp0"
        copy /y "!RC_EXE!" "%~dp0rc.exe" >nul
    ) else (
        echo ОШИБКА: rc.exe не найден. Установи Windows SDK.
        pause & exit /b 1
    )
)

echo.
echo [1/2] Компиляция ресурсов...
rc /nologo /c 65001 /fo app.res app.rc
if %ERRORLEVEL% neq 0 (
    echo ОШИБКА: rc.exe завершился с кодом %ERRORLEVEL%
    pause & exit /b 1
)

echo [2/2] Компиляция...
cl /nologo /EHsc /W3 /O2 /utf-8 /DUNICODE /D_UNICODE /I. ^
   main.cpp app.res ^
   sqlite3.lib user32.lib gdi32.lib comctl32.lib shell32.lib ^
   /link /LIBPATH:. /SUBSYSTEM:WINDOWS /OUT:cloud_audit.exe

if %ERRORLEVEL% equ 0 (
    echo.
    echo Готово: cloud_audit.exe
    del /q main.obj app.res rc.exe 2>nul
) else (
    echo.
    echo ОШИБКА сборки.
)

pause
