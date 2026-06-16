#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <signal.h>
#include "common.h"
#include "column_simulation.h"
#include "sensitivity_analysis.h"
#include "udp_server.h"
#include "clickhouse_client.h"
#include "mqtt_client.h"
#include "http_server.h"
#include "alert_manager.h"

using namespace seismograph;

ColumnSimulation* g_simulation = nullptr;
SensitivityAnalysis* g_sensitivity = nullptr;
std::shared_ptr<ClickHouseClient> g_clickhouse = nullptr;

volatile sig_atomic_t g_running = 1;

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        g_running = 0;
    }
}

void on_sensor_data_received(const SensorData& data,
                             std::shared_ptr<ClickHouseClient> clickhouse,
                             std::shared_ptr<AlertManager> alert_manager,
                             ColumnSimulation* simulation) {
    
    if (clickhouse && clickhouse->is_connected()) {
        clickhouse->insert_sensor_data(data);
    }
    
    if (simulation && alert_manager) {
        SeismicWaveParams params;
        params.magnitude = data.magnitude;
        params.epicenter_distance = data.epicenter_distance;
        params.duration = 10.0;
        
        SimulationResult sim_result = simulation->simulate(params, 0.001);
        alert_manager->process_sensor_data(data, sim_result);
    }
    
    std::cout << "[" << data.timestamp << "] Received data from " << data.device_id
              << " | Mag: " << data.magnitude 
              << " | Dist: " << data.epicenter_distance << "km"
              << " | Triggered: " << (data.is_triggered ? "Yes" : "No")
              << std::endl;
}

