@echo off
chcp 65001 >nul
echo ========================================
echo  古代地动仪仿真系统 - 启动模拟器
echo ========================================
echo.

set PROJECT_DIR=%~dp0
set SIMULATOR_PATH=%PROJECT_DIR%simulator\seismograph_simulator.py

where python >nul 2>&1
if %errorlevel% neq 0 (
    echo [错误] 未找到Python，请先安装Python 3.7+
    pause
    exit /b 1
)

echo [信息] 检查Python依赖...
python -c "import numpy" >nul 2>&1
if %errorlevel% neq 0 (
    echo [信息] 安装依赖包 numpy...
    pip install numpy
)

echo.
echo [配置] 目标UDP地址: localhost:8888
echo [配置] 设备ID: device_001
echo [配置] 上报间隔: 60秒
echo.
echo [提示] 按 Ctrl+C 停止模拟器
echo.

python "%SIMULATOR_PATH%" --host localhost --port 8888 --device-id device_001 --interval 60 --format json --auto-earthquake --earthquake-interval 300

echo.
echo [信息] 模拟器已停止
pause
