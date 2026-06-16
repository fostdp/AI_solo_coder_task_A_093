#include "alert_manager.h"
#include <sstream>
#include <cmath>
#include <algorithm>

namespace seismograph {

AlertManager::AlertManager()
    : false_trigger_threshold_(0.1)
    , sensitivity_decrease_threshold_(0.2)
    , false_trigger_window_seconds_(3600)
    , monitoring_(false) {
    std::lock_guard<std::mutex> lock(mutex_);
    stats_.total_alerts = 0;
    stats_.false_trigger_alerts = 0;
    stats_.sensitivity_alerts = 0;
    stats_.last_alert_time = 0;
    stats_.current_sensitivity_score = 1.0;
    stats_.false_trigger_rate = 0.0;
}

AlertManager::~AlertManager() {
    stop_monitoring();
}

void AlertManager::set_mqtt_client(std::shared_ptr<MqttClient> mqtt_client) {
    mqtt_client_ = mqtt_client;
}

void AlertManager::set_clickhouse_client(std::shared_ptr<ClickHouseClient> clickhouse_client) {
    clickhouse_client_ = clickhouse_client;
}

Alert AlertManager::create_alert(AlertType type, AlertLevel level,
                                 const std::string& device_id,
                                 const std::string& message,
                                 const std::string& details) {
    Alert alert;
    alert.timestamp = current_timestamp_ms();
    alert.device_id = device_id;
    alert.alert_type = alert_type_to_string(type);
    alert.alert_level = alert_level_to_string(level);
    alert.message = message;
    alert.details = details;
    return alert;
}

void AlertManager::publish_alert(const Alert& alert) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stats_.total_alerts++;
        stats_.last_alert_time = alert.timestamp;
        
        if (alert.alert_type == "FALSE_TRIGGER") {
            stats_.false_trigger_alerts++;
        } else if (alert.alert_type == "SENSITIVITY_DECREASE") {
            stats_.sensitivity_alerts++;
        }
    }
    
    if (clickhouse_client_) {
        clickhouse_client_->insert_alert(alert);
    }
    
    if (mqtt_client_ && mqtt_client_->is_connected()) {
        mqtt_client_->publish_alert(alert);
    }
    
    std::cout << "[" << alert.alert_level << "] " << alert.alert_type << ": " 
              << alert.message << " (device: " << alert.device_id << ")" << std::endl;
}

bool AlertManager::check_false_trigger(const SensorData& data, const SimulationResult& sim_result) {
    if (!data.is_triggered) return false;
    
    double expected_magnitude = data.magnitude;
    double detection_threshold = 3.0;
    
    if (expected_magnitude < detection_threshold) {
        return true;
    }
    
    if (sim_result.is_triggered && expected_magnitude >= detection_threshold) {
        int expected_direction = static_cast<int>(std::round(
            std::atan2(data.seismic_accel_y, data.seismic_accel_x) / (M_PI / 4.0)
        )) % 8;
        if (expected_direction < 0) expected_direction += 8;
        
        int actual_direction = data.trigger_direction;
        int diff = std::abs(actual_direction - expected_direction);
        diff = std::min(diff, 8 - diff);
        
        if (diff > 2) {
            return true;
        }
    }
    
    return false;
}

bool AlertManager::check_sensitivity_decrease(const SensitivityResult& result) {
    double baseline = 0.8;
    return result.detection_probability < baseline * (1.0 - sensitivity_decrease_threshold_);
}

void AlertManager::process_sensor_data(const SensorData& data, const SimulationResult& sim_result) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& history = trigger_history_[data.device_id];
        
        TriggerHistory entry;
        entry.timestamp = data.timestamp;
        entry.is_triggered = data.is_triggered;
        entry.magnitude = data.magnitude;
        entry.expected_trigger = (data.magnitude >= 3.0) ? 1.0 : 0.0;
        
        history.push_back(entry);
        
        uint64_t window_start = data.timestamp - false_trigger_window_seconds_ * 1000;
        while (!history.empty() && history.front().timestamp < window_start) {
            history.pop_front();
        }
    }
    
    if (check_false_trigger(data, sim_result)) {
        std::stringstream details;
        details << "Magnitude: " << data.magnitude 
                << ", Direction: " << data.trigger_direction
                << ", Expected magnitude threshold: 3.0";
        
        Alert alert = create_alert(
            AlertType::FALSE_TRIGGER,
            AlertLevel::WARNING,
            data.device_id,
            "地动仪误触发检测",
            details.str()
        );
        
        publish_alert(alert);
    }
    
    double current_sensitivity = calculate_current_sensitivity_score(data.device_id);
    double false_trigger_rate = calculate_false_trigger_rate(data.device_id);
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stats_.current_sensitivity_score = current_sensitivity;
        stats_.false_trigger_rate = false_trigger_rate;
    }
    
    if (current_sensitivity < 0.8) {
        std::stringstream details;
        details << "Current sensitivity: " << current_sensitivity
                << ", Baseline: 0.8, Threshold: " << (1.0 - sensitivity_decrease_threshold_);
        
        Alert alert = create_alert(
            AlertType::SENSITIVITY_DECREASE,
            AlertLevel::WARNING,
            data.device_id,
            "地动仪灵敏度下降",
            details.str()
        );
        
        publish_alert(alert);
    }
}

