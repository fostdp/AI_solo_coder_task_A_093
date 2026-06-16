#pragma once
#include "common.h"
#include "mqtt_client.h"
#include "clickhouse_client.h"
#include <memory>
#include <mutex>
#include <deque>
#include <atomic>
#include <thread>
#include <chrono>

namespace seismograph {

class AlertManager {
public:
    AlertManager();
    ~AlertManager();
    
    void set_mqtt_client(std::shared_ptr<MqttClient> mqtt_client);
    void set_clickhouse_client(std::shared_ptr<ClickHouseClient> clickhouse_client);
    
    void set_false_trigger_threshold(double threshold) { false_trigger_threshold_ = threshold; }
    void set_sensitivity_decrease_threshold(double threshold) { sensitivity_decrease_threshold_ = threshold; }
    void set_false_trigger_window_seconds(uint32_t seconds) { false_trigger_window_seconds_ = seconds; }
    
    void process_sensor_data(const SensorData& data, const SimulationResult& sim_result);
    void process_sensitivity_result(const SensitivityResult& result);
    
    Alert create_alert(AlertType type, AlertLevel level, 
                       const std::string& device_id,
                       const std::string& message,
                       const std::string& details = "");
    
    void publish_alert(const Alert& alert);
    
    struct AlertStats {
        uint64_t total_alerts;
        uint64_t false_trigger_alerts;
        uint64_t sensitivity_alerts;
        uint64_t last_alert_time;
        double current_sensitivity_score;
        double false_trigger_rate;
    };
    
    AlertStats get_stats() const;
    
    void start_monitoring();
    void stop_monitoring();
    
    double calculate_current_sensitivity_score(const std::string& device_id);
    double calculate_false_trigger_rate(const std::string& device_id);

private:
    std::shared_ptr<MqttClient> mqtt_client_;
    std::shared_ptr<ClickHouseClient> clickhouse_client_;
    
    double false_trigger_threshold_;
    double sensitivity_decrease_threshold_;
    uint32_t false_trigger_window_seconds_;
    
    mutable std::mutex mutex_;
    AlertStats stats_;
    
    std::atomic<bool> monitoring_;
    std::thread monitoring_thread_;
    
    struct TriggerHistory {
        uint64_t timestamp;
        bool is_triggered;
        double magnitude;
        double expected_trigger;
    };
    
    std::map<std::string, std::deque<TriggerHistory>> trigger_history_;
    
    std::map<std::string, double> baseline_sensitivity_;
    
    bool check_false_trigger(const SensorData& data, const SimulationResult& sim_result);
    bool check_sensitivity_decrease(const SensitivityResult& result);
    
    void monitoring_loop();
    
    void update_baseline_sensitivity(const std::string& device_id, double sensitivity);
    
    std::string alert_type_to_string(AlertType type);
    std::string alert_level_to_string(AlertLevel level);
};

}
