# 古代地动仪都柱地震响应仿真与灵敏度分析系统

> 基于 C++ 多体动力学 + Three.js 3D 可视化 + ClickHouse 时序存储的现代张衡地动仪数字化重建系统。

---

## 目录

- [一、系统架构](#一系统架构)
- [二、模块说明](#二模块说明)
- [三、快速部署](#三快速部署)
- [四、地动仪模拟器使用说明](#四地动仪模拟器使用说明)
- [五、API 接口](#五api-接口)
- [六、Prometheus 指标](#六prometheus-指标)
- [七、本地开发](#七本地开发)

---

## 一、系统架构

```
                                ┌───────────────────────────────────┐
                                │           Web 浏览器              │
                                │  ┌──────────────┐ ┌────────────┐  │
                                │  │ 3D 地动仪可视化│ │灵敏度面板 │  │
                                │  └──────┬───────┘ └──────┬─────┘  │
                                └─────────┼────────────────┼────────┘
                                          │ HTTP/WebSocket │
                                          ▼                ▼
                                ┌────────────────────────────────────┐
                                │   frontend (Nginx, Gzip/Brotli)    │
                                │   静态资源 + /api 反向代理           │
                                └───────────────┬────────────────────┘
                                                │ /api/*, /metrics
                                                ▼
 ┌──────────────────────────────────────────────────────────────────────────────┐
 │                               backend (C++)                                    │
 │                                                                                │
 │ ┌──────────────┐   Lockfree     ┌─────────────────┐    MQTT      ┌──────────┐  │
 │ │ UDP Receiver │ ──Queue──────▶ │ Seismic         │ ──Pub──────▶ │ MQTT     │  │
 │ │ (采集+校验)   │   SensorMsg    │ Simulator       │   Alert      │ Broker   │  │
 │ └──────────────┘                │ (动力学+触发)    │              │ (Mosquitto)│
 │         ▲                       └────────┬────────┘              └────┬─────┘  │
 │         │ UDP                  SimMsg   │ SensitivityMsg            │ Sub    │
 │         │                         ┌─────▼───────┐                    │        │
 │ ┌───────┴─────────┐              │ Sensitivity │ ────────┐          │        │
 │ │  Sensor 模拟器   │              │ Analyzer    │         ▼          ▼        │
 │ │  (Python)       │              │ (蒙特卡洛)   │   ┌─────────────┐          │
 │ └─────────────────┘              └─────────────┘   │ Alarm MQTT  │          │
 │                                                   │ (告警评估+推送)│          │
 │                                                   └──────┬──────┘          │
 │                                                          │                 │
 │                                                          ▼                 │
 │                                               ┌────────────────────┐       │
 │                                               │ Prometheus /metrics│       │
 │                                               └────────────────────┘       │
 └───────────────────────────────────────────────────┬──────────────────────────┘
                                                     │ INSERT
                                                     ▼
                                           ┌─────────────────┐
                                           │   ClickHouse    │
                                           │ (降采样 + TTL)   │
                                           └─────────────────┘
```

**数据流向**：
1. 地动仪模拟器通过 UDP 向 `UDP Receiver` 发送传感器数据
2. `Seismic Simulator` 消费传感器队列，计算都柱动力学 + 触发判定，写入 `simulation_queue`
3. `Sensitivity Analyzer` 定时做蒙特卡洛分析，产出检测概率/误报率，写入 `sensitivity_queue`
4. `Alarm MQTT` 消费 3 条队列，判定误触发 / 灵敏度下降告警并推送到 MQTT Broker
5. HTTP Server 提供 REST API + Prometheus `/metrics`
6. ClickHouse 存储原始数据 + 自动降采样 + TTL 分层保留

---

## 二、模块说明

### 2.1 C++ 后端模块

| 模块 | 源码 | 职责 |
|---|---|---|
| **UDP Receiver** | `backend/include/udp_receiver.h` <br> `backend/src/udp_receiver.cpp` | UDP 非阻塞接收、多协议解析 (JSON/CSV/Binary)、数据范围校验、丢包统计 |
| **Seismic Simulator** | `backend/include/seismic_simulator.h` <br> `backend/src/seismic_simulator.cpp` | 都柱多体动力学（Stribeck 摩擦 + 罚函数接触 + Rayleigh 阻尼 + 半隐式欧拉）、触发方向判定 |
| **Sensitivity Analyzer** | `backend/include/sensitivity_analyzer.h` <br> `backend/src/sensitivity_analyzer.cpp` | 震级/距离/2D 灵敏度扫描、蒙特卡洛检测概率、检测范围计算、参数优化 |
| **Alarm MQTT** | `backend/include/alarm_mqtt.h` <br> `backend/src/alarm_mqtt.cpp` | 误触发/灵敏度下降告警评估、MQTT 3.1.1 手写帧推送、滑窗统计 |
| **队列通信** | `backend/include/message_queue.h` | `MessageQueue<T>` 模板，`Boost.Lockfree` 或 `std::mutex` 双模式 |
| **日志** | `backend/include/logger.h` <br> `backend/src/logger.cpp` | `LOG_INFO/WARN/ERROR` 宏，可选 spdlog 高性能后端 |
| **指标** | `backend/include/prometheus_metrics.h` <br> `backend/src/prometheus_metrics.cpp` | Counter/Gauge/Callback 指标，`GET /metrics` 暴露 Prometheus 格式 |
| **配置** | `backend/config.json` + `backend/include/config_loader.h` | 动力学 / 地震波 / 土壤 / 灵敏度 / 告警 / 服务 6 大配置块 |

### 2.2 前端模块

| 模块 | 源码 | 职责 |
|---|---|---|
| **3D 地动仪** | `frontend/js/seismoscope_3d.js` | Three.js 场景、八方龙头/都柱/蟾蜍程序化建模、Catmull-Rom 插值动画、触发视觉反馈 |
| **灵敏度面板** | `frontend/js/sensitivity_panel.js` | Chart.js 图表、扫点配置、检测概率/误报率可视化 |
| **主控** | `frontend/js/main.js` | App 初始化、数据轮询、事件绑定 |
| **Nginx** | `frontend/nginx.conf` + `frontend/Dockerfile` | Gzip/Brotli 压缩、静态资源缓存、API 反向代理、安全头 |

### 2.3 数据层

| 组件 | 作用 |
|---|---|
| **ClickHouse** | 时序存储、**自动降采样** (1m → 5m → 1h → 1d)、**分层 TTL** (30d → 5y) |
| **Mosquitto** | MQTT 告警消息 Broker |
| **Prometheus + Grafana** | 可选监控 Profile |

---

## 三、快速部署

### 3.1 环境要求

- Docker Engine ≥ 24.0
- Docker Compose v2 ≥ 2.20
- CPU ≥ 4 核, 内存 ≥ 8GB (ClickHouse 建议 ≥ 4GB 可用)

### 3.2 一键启动（核心 4 服务）

```bash
# 1. 克隆项目并进入目录
cd AI_solo_coder_task_A_093

# 2. 启动 ClickHouse + MQTT + C++ 后端 + 前端 Nginx
docker compose up -d --build

# 3. 查看状态
docker compose ps

# 4. 查看日志
docker compose logs -f backend
```

等待所有服务健康检查通过后访问：
- 前端页面：http://localhost
- 后端 API：http://localhost:8080/api/stats
- Prometheus 指标：http://localhost:8080/metrics
- ClickHouse HTTP：http://localhost:8123

### 3.3 启动完整链路（含模拟器 + 监控）

```bash
# 启动核心服务 + 模拟器 + Prometheus + Grafana
docker compose --profile simulator --profile monitoring up -d --build

# Grafana 访问: http://localhost:3000  (admin / seismograph)
# Prometheus:     http://localhost:9090
```

### 3.4 仅启动单个组件

```bash
# 只跑后端
docker compose up -d clickhouse mqtt backend

# 只跑前端（需先配置好后端地址）
docker compose up -d frontend
```

### 3.5 停止 & 清理

```bash
# 停止但保留数据
docker compose down

# 完全清理数据卷 (慎用!)
docker compose down -v
```

---

## 四、地动仪模拟器使用说明

模拟器源码：`simulator/seismograph_simulator.py`

### 4.1 快速上手

```bash
# 方式 A: 本地 Python 运行 (Python 3.10+)
cd simulator
python seismograph_simulator.py --host 127.0.0.1 --port 12345

# 方式 B: Docker 运行 (已集成在 docker-compose)
docker compose --profile simulator up -d simulator
```

### 4.2 常用命令

| 场景 | 命令 |
|---|---|
| 触发 1 次 M5.0 / 50km 地震 (II 类土) | `python seismograph_simulator.py --trigger-once --magnitude 5.0 --distance 50 --soil 1` |
| 连续随机触发，震级 3~6.5，20~200km | `python seismograph_simulator.py --magnitude-min 3 --magnitude-max 6.5 --distance-min 20 --distance-max 200` |
| 批处理 10 次地震后退出，固定种子 | `python seismograph_simulator.py --batch 10 --seed 42 --earthquake-interval 5` |
| 指定 IV 类极软土，方向 0°(正东) | `python seismograph_simulator.py --trigger-once --soil 3 --direction 0` |
| 仅发送传感器微扰，不触发地震 | `python seismograph_simulator.py --no-auto-earthquake` |

### 4.3 完整参数

```bash
python seismograph_simulator.py --help
```

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `--host` | str | `127.0.0.1` | 后端服务器地址 |
| `--port` | int | `12345` | 后端 UDP 端口 |
| `--device-id` | str | `device_001` | 设备 ID |
| `--interval` | float | `60.0` | 上报间隔 (秒) |
| `--protocol` | enum | `json` | 协议：`json` / `csv` / `binary` |
| **`--soil`** | int | `1` | 场地土类型：`0`=I类坚硬岩 / `1`=II类中硬土 / `2`=III类软弱土 / `3`=IV类极软土 |
| **`--seed`** | int | 无 | 随机种子，用于复现实验 |
| `--no-auto-earthquake` | flag | - | 禁用自动随机地震 |
| `--earthquake-interval` | float | `300.0` | 自动地震间隔 (秒) |
| **`--magnitude`** | float | `5.0` | 单次触发震级 (配合 `--trigger-once`) |
| **`--distance`** | float | `50.0` | 单次触发震中距 km |
| `--duration` | float | `10.0` | 单次地震持续秒数 |
| `--direction` | float | 随机 | 震源方向角度，0°=正东 |
| **`--magnitude-min/max`** | float | `2.0 / 7.0` | 随机震级范围 |
| **`--distance-min/max`** | float | `10.0 / 500.0` | 随机震中距范围 |
| `--trigger-once` | flag | - | 触发一次地震后退出 |
| `--batch N` | int | 无 | 批处理模式：触发 N 次地震后退出 |
| `--stop-after N` | float | 无 | 运行 N 秒后退出 |

### 4.4 土壤放大系数

| `--soil` 值 | 类型 | 放大系数 | 适用条件 |
|---|---|---|---|
| 0 | I 类坚硬岩 | 0.85 | 基岩 / 稳定岩石 |
| 1 | II 类中硬土 | 1.00 | 一般建筑场地 (默认) |
| 2 | III 类软弱土 | 1.30 | 中软土 / 松散层较厚 |
| 3 | IV 类极软土 | 1.80 | 淤泥 / 软弱土层 |

---

## 五、API 接口

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/api/stats` | 服务运行统计 |
| GET | `/api/sensor-data` | 查询最近传感器数据 |
| POST | `/api/sensor-data` | 手动写入传感器数据 |
| GET | `/api/alerts` | 查询告警记录 |
| GET | `/api/sensitivity` | 查询灵敏度分析结果 |
| POST | `/api/simulation` | 运行单次都柱仿真 |
| POST | `/api/sensitivity-analysis` | 运行灵敏度分析 |
| GET | `/metrics` | Prometheus 指标 |
| GET | `/health` | 健康检查 |

示例：
```bash
# 检查健康
curl http://localhost:8080/api/stats

# 抓取指标
curl http://localhost:8080/metrics

# 运行单次仿真 (M5.5 / 30km / II 类土)
curl -X POST http://localhost:8080/api/simulation \
  -H "Content-Type: application/json" \
  -d '{"magnitude":5.5,"distance":30,"soil_type":1}'
```

---

## 六、Prometheus 指标

访问 `http://localhost:8080/metrics` 查看全部指标。核心指标：

| 指标名 | 类型 | 说明 |
|---|---|---|
| `seismograph_udp_packets_total` | Counter | 累计接收 UDP 包数 |
| `seismograph_simulations_total` | Counter | 累计仿真次数 |
| `seismograph_triggers_total` | Counter | 累计触发都柱次数 |
| `seismograph_alerts_total` | Counter | 累计告警条数 |
| `seismograph_mqtt_messages_total` | Counter | 累计推送 MQTT 消息数 |
| `seismograph_clickhouse_writes_total` | Counter | 累计写入 ClickHouse 行数 |
| `seismograph_http_requests_total` | Counter | 累计 HTTP 请求数 |
| `seismograph_queue_sensor_depth` | Gauge | 传感器队列深度 |
| `seismograph_queue_simulation_depth` | Gauge | 仿真队列深度 |
| `seismograph_queue_sensitivity_depth` | Gauge | 灵敏度队列深度 |
| `seismograph_detection_probability` | Gauge | 当前检测概率 |
| `seismograph_false_alarm_rate` | Gauge | 当前误报率 |
| `seismograph_sensitivity_score` | Gauge | 综合灵敏度评分 P(1-F) |
| `seismograph_uptime_seconds` | Gauge | 服务运行秒数 |

Grafana 导入面板：使用 provisioning 自动配置 `ClickHouse` 和 `Prometheus` 数据源。

---

## 七、本地开发

### 7.1 后端 (C++)

```bash
# 依赖: CMake 3.15+, GCC 13+ 或 MSVC 2022, Boost 1.74+, spdlog 1.12+

cd backend
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DUSE_BOOST_LOCKFREE=ON \
  -DUSE_SPDLOG=ON
cmake --build build -j

./build/seismograph_backend --config config.json
```

### 7.2 前端 (Node 本地静态服务器，含压缩)

```bash
# Node 18+
cd frontend
# 直接启动（默认端口 3000，代理后端至 8080）
node server.js

# 自定义端口
PORT=3001 BACKEND_URL=http://127.0.0.1:8080 node server.js
```

或使用 Nginx：
```bash
docker run -p 80:80 \
  -v $(pwd)/frontend/nginx.conf:/etc/nginx/nginx.conf:ro \
  -v $(pwd)/frontend:/usr/share/nginx/html:ro \
  nginx:1.27-alpine
```

### 7.3 运行功能回归测试

```bash
cd tests
python regression_test.py
# 期望输出: 测试结果: 7 通过, 0 失败, 共 7 项
```

覆盖：Stribeck 摩擦平滑性、罚函数接触连续性、半隐式欧拉+Rayleigh 能量稳定性、GB50011 土壤放大系数、八方触发方向、模块队列通信、蒙特卡洛灵敏度分析。

---

## 八、技术栈总结

| 层 | 技术 |
|---|---|
| **前端** | Three.js (3D), Chart.js (图表), Nginx (Gzip/Brotli) |
| **后端** | C++17, Boost.Lockfree, spdlog, 手写 MQTT 3.1.1 |
| **动力学** | Stribeck 摩擦 + 罚函数法 + Rayleigh 阻尼 + 半隐式欧拉 |
| **存储** | ClickHouse (SummingMergeTree + TTL + 物化视图降采样) |
| **消息** | MQTT (Mosquitto), Boost.Lockfree / std::mutex 双模式队列 |
| **可观测** | Prometheus `/metrics`, Grafana, 结构化日志 |
| **部署** | Docker Compose, 多阶段构建 |

---

© 地动仪仿真团队 · 基于张衡候风地动仪原理重建
