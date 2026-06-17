#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <signal.h>
#include <string>

#include "common.h"
#include "config_loader.h"
#include "message_queue.h"
#include "logger.h"
#include "prometheus_metrics.h"

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
AppConfig* g_config = nullptr;

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        LOG_INFO("Received signal %d, shutting down...", signal);
        g_running = 0;
    }
}

void register_prometheus_metrics() {
    auto& reg = PrometheusRegistry::instance();
    
    reg.register_counter("seismograph_udp_packets_total", 
                         "Total number of UDP packets received", 
                         {{"module", "udp_receiver"}});
    reg.register_counter("seismograph_simulations_total",
                         "Total number of simulations run",
                         {{"module", "seismic_simulator"}});
    reg.register_counter("seismograph_triggers_total",
                         "Total number of column triggers detected",
                         {{"module", "seismic_simulator"}});
    reg.register_counter("seismograph_alerts_total",
                         "Total number of alerts generated",
                         {{"module", "alarm_mqtt"}});
    reg.register_counter("seismograph_mqtt_messages_total",
                         "Total number of MQTT messages published",
                         {{"module", "alarm_mqtt"}});
    reg.register_counter("seismograph_clickhouse_writes_total",
                         "Total number of ClickHouse writes",
                         {{"module", "clickhouse"}});
    reg.register_counter("seismograph_http_requests_total",
                         "Total number of HTTP requests",
                         {{"module", "http_server"}});
    
    reg.register_gauge("seismograph_queue_sensor_depth",
                       "Current depth of sensor message queue",
                       {{"queue", "sensor"}});
    reg.register_gauge("seismograph_queue_simulation_depth",
                       "Current depth of simulation message queue",
                       {{"queue", "simulation"}});
    reg.register_gauge("seismograph_queue_sensitivity_depth",
                       "Current depth of sensitivity message queue",
                       {{"queue", "sensitivity"}});
    reg.register_gauge("seismograph_detection_probability",
                       "Current column detection probability",
                       {});
    reg.register_gauge("seismograph_false_alarm_rate",
                       "Current column false alarm rate",
                       {});
    reg.register_gauge("seismograph_sensitivity_score",
                       "Current sensitivity score",
                       {});
    reg.register_gauge("seismograph_uptime_seconds",
                       "Service uptime in seconds",
                       {});
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
    
    Logger::init("logs", "seismograph", LogLevel::INFO, LogLevel::DEBUG);
    
    register_prometheus_metrics();
    
    auto start_time = std::chrono::steady_clock::now();
    
    PrometheusRegistry::instance().register_callback(
        "seismograph_uptime_seconds",
        [start_time]() {
            auto now = std::chrono::steady_clock::now();
            return std::chrono::duration<double>(now - start_time).count();
        },
        MetricType::GAUGE,
        "Service uptime in seconds"
    );
    
    LOG_INFO("=== 古代地动仪都柱地震响应仿真与灵敏度分析系统 ===");
    LOG_INFO("=== 模块化架构: UDP Receiver → Seismic Simulator → Alarm MQTT ===");
    
    AppConfig config;
    g_config = &config;
    if (!ConfigLoader::load(config_path, config)) {
        LOG_WARN("Failed to load config from %s, using defaults", config_path.c_str());
        config.dynamics.mass = 1000.0;
        config.dynamics.height = 2.5;
        config.dynamics.stiffness = 50000.0;
        config.dynamics.damping_coefficient = 500.0;
        config.dynamics.trigger_threshold = 0.05;
        config.services.udp_port = 12345;
        config.services.http_port = 8080;
        config.services.device_id = "device_001";
    } else {
        LOG_INFO("Config loaded from %s", config_path.c_str());
    }
    
    auto sensor_queue = std::make_shared<MessageQueue<SensorMessage>>(4096);
    auto simulation_queue = std::make_shared<MessageQueue<SimulationMessage>>(4096);
    auto sensitivity_queue = std::make_shared<MessageQueue<SensitivityMessage>>(4096);
    
    auto clickhouse = std::make_shared<ClickHouseClient>();
    g_clickhouse = clickhouse;
    
    LOG_INFO("Connecting to ClickHouse at %s:%u...", 
             config.services.clickhouse_host.c_str(), config.services.clickhouse_port);
    if (clickhouse->connect(config.services.clickhouse_host, config.services.clickhouse_port,
                            config.services.clickhouse_database)) {
        LOG_INFO("ClickHouse connected successfully");
        clickhouse->start_async_writer();
    } else {
        LOG_WARN("Failed to connect to ClickHouse, will retry later");
    }
    
    UDPReceiver udp_receiver;
    udp_receiver.set_config(config.dynamics);
    if (!udp_receiver.start(config.services.udp_port, sensor_queue)) {
        LOG_ERROR("Failed to start UDP receiver on port %u", config.services.udp_port);
        return 1;
    }
    LOG_INFO("UDP Receiver started on port %u", config.services.udp_port);
    
    SeismicSimulator seismic_simulator;
    seismic_simulator.set_dynamics_config(config.dynamics);
    seismic_simulator.set_soil_type(ConfigLoader::soil_type_from_string(config.soil.default_type));
    seismic_simulator.start(sensor_queue, simulation_queue);
    LOG_INFO("Seismic Simulator started");
    
    g_simulation = nullptr;
    
    SensitivityAnalyzer sensitivity_analyzer;
    sensitivity_analyzer.set_dynamics_config(config.dynamics);
    sensitivity_analyzer.set_sensitivity_config(config.sensitivity);
    sensitivity_analyzer.set_soil_type(ConfigLoader::soil_type_from_string(config.soil.default_type));
    sensitivity_analyzer.start(sensitivity_queue);
    LOG_INFO("Sensitivity Analyzer started");
    
    g_sensitivity = nullptr;
    
    AlarmMQTT alarm_mqtt;
    alarm_mqtt.set_alert_config(config.alert);
    alarm_mqtt.start(sensor_queue, simulation_queue, sensitivity_queue,
                     config.services.mqtt_host, config.services.mqtt_port,
                     "seismograph_backend_" + config.services.device_id);
    LOG_INFO("Alarm MQTT started");
    
    ColumnSimulation http_sim;
    http_sim.set_column_params(ConfigLoader::to_column_params(config.dynamics));
    http_sim.set_soil_type(ConfigLoader::soil_type_from_string(config.soil.default_type));
    g_simulation = &http_sim;
    
    SensitivityAnalysis http_sensitivity;
    http_sensitivity.set_column_simulation(&http_sim);
    g_sensitivity = &http_sensitivity;
    
    HttpServer http_server;
    if (!http_server.start(config.services.http_port)) {
        LOG_ERROR("Failed to start HTTP server on port %u", config.services.http_port);
        udp_receiver.stop();
        return 1;
    }
    LOG_INFO("HTTP Server started on port %u", config.services.http_port);
    
    LOG_INFO("\n=== 服务已启动 ===");
    LOG_INFO("UDP监听端口: %u", config.services.udp_port);
    LOG_INFO("HTTP API端口: %u", config.services.http_port);
    LOG_INFO("设备ID: %s", config.services.device_id.c_str());
    LOG_INFO("土壤类型: %s", config.soil.default_type.c_str());
    LOG_INFO("按 Ctrl+C 停止服务");
    
    auto sensitivity_thread = std::thread([&]() {
        int interval_seconds = config.sensitivity.analysis_interval_minutes * 60;
        while (g_running) {
            for (int i = 0; i < interval_seconds && g_running; ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            
            if (!g_running) break;
            
            LOG_INFO("Running automatic sensitivity analysis...");
            
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
                
                PrometheusRegistry::instance().set_gauge(
                    "seismograph_detection_probability", 
                    msg.result.detection_probability);
                PrometheusRegistry::instance().set_gauge(
                    "seismograph_false_alarm_rate", 
                    msg.result.false_alarm_rate);
                PrometheusRegistry::instance().set_gauge(
                    "seismograph_sensitivity_score",
                    msg.result.detection_probability * (1.0 - msg.result.false_alarm_rate));
            }
            
            auto range = sensitivity_analyzer.calculate_detection_range(
                config.sensitivity.detection_threshold,
                config.sensitivity.false_alarm_limit,
                soil
            );
            
            LOG_INFO("Detection range: magnitude %.1f-%.1f, distance %.0f-%.0f km",
                     range.min_magnitude, range.max_magnitude,
                     range.min_distance, range.max_distance);
        }
    });
    
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        SensorMessage msg;
        size_t written = 0;
        while (sensor_queue->pop(msg)) {
            if (clickhouse->is_connected()) {
                clickhouse->insert_sensor_data(msg.data);
                ++written;
            }
        }
        if (written > 0) {
            PrometheusRegistry::instance().increment_counter(
                "seismograph_clickhouse_writes_total", written);
        }
        
        PrometheusRegistry::instance().set_gauge(
            "seismograph_queue_sensor_depth", sensor_queue->size());
        PrometheusRegistry::instance().set_gauge(
            "seismograph_queue_simulation_depth", simulation_queue->size());
        PrometheusRegistry::instance().set_gauge(
            "seismograph_queue_sensitivity_depth", sensitivity_queue->size());
    }
    
    LOG_INFO("Shutting down...");
    
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
    
    LOG_INFO("Service stopped. Goodbye!");
    Logger::flush();
    
    return 0;
}
