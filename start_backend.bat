@echo off
chcp 65001 >nul
echo ========================================
echo  古代地动仪仿真系统 - 启动后端服务
echo ========================================
echo.

set PROJECT_DIR=%~dp0
set BUILD_DIR=%PROJECT_DIR%build
set EXE_PATH=%BUILD_DIR%\seismograph_backend.exe

if not exist "%EXE_PATH%" (
    echo [警告] 未找到编译后的可执行文件，尝试先构建...
    call "%PROJECT_DIR%build.bat"
    if %errorlevel% neq 0 (
        echo [错误] 构建失败，无法启动服务
        pause
        exit /b 1
    )
)

echo [信息] 启动后端服务...
echo [配置] UDP端口: 8888
echo [配置] HTTP端口: 8080
echo [配置] ClickHouse: localhost:8123
echo [配置] MQTT: localhost:1883
echo.
echo [提示] 按 Ctrl+C 停止服务
echo.

"%EXE_PATH%" --udp-port 8888 --http-port 8080 --clickhouse-host localhost --mqtt-host localhost --device-id device_001

echo.
echo [信息] 服务已停止
pause