int main(int argc, char* argv[]) {
    int udp_port = 12345;
    int http_port = 8080;
    std::string clickhouse_host = "127.0.0.1";
    int clickhouse_port = 8123;
    std::string mqtt_host = "127.0.0.1";
    int mqtt_port = 1883;
    std::string device_id = "device_001";
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--udp-port" && i + 1 < argc) udp_port = std::stoi(argv[++i]);
        else if (arg == "--http-port" && i + 1 < argc) http_port = std::stoi(argv[++i]);
        else if (arg == "--clickhouse-host" && i + 1 < argc) clickhouse_host = argv[++i];
        else if (arg == "--clickhouse-port" && i + 1 < argc) clickhouse_port = std::stoi(argv[++i]);
        else if (arg == "--mqtt-host" && i + 1 < argc) mqtt_host = argv[++i];
        else if (arg == "--mqtt-port" && i + 1 < argc) mqtt_port = std::stoi(argv[++i]);
        else if (arg == "--device-id" && i + 1 < argc) device_id = argv[++i];
        else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  --udp-port PORT          UDP port for sensor data (default: 12345)\n"
                      << "  --http-port PORT         HTTP API port (default: 8080)\n"
                      << "  --clickhouse-host HOST   ClickHouse host (default: 127.0.0.1)\n"
                      << "  --clickhouse-port PORT   ClickHouse port (default: 8123)\n"
                      << "  --mqtt-host HOST         MQTT broker host (default: 127.0.0.1)\n"
                      << "  --mqtt-port PORT         MQTT broker port (default: 1883)\n"
                      << "  --device-id ID           Device ID (default: device_001)\n"
                      << "  --help                   Show this help\n";
            return 0;
        }
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    std::cout << "=== 古代地动仪都柱地震响应仿真与灵敏度分析系统 ===" << std::endl;
    std::cout << "Starting backend server..." << std::endl;
    
    ColumnSimulation simulation;
    g_simulation = &simulation;
    
    ColumnParams column_params;
    column_params.mass = 1000.0;
    column_params.height = 2.5;
    column_params.base_radius = 0.3;
    column_params.top_radius = 0.15;
    column_params.stiffness = 50000.0;
    column_params.damping_coefficient = 500.0;
    column_params.static_friction = 0.6;
    column_params.dynamic_friction = 0.4;
    column_params.trigger_threshold = 0.05;
    simulation.set_column_params(column_params);
    
    std::cout << "都柱参数: 质量=" << column_params.mass << "kg, 高度=" 
              << column_params.height << "m, 刚度=" << column_params.stiffness 
              << "N/m" << std::endl;
    
    SensitivityAnalysis sensitivity;
    g_sensitivity = &sensitivity;
    sensitivity.set_column_simulation(&simulation);
    
    auto clickhouse = std::make_shared<ClickHouseClient>();
    g_clickhouse = clickhouse;
    
    std::cout << "Connecting to ClickHouse at " << clickhouse_host << ":" << clickhouse_port << "..." << std::endl;
    if (clickhouse->connect(clickhouse_host, clickhouse_port, "seismograph")) {
        std::cout << "ClickHouse connected successfully" << std::endl;
        clickhouse->start_async_writer();
    } else {
        std::cout << "Warning: Failed to connect to ClickHouse, will retry..." << std::endl;
    }
    
    auto mqtt_client = std::make_shared<MqttClient>();
    
    std::cout << "Connecting to MQTT broker at " << mqtt_host << ":" << mqtt_port << "..." << std::endl;
    if (mqtt_client->connect(mqtt_host, mqtt_port, "seismograph_backend_" + device_id)) {
        std::cout << "MQTT connected successfully" << std::endl;
        mqtt_client->start_async_publisher();
    } else {
        std::cout << "Warning: Failed to connect to MQTT broker, alerts will be stored locally" << std::endl;
    }
    
    auto alert_manager = std::make_shared<AlertManager>();
    alert_manager->set_mqtt_client(mqtt_client);
    alert_manager->set_clickhouse_client(clickhouse);
    alert_manager->start_monitoring();
    
    UDPServer udp_server;
    
    auto data_callback = [&](const SensorData& data) {
        on_sensor_data_received(data, clickhouse, alert_manager, &simulation);
    };
    
    if (!udp_server.start(udp_port, data_callback)) {
        std::cerr << "Failed to start UDP server on port " << udp_port << std::endl;
        return 1;
    }
    
    HttpServer http_server;
    if (!http_server.start(http_port)) {
        std::cerr << "Failed to start HTTP server on port " << http_port << std::endl;
        udp_server.stop();
        return 1;
    }
    
    std::cout << "\n=== 服务已启动 ===" << std::endl;
    std::cout << "UDP监听端口: " << udp_port << std::endl;
    std::cout << "HTTP API端口: " << http_port << std::endl;
    std::cout << "设备ID: " << device_id << std::endl;
    std::cout << "按 Ctrl+C 停止服务" << std::endl;
    std::cout << "=====================================\n" << std::endl;
    
    auto sensitivity_thread = std::thread([&]() {
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::minutes(5));
            
            if (!g_running) break;
            
            std::cout << "\n[自动分析] 运行灵敏度分析..." << std::endl;
            
            auto results = sensitivity.analyze_magnitude_sensitivity(2.0, 8.0, 7, 50.0, 20);
            
            if (clickhouse->is_connected()) {
                for (const auto& result : results) {
                    clickhouse->insert_sensitivity_result(result, device_id);
                }
            }
            
            alert_manager->process_sensitivity_result(results[results.size() / 2]);
            
            auto range = sensitivity.calculate_detection_range();
            std::cout << "[自动分析] 检测范围: 震级 " << range.min_magnitude 
                      << "-" << range.max_magnitude 
                      << ", 距离 " << range.min_distance 
                      << "-" << range.max_distance << "km" << std::endl;
        }
    });
    
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    std::cout << "\n正在关闭服务..." << std::endl;
    
    g_running = 0;
    
    if (sensitivity_thread.joinable()) {
        sensitivity_thread.join();
    }
    
    alert_manager->stop_monitoring();
    http_server.stop();
    udp_server.stop();
    
    if (mqtt_client->is_connected()) {
        mqtt_client->stop_async_publisher();
        mqtt_client->disconnect();
    }
    
    if (clickhouse->is_connected()) {
        clickhouse->stop_async_writer();
        clickhouse->flush();
        clickhouse->disconnect();
    }
    
    std::cout << "服务已关闭。再见！" << std::endl;
    
    return 0;
}
