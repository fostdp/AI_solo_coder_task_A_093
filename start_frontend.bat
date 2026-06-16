@echo off
chcp 65001 >nul
echo ========================================
echo  古代地动仪仿真系统 - 启动前端
echo ========================================
echo.

set PROJECT_DIR=%~dp0
set FRONTEND_DIR=%PROJECT_DIR%frontend

where python >nul 2>&1
if %errorlevel% neq 0 (
    echo [错误] 未找到Python，无法启动HTTP服务器
    echo [提示] 您也可以直接在浏览器中打开: %FRONTEND_DIR%\index.html
    pause
    exit /b 1
)

echo [信息] 启动前端HTTP服务器...
echo [信息] 前端地址: http://localhost:8000
echo [提示] 按 Ctrl+C 停止服务器
echo.

cd "%FRONTEND_DIR%"
python -m http.server 8000

echo.
echo [信息] 前端服务器已停止
pause