void AlertManager::process_sensitivity_result(const SensitivityResult& result) {
    if (check_sensitivity_decrease(result)) {
        std::stringstream details;
        details << "Detection probability: " << result.detection_probability
                << ", Test magnitude: " << result.test_magnitude
                << ", Test distance: " << result.test_distance;
        
        Alert alert = create_alert(
            AlertType::SENSITIVITY_DECREASE,
            AlertLevel::WARNING,
            "device_001",
            "灵敏度分析检测到性能下降",
            details.str()
        );
        
        publish_alert(alert);
    }
}

double AlertManager::calculate_current_sensitivity_score(const std::string& device_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = trigger_history_.find(device_id);
    if (it == trigger_history_.end() || it->second.empty()) {
        return 1.0;
    }
    
    const auto& history = it->second;
    
    size_t correct_detections = 0;
    size_t total_expected = 0;
    
    for (const auto& entry : history) {
        if (entry.magnitude >= 3.0) {
            total_expected++;
            if (entry.is_triggered) {
                correct_detections++;
            }
        }
    }
    
    if (total_expected == 0) {
        return 1.0;
    }
    
    return static_cast<double>(correct_detections) / total_expected;
}

double AlertManager::calculate_false_trigger_rate(const std::string& device_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = trigger_history_.find(device_id);
    if (it == trigger_history_.end() || it->second.empty()) {
        return 0.0;
    }
    
    const auto& history = it->second;
    
    size_t false_triggers = 0;
    size_t total_triggers = 0;
    
    for (const auto& entry : history) {
        if (entry.is_triggered) {
            total_triggers++;
            if (entry.magnitude < 3.0) {
                false_triggers++;
            }
        }
    }
    
    if (total_triggers == 0) {
        return 0.0;
    }
    
    return static_cast<double>(false_triggers) / total_triggers;
}

void AlertManager::start_monitoring() {
    if (monitoring_) return;
    
    monitoring_ = true;
    monitoring_thread_ = std::thread(&AlertManager::monitoring_loop, this);
    std::cout << "Alert monitoring started" << std::endl;
}

void AlertManager::stop_monitoring() {
    monitoring_ = false;
    if (monitoring_thread_.joinable()) {
        monitoring_thread_.join();
    }
    std::cout << "Alert monitoring stopped" << std::endl;
}

void AlertManager::monitoring_loop() {
    while (monitoring_) {
        std::this_thread::sleep_for(std::chrono::seconds(60));
        
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [device_id, history] : trigger_history_) {
            uint64_t now = current_timestamp_ms();
            uint64_t window_start = now - false_trigger_window_seconds_ * 1000;
            
            while (!history.empty() && history.front().timestamp < window_start) {
                history.pop_front();
            }
        }
    }
}

void AlertManager::update_baseline_sensitivity(const std::string& device_id, double sensitivity) {
    baseline_sensitivity_[device_id] = sensitivity;
}

std::string AlertManager::alert_type_to_string(AlertType type) {
    switch (type) {
        case AlertType::FALSE_TRIGGER: return "FALSE_TRIGGER";
        case AlertType::SENSITIVITY_DECREASE: return "SENSITIVITY_DECREASE";
        case AlertType::SENSOR_FAILURE: return "SENSOR_FAILURE";
        case AlertType::CALIBRATION_NEEDED: return "CALIBRATION_NEEDED";
        default: return "UNKNOWN";
    }
}

std::string AlertManager::alert_level_to_string(AlertLevel level) {
    switch (level) {
        case AlertLevel::INFO: return "INFO";
        case AlertLevel::WARNING: return "WARNING";
        case AlertLevel::ERROR: return "ERROR";
        case AlertLevel::CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

AlertManager::AlertStats AlertManager::get_stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

}
