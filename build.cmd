@echo off
setlocal enabledelayedexpansion

where /Q cl.exe || (
  set __VSCMD_ARG_NO_LOGO=1
  for /f "tokens=*" %%i in ('"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.VisualStudio.Workload.NativeDesktop -property installationPath') do set VS=%%i
  if "!VS!" equ "" (
    echo ERROR: Visual Studio installation not found
    exit /b 1
  )  
  call "!VS!\VC\Auxiliary\Build\vcvarsall.bat" amd64 || exit /b 1
)

if "%VSCMD_ARG_TGT_ARCH%" neq "x64" (
  echo ERROR: please run this from MSVC x64 native tools command prompt, 32-bit target is not supported!
  exit /b 1
)

if "%1" equ "debug" (
  set CL=/W3 /WX /MTd /Od /Zi /D_DEBUG /RTC1 /Fdwstream.pdb /fsanitize=address
  set LINK=/DEBUG libucrtd.lib libvcruntimed.lib
) else (
  set CL=/W3 /WX /GL /O1 /DNDEBUG /GS-
  set LINK=/LTCG /OPT:REF /OPT:ICF libvcruntime.lib
)

cl.exe /nologo /MP *.c /Fewstream.exe /link /INCREMENTAL:NO /MANIFEST:EMBED /MANIFESTINPUT:wstream.manifest /SUBSYSTEM:CONSOLE /FIXED /merge:_RDATA=.rdata
del *.obj *.res >nul
