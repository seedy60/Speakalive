@echo off
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -property installationPath`) do set "VSPATH=%%i"
call "%VSPATH%\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d C:\git\Speakalive\build
echo --- compiling test ---
cl /nologo /c /W3 /GS- /DWIN32 /D_WIN32_WINNT=0x0500 /DWINVER=0x0500 /DCOBJMACROS ..\tools\test_save.c
if errorlevel 1 ( echo COMPILE FAILED & exit /b 1 )
echo --- linking test ---
link /nologo /NODEFAULTLIB /ENTRY:WinMainCRTStartup /SUBSYSTEM:CONSOLE /MACHINE:X86 ^
 /OUT:test_save.exe test_save.obj sapi5.obj onecore.obj sapi4.obj audiofile.obj util.obj ^
 kernel32.lib user32.lib ole32.lib msacm32.lib winmm.lib sapi.lib uuid.lib
if errorlevel 1 ( echo LINK FAILED & exit /b 1 )
echo --- running test ---
test_save.exe
echo --- exit %errorlevel% ---
endlocal
