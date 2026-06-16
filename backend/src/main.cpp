#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <signal.h>
#include <string>

#include "common.h"
#include "config_loader.h"
#include "message_queue.h"

#include "udp_receiver.h"
#include "seismic_simulator.h"
#include "sensitivity_analyzer.h"
#include "alarm_mqtt.h"

#include "column_simulation.h"
#include "sensitivity_analysis.h"
#include "clickhouse_client.h"
#include "http_server.h"

using namespace seismograph;

volatile sig_atomic_t g_running = 1;

ColumnSimulation* g_simulation = nullptr;
SensitivityAnalysis* g_sensitivity = nullptr;
std::shared_ptr<ClickHouseClient> g_clickhouse = nullptr;

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        g_running = 0;
    }
}

int main(int argc, char* argv[]) {
    std::string config_path = "config.json";
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  --config PATH           Path to config.json (default: config.json)\n"
                      << "  --help                  Show this help\n";
            return 0;
        }
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    std::cout << "=== 古代地动仪都柱地震响应仿真与灵敏度分析系统 ===" << std::endl;
    std::cout << "=== 模块化架构: UDP Receiver → Seismic Simulator → Alarm MQTT ===" << std::endl;
    
    AppConfig config;
    if (!ConfigLoader::load(config_path, config)) {
        std::cerr << "Warning: Failed to load config from " << config_path 
                  << ", using defaults" << std::endl;
        config.dynamics.mass = 1000.0;
        config.dynamics.height = 2.5;
        config.dynamics.stiffness = 50000.0;
        config.dynamics.damping_coefficient = 500.0;
        config.dynamics.trigger_threshold = 0.05;
        config.services.udp_port = 12345;
        config.services.http_port = 8080;
        config.services.device_id = "device_001";
    } else {
        std::cout << "Config loaded from " << config_path << std::endl;
    }
    
    auto sensor_queue = std::make_shared<MessageQueue<SensorMessage>>(4096);
    auto simulation_queue = std::make_shared<MessageQueue<SimulationMessage>>(4096);
    auto sensitivity_queue = std::make_shared<MessageQueue<SensitivityMessage>>(4096);
    
    auto clickhouse = std::make_shared<ClickHouseClient>();
    g_clickhouse = clickhouse;
    
    std::cout << "Connecting to ClickHouse at " << config.services.clickhouse_host 
              << ":" << config.services.clickhouse_port << "..." << std::endl;
    if (clickhouse->connect(config.services.clickhouse_host, config.services.clickhouse_port,
                            config.services.clickhouse_database)) {
        std::cout << "ClickHouse connected successfully" << std::endl;
        clickhouse->start_async_writer();
    } else {
        std::cout << "Warning: Failed to connect to ClickHouse, will retry later" << std::endl;
    }
    
    UDPReceiver udp_receiver;
    udp_receiver.set_config(config.dynamics);
    if (!udp_receiver.start(config.services.udp_port, sensor_queue)) {
        std::cerr << "Failed to start UDP receiver on port " << config.services.udp_port << std::endl;
        return 1;
    }
    
    SeismicSimulator seismic_simulator;
    seismic_simulator.set_dynamics_config(config.dynamics);
    seismic_simulator.set_soil_type(ConfigLoader::soil_type_from_string(config.soil.default_type));
    seismic_simulator.start(sensor_queue, simulation_queue);
    
    g_simulation = nullptr;
    
    SensitivityAnalyzer sensitivity_analyzer;
    sensitivity_analyzer.set_dynamics_config(config.dynamics);
    sensitivity_analyzer.set_sensitivity_config(config.sensitivity);
    sensitivity_analyzer.set_soil_type(ConfigLoader::soil_type_from_string(config.soil.default_type));
    sensitivity_analyzer.start(sensitivity_queue);
    
    g_sensitivity = nullptr;
    
    AlarmMQTT alarm_mqtt;
    alarm_mqtt.set_alert_config(config.alert);
    alarm_mqtt.start(sensor_queue, simulation_queue, sensitivity_queue,
                     config.services.mqtt_host, config.services.mqtt_port,
                     "seismograph_backend_" + config.services.device_id);
    
    ColumnSimulation http_sim;
    http_sim.set_column_params(ConfigLoader::to_column_params(config.dynamics));
    http_sim.set_soil_type(ConfigLoader::soil_type_from_string(config.soil.default_type));
    g_simulation = &http_sim;
    
    SensitivityAnalysis http_sensitivity;
    http_sensitivity.set_column_simulation(&http_sim);
    g_sensitivity = &http_sensitivity;
    
    HttpServer http_server;
    if (!http_server.start(config.services.http_port)) {
        std::cerr << "Failed to start HTTP server on port " << config.services.http_port << std::endl;
        udp_receiver.stop();
        return 1;
    }
    
    std::cout << "\n=== 服务已启动 ===" << std::endl;
    std::cout << "UDP监听端口: " << config.services.udp_port << std::endl;
    std::cout << "HTTP API端口: " << config.services.http_port << std::endl;
    std::cout << "设备ID: " << config.services.device_id << std::endl;
    std::cout << "土壤类型: " << config.soil.default_type << std::endl;
    std::cout << "按 Ctrl+C 停止服务" << std::endl;
    std::cout << "=====================================\n" << std::endl;
    
    auto sensitivity_thread = std::thread([&]() {
        int interval_seconds = config.sensitivity.analysis_interval_minutes * 60;
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(interval_seconds));
            
            if (!g_running) break;
            
            std::cout << "\n[自动分析] 运行灵敏度分析..." << std::endl;
            
            SoilType soil = ConfigLoader::soil_type_from_string(config.soil.default_type);
            auto results = sensitivity_analyzer.analyze_magnitude_sensitivity(
                config.sensitivity.min_magnitude,
                config.sensitivity.max_magnitude,
                7,
                50.0,
                soil,
                20
            );
            
            if (clickhouse->is_connected()) {
                for (const auto& result : results) {
                    clickhouse->insert_sensitivity_result(result, config.services.device_id);
                }
            }
            
            if (!results.empty()) {
                SensitivityMessage msg;
                msg.result = results[results.size() / 2];
                msg.analysis_type = "auto_magnitude";
                msg.computed_at = current_timestamp_ms();
                sensitivity_queue->push(msg);
            }
            
            auto range = sensitivity_analyzer.calculate_detection_range(
                config.sensitivity.detection_threshold,
                config.sensitivity.false_alarm_limit,
                soil
            );
            
            std::cout << "[自动分析] 检测范围: 震级 " << range.min_magnitude 
                      << "-" << range.max_magnitude 
                      << ", 距离 " << range.min_distance 
                      << "-" << range.max_distance << "km" << std::endl;
        }
    });
    
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        SensorMessage msg;
        while (sensor_queue->pop(msg)) {
            if (clickhouse->is_connected()) {
                clickhouse->insert_sensor_data(msg.data);
            }
        }
    }
    
    std::cout << "\n正在关闭服务..." << std::endl;
    
    g_running = 0;
    
    if (sensitivity_thread.joinable()) {
        sensitivity_thread.join();
    }
    
    http_server.stop();
    udp_receiver.stop();
    seismic_simulator.stop();
    sensitivity_analyzer.stop();
    alarm_mqtt.stop();
    
    if (clickhouse->is_connected()) {
        clickhouse->stop_async_writer();
        clickhouse->flush();
        clickhouse->disconnect();
    }
    
    std::cout << "服务已关闭。再见！" << std::endl;
    
    return 0;
}
