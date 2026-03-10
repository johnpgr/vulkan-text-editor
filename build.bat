@echo off
setlocal EnableExtensions EnableDelayedExpansion

for %%I in ("%~dp0.") do set "ROOT_DIR=%%~fI"
set "FLAGS_FILE=%ROOT_DIR%\compile_flags.txt"
set "MAIN_SOURCE_FILE=%ROOT_DIR%\src\main.cpp"
set "GAME_SOURCE_FILE=%ROOT_DIR%\src\game.cpp"
set "BUILD_DIR=%ROOT_DIR%\build"

if defined BUILD_MODE (
  set "MODE=%BUILD_MODE%"
) else (
  set "MODE=debug"
)

if /I "%~1"=="debug" (
  set "MODE=debug"
  shift
) else if /I "%~1"=="release" (
  set "MODE=release"
  shift
)

if "%~1"=="" (
  set "OUTPUT_FILE=%BUILD_DIR%\main.exe"
) else (
  set "OUTPUT_FILE=%~1"
)

if defined CXX (
  set "COMPILER=%CXX%"
) else (
  set "COMPILER=clang++"
)

if defined VULKAN_SDK (
  set "VULKAN_SDK_DIR=%VULKAN_SDK%"
) else (
  set "VULKAN_SDK_DIR=C:\VulkanSDK\1.4.341.1"
)

set "GAME_OUTPUT_FILE=%BUILD_DIR%\game.dll"
set "GAME_LINK_FLAGS=-shared"
set "MAIN_LINK_FLAGS=-lgdi32 -luser32"
set "WINDOWS_RUNTIME_FLAGS=-fms-runtime-lib=dll"
set VULKAN_COMPILE_FLAGS=-DVK_USE_PLATFORM_WIN32_KHR
set VULKAN_LINK_FLAGS=-L"%VULKAN_SDK_DIR%\Lib" -lvulkan-1

if not exist "%FLAGS_FILE%" (
  >&2 echo Missing compile flags file: %FLAGS_FILE%
  exit /b 1
)

if not exist "%MAIN_SOURCE_FILE%" (
  >&2 echo Missing source file: %MAIN_SOURCE_FILE%
  exit /b 1
)

if not exist "%GAME_SOURCE_FILE%" (
  >&2 echo Missing source file: %GAME_SOURCE_FILE%
  exit /b 1
)

if not exist "%VULKAN_SDK_DIR%\Include\vulkan\vulkan.h" (
  >&2 echo Missing Vulkan headers: %VULKAN_SDK_DIR%\Include\vulkan\vulkan.h
  exit /b 1
)

if not exist "%VULKAN_SDK_DIR%\Lib\vulkan-1.lib" (
  >&2 echo Missing Vulkan import library: %VULKAN_SDK_DIR%\Lib\vulkan-1.lib
  exit /b 1
)

if /I "%MODE%"=="release" (
  set "MODE_FLAGS=-O3 -DNDEBUG"
) else (
  set "MODE_FLAGS=-Og -g3 -DDEBUG"
)

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

set "FLAGS="
for /f "usebackq tokens=* delims=" %%L in ("%FLAGS_FILE%") do (
  set "LINE=%%L"
  for /f "tokens=* delims= " %%A in ("!LINE!") do set "LINE=%%A"
  if defined LINE (
    if not "!LINE:~0,1!"=="#" (
      for /f "tokens=1 delims=#" %%A in ("!LINE!") do set "LINE=%%A"
      for /f "tokens=* delims= " %%A in ("!LINE!") do set "LINE=%%A"
      if defined LINE (
        if /I "%MODE%"=="release" (
          if /I not "!LINE:~0,11!"=="-fsanitize" set "FLAGS=!FLAGS! !LINE!"
        ) else (
          set "FLAGS=!FLAGS! !LINE!"
        )
      )
    )
  )
)

call "%COMPILER%" %CPPFLAGS% %CFLAGS% %CXXFLAGS% %WINDOWS_RUNTIME_FLAGS% %VULKAN_COMPILE_FLAGS% !FLAGS! %MODE_FLAGS% "%MAIN_SOURCE_FILE%" %LDFLAGS% %MAIN_LINK_FLAGS% %VULKAN_LINK_FLAGS% -o "%OUTPUT_FILE%"
if errorlevel 1 exit /b %errorlevel%

call "%COMPILER%" %CPPFLAGS% %CFLAGS% %CXXFLAGS% %WINDOWS_RUNTIME_FLAGS% %VULKAN_COMPILE_FLAGS% !FLAGS! %MODE_FLAGS% "%GAME_SOURCE_FILE%" %LDFLAGS% %GAME_LINK_FLAGS% %VULKAN_LINK_FLAGS% -o "%GAME_OUTPUT_FILE%"
if errorlevel 1 exit /b %errorlevel%

echo Built (%MODE%): %OUTPUT_FILE%
echo Built (%MODE%): %GAME_OUTPUT_FILE%
exit /b 0
