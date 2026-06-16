@echo off
chcp 65001 >nul
echo ========================================
echo  古代地动仪仿真系统 - 构建脚本
echo ========================================
echo.

set PROJECT_DIR=%~dp0
set BUILD_DIR=%PROJECT_DIR%build
set BACKEND_DIR=%PROJECT_DIR%backend

echo [1/4] 检查构建环境...
where cmake >nul 2>&1
if %errorlevel% neq 0 (
    echo [错误] 未找到CMake，请先安装CMake并配置环境变量
    pause
    exit /b 1
)

where g++ >nul 2>&1
if %errorlevel% neq 0 (
    where clang++ >nul 2>&1
    if %errorlevel% neq 0 (
        echo [错误] 未找到C++编译器，请安装MinGW或Visual Studio
        pause
        exit /b 1
    )
)

echo [成功] 构建环境检查通过
echo.

echo [2/4] 创建构建目录...
if not exist "%BUILD_DIR%" (
    mkdir "%BUILD_DIR%"
)
echo [成功] 构建目录已创建: %BUILD_DIR%
echo.

echo [3/4] 配置CMake项目...
cd "%BUILD_DIR%"
cmake "%BACKEND_DIR%" -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
if %errorlevel% neq 0 (
    echo [错误] CMake配置失败
    pause
    exit /b 1
)
echo [成功] CMake配置完成
echo.

echo [4/4] 编译项目...
cmake --build . --config Release -j4
if %errorlevel% neq 0 (
    echo [错误] 编译失败
    pause
    exit /b 1
)

echo.
echo ========================================
echo  构建完成！
echo  可执行文件位置: %BUILD_DIR%\seismograph_backend.exe
echo ========================================
echo.
pause
