@echo off
rem Run from a Visual Studio x64 Native Tools command prompt, after python build.py.
cd /d "%~dp0.."
cl /nologo /std:c11 /utf-8 /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /Iinclude /Ithird_party /Ibuild /Febuild\test_text.exe /Fobuild\ tests\test_text.c source\text.c source\view.c source\ui.c
if errorlevel 1 exit /b 1
build\test_text.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /utf-8 /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /Iinclude /Febuild\test_autoindent.exe /Fobuild\ tests\test_autoindent.c source\text.c
if errorlevel 1 exit /b 1
build\test_autoindent.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /utf-8 /O2 /W3 /Iinclude /Febuild\test_typer.exe /Fobuild\ tests\test_typer.c source\typer.c
if errorlevel 1 exit /b 1
build\test_typer.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /utf-8 /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /Itests\mock /Iinclude /Febuild\test_usb.exe /Fobuild\ tests\test_usb.c source\usb_keyboard.c
if errorlevel 1 exit /b 1
build\test_usb.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /utf-8 /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /Iinclude /Ithird_party /Ibuild /Febuild\preview.exe /Fobuild\ tests\preview.c source\text.c source\view.c source\ui.c
if errorlevel 1 exit /b 1
cd build
preview.exe
