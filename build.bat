@echo off
rem Builds build\Perch.exe from src\. Prefers MinGW (WinLibs via winget), falls back to MSVC.
setlocal
cd /d "%~dp0"
if not exist build mkdir build
set SOURCES=src\notch.cpp src\media.cpp src\settings.cpp src\settingsui.cpp src\calendar.cpp src\mascot.cpp

set "MINGW=%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64"
rem Else a MinGW g++ on PATH (e.g. GitHub's Windows runners).
if not exist "%MINGW%\bin\g++.exe" for /f "delims=" %%G in ('where g++ 2^>nul') do if not defined FOUNDGXX (set "FOUNDGXX=1" & set "MINGW=%%~dpG..")
if exist "%MINGW%\bin\g++.exe" (
  rem Use the 8.3 short path: the linker breaks on the space in the user folder name.
  for %%I in ("%MINGW%") do set "MS=%%~sI"
  call :mingw
  exit /b %errorlevel%
)
where cl >nul 2>nul && (
  rem /i . lets perch.rc find assets\perch.ico from the project folder.
  rc /nologo /i . /fo build\perch.res src\perch.rc || exit /b 1
  cl /nologo /O2 /EHsc /std:c++17 /DUNICODE /D_UNICODE /Fobuild\ %SOURCES% build\perch.res /link /SUBSYSTEM:WINDOWS /OUT:build\Perch.exe runtimeobject.lib wlanapi.lib winmm.lib winhttp.lib crypt32.lib
  goto :eof
)
echo No C++ compiler found. Run: winget install BrechtSanders.WinLibs.POSIX.UCRT
exit /b 1

:mingw
set "PATH=%MS%\bin;%PATH%"
"%MS%\bin\windres.exe" --include-dir=. src\perch.rc -O coff -o build\perch.res.o
if errorlevel 1 (echo Icon resource failed.& exit /b 1)
"%MS%\bin\g++.exe" "-B%MS%\x86_64-w64-mingw32\lib\\" -O2 -s -std=c++17 -Wall -municode -mwindows %SOURCES% build\perch.res.o -o build\Perch.exe -ld2d1 -ldwrite -lole32 -ladvapi32 -lshell32 -lruntimeobject -ldwmapi -lwlanapi -lwinmm -lwinhttp -lcrypt32 -static
if errorlevel 1 (echo Build failed.& exit /b 1)
echo Built build\Perch.exe
goto :eof
