@echo off
setlocal

set "ROOT=%~dp0"
set "MAVEN_CMD=%USERPROFILE%\tools\apache-maven-3.9.9\bin\mvn.cmd"
set "CMAKE_EXE=%USERPROFILE%\tools\cmake-4.3.1-windows-x86_64\bin\cmake.exe"

if not exist "%MAVEN_CMD%" (
  where mvn >nul 2>nul
  if %errorlevel% neq 0 (
    echo [ERROR] Maven not found. Expected: "%MAVEN_CMD%" or PATH mvn.
    exit /b 1
  )
  set "MAVEN_CMD=mvn"
)

if not exist "%CMAKE_EXE%" (
  where cmake >nul 2>nul
  if %errorlevel% neq 0 (
    echo [ERROR] CMake not found.
    echo [HINT] Expected local CMake path: %USERPROFILE%\tools\cmake-4.3.1-windows-x86_64\bin\cmake.exe
    echo [HINT] Install CMake binary package or update CMAKE_EXE in this script.
    exit /b 1
  )
  set "CMAKE_EXE=cmake"
)

echo [1/3] Build Spring Boot monitor UI...
cd /d "%ROOT%chat-ui"
call "%MAVEN_CMD%" -DskipTests clean package
if %errorlevel% neq 0 (
  echo [ERROR] Maven build failed.
  exit /b 1
)

echo [2/3] Configure CMake project...
cd /d "%ROOT%"
if not exist "cmake-build-release" mkdir cmake-build-release
call "%CMAKE_EXE%" -S . -B cmake-build-release -DCMAKE_BUILD_TYPE=Release
if %errorlevel% neq 0 (
  echo [ERROR] CMake configure failed.
  exit /b 1
)

echo [3/3] Build C++ executable...
call "%CMAKE_EXE%" --build cmake-build-release --config Release
if %errorlevel% neq 0 (
  echo [ERROR] C++ build failed.
  exit /b 1
)

echo.
echo Build success.
echo EXE: %ROOT%cmake-build-release\Release\untitled18.exe
echo Run and choose mode 4 for one-click startup.
endlocal
