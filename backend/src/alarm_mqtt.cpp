#include "alarm_mqtt.h"
#include <iostream>
#include <chrono>
#include <sstream>
#include <cmath>
#include <algorithm>

namespace seismograph {

AlarmMQTT::AlarmMQTT()
    : running_(false)
    , sensor_queue_(nullptr)
    , sim_queue_(nullptr)
    , sensitivity_queue_(nullptr)
    , mqtt_client_(nullptr)
    , alert_topic_prefix_("seismograph/alerts/") {
    
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.total_alerts = 0;
    stats_.false_trigger_alerts = 0;
    stats_.sensitivity_alerts = 0;
    stats_.last_alert_time = 0;
    stats_.mqtt_messages_sent = 0;
    stats_.current_sensitivity_score = 1.0;
    stats_.false_trigger_rate = 0.0;
}

AlarmMQTT::~AlarmMQTT() {
    stop();
}

bool AlarmMQTT::start(
    std::shared_ptr<MessageQueue<SensorMessage>> sensor_queue,
    std::shared_ptr<MessageQueue<SimulationMessage>> sim_queue,
    std::shared_ptr<MessageQueue<SensitivityMessage>> sensitivity_queue,
    const std::string& mqtt_host, int mqtt_port,
    const std::string& client_id) {
    
    if (running_) return false;
    
    sensor_queue_ = sensor_queue;
    sim_queue_ = sim_queue;
    sensitivity_queue_ = sensitivity_queue;
    
    mqtt_client_ = std::make_unique<MqttClient>();
    if (mqtt_client_->connect(mqtt_host, mqtt_port, client_id)) {
        std::cout << "[AlarmMQTT] MQTT connected to " << mqtt_host << ":" << mqtt_port << std::endl;
        mqtt_client_->start_async_publisher();
    } else {
        std::cout << "[AlarmMQTT] Warning: MQTT connection failed, alerts stored locally only" << std::endl;
    }
    
    running_ = true;
    worker_thread_ = std::thread(&AlarmMQTT::worker_loop, this);
    
    std::cout << "[AlarmMQTT] Started" << std::endl;
    return true;
}

void AlarmMQTT::stop() {
    running_ = false;
    
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    
    if (mqtt_client_) {
        if (mqtt_client_->is_connected()) {
            mqtt_client_->stop_async_publisher();
            mqtt_client_->disconnect();
        }
        mqtt_client_.reset();
    }
    
    auto s = get_stats();
    std::cout << "[AlarmMQTT] Stopped. Total alerts: " << s.total_alerts 
              << ", MQTT sent: " << s.mqtt_messages_sent << std::endl;
}

bool AlarmMQTT::is_running() const {
    return running_;
}

void AlarmMQTT::set_alert_config(const AlertConfig& config) {
    alert_config_ = config;
}

void AlarmMQTT::publish_alert(const Alert& alert) {
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.total_alerts++;
        stats_.last_alert_time = alert.timestamp;
        
        if (alert.alert_type == "FALSE_TRIGGER") {
            stats_.false_trigger_alerts++;
        } else if (alert.alert_type == "SENSITIVITY_DECREASE") {
            stats_.sensitivity_alerts++;
        }
    }
    
    if (mqtt_client_ && mqtt_client_->is_connected()) {
        std::string topic = alert_topic_prefix_ + alert.device_id + "/" + alert.alert_type;
        
        std::stringstream ss;
        ss << "{";
        ss << "\"timestamp\":" << alert.timestamp << ",";
        ss << "\"device_id\":\"" << alert.device_id << "\",";
        ss << "\"alert_type\":\"" << alert.alert_type << "\",";
        ss << "\"alert_level\":\"" << alert.alert_level << "\",";
        ss << "\"message\":\"" << alert.message << "\",";
        ss << "\"details\":\"" << alert.details << "\"";
        ss << "}";
        
        if (mqtt_client_->publish(topic, ss.str(), 1)) {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.mqtt_messages_sent++;
        }
    }
    
    std::cout << "[ALERT][" << alert.alert_level << "] " << alert.alert_type 
              << ": " << alert.message 
              << " (device: " << alert.device_id << ")" << std::endl;
}

AlarmMQTT::Stats AlarmMQTT::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void AlarmMQTT::worker_loop() {
    SensorMessage sensor_msg;
    SimulationMessage sim_msg;
    SensitivityMessage sens_msg;
    
    std::unordered_map<std::string, SimulationMessage> pending_sim;
    
    while (running_) {
        bool did_work = false;
        
        if (sensor_queue_ && sensor_queue_->pop(sensor_msg)) {
            did_work = true;
            
            std::string key = sensor_msg.data.device_id;
            
            bool has_sim = false;
            SimulationResult sim_result;
            
            if (sim_queue_ && sim_queue_->pop(sim_msg)) {
                has_sim = true;
                sim_result = sim_msg.result;
            } else {
                SeismicWaveParams params;
                params.magnitude = sensor_msg.data.magnitude;
                params.epicenter_distance = sensor_msg.data.epicenter_distance;
                params.duration = 10.0;
                ColumnSimulation sim;
                sim_result = sim.simulate(params, 0.001);
                has_sim = true;
            }
            
            {
                std::lock_guard<std::mutex> lock(history_mutex_);
                auto& history = trigger_history_[sensor_msg.data.device_id];
                
                TriggerHistory entry;
                entry.timestamp = sensor_msg.data.timestamp;
                entry.is_triggered = sensor_msg.data.is_triggered;
                entry.magnitude = sensor_msg.data.magnitude;
                entry.expected_trigger = (sensor_msg.data.magnitude >= alert_config_.detection_magnitude_threshold) ? 1.0 : 0.0;
                
                history.push_back(entry);
                
                uint64_t window_start = sensor_msg.data.timestamp 
                    - static_cast<uint64_t>(alert_config_.history_window_seconds) * 1000;
                while (!history.empty() && history.front().timestamp < window_start) {
                    history.pop_front();
                }
            }
            
            if (has_sim && check_false_trigger(sensor_msg.data, sim_result)) {
                std::stringstream details;
                details << "Magnitude: " << sensor_msg.data.magnitude
                        << ", Direction: " << sensor_msg.data.trigger_direction
                        << ", Threshold: " << alert_config_.detection_magnitude_threshold;
                
                Alert alert = create_alert(
                    "FALSE_TRIGGER",
                    "WARNING",
                    sensor_msg.data.device_id,
                    "地动仪误触发检测",
                    details.str()
                );
                
                publish_alert(alert);
            }
            
            double score = calculate_sensitivity_score(sensor_msg.data.device_id);
            double far = calculate_false_trigger_rate(sensor_msg.data.device_id);
            
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.current_sensitivity_score = score;
                stats_.false_trigger_rate = far;
            }
            
            if (score < alert_config_.baseline_sensitivity * (1.0 - alert_config_.sensitivity_decrease_threshold)) {
                std::stringstream details;
                details << "Sensitivity: " << score
                        << ", Baseline: " << alert_config_.baseline_sensitivity
                        << ", Decrease threshold: " << alert_config_.sensitivity_decrease_threshold;
                
                Alert alert = create_alert(
                    "SENSITIVITY_DECREASE",
                    "WARNING",
                    sensor_msg.data.device_id,
                    "地动仪灵敏度下降",
                    details.str()
                );
                
                publish_alert(alert);
            }
        }
        
        if (sensitivity_queue_ && sensitivity_queue_->pop(sens_msg)) {
            did_work = true;
            
            if (check_sensitivity_decrease(sens_msg.result)) {
                std::stringstream details;
                details << "Detection probability: " << sens_msg.result.detection_probability
                        << ", Test magnitude: " << sens_msg.result.test_magnitude
                        << ", Test distance: " << sens_msg.result.test_distance
                        << ", Soil type: " << static_cast<int>(sens_msg.result.soil_type);
                
                Alert alert = create_alert(
                    "SENSITIVITY_DECREASE",
                    "WARNING",
                    "device_001",
                    "灵敏度分析检测到性能下降",
                    details.str()
                );
                
                publish_alert(alert);
            }
        }
        
        if (!did_work) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

bool AlarmMQTT::check_false_trigger(const SensorData& data, const SimulationResult& sim_result) {
    if (!data.is_triggered) return false;
    
    double expected_magnitude = data.magnitude;
    double detection_threshold = alert_config_.detection_magnitude_threshold;
    
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

bool AlarmMQTT::check_sensitivity_decrease(const SensitivityResult& result) {
    double baseline = alert_config_.baseline_sensitivity;
    return result.detection_probability < baseline * (1.0 - alert_config_.sensitivity_decrease_threshold);
}

double AlarmMQTT::calculate_sensitivity_score(const std::string& device_id) {
    std::lock_guard<std::mutex> lock(history_mutex_);
    
    auto it = trigger_history_.find(device_id);
    if (it == trigger_history_.end() || it->second.empty()) {
        return 1.0;
    }
    
    const auto& history = it->second;
    size_t correct = 0;
    size_t total_expected = 0;
    
    for (const auto& entry : history) {
        if (entry.magnitude >= alert_config_.detection_magnitude_threshold) {
            total_expected++;
            if (entry.is_triggered) {
                correct++;
            }
        }
    }
    
    if (total_expected == 0) return 1.0;
    
    return static_cast<double>(correct) / total_expected;
}

double AlarmMQTT::calculate_false_trigger_rate(const std::string& device_id) {
    std::lock_guard<std::mutex> lock(history_mutex_);
    
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
            if (entry.magnitude < alert_config_.detection_magnitude_threshold) {
                false_triggers++;
            }
        }
    }
    
    if (total_triggers == 0) return 0.0;
    
    return static_cast<double>(false_triggers) / total_triggers;
}

Alert AlarmMQTT::create_alert(const std::string& type, const std::string& level,
                               const std::string& device_id, const std::string& message,
                               const std::string& details) {
    Alert alert;
    alert.timestamp = current_timestamp_ms();
    alert.device_id = device_id;
    alert.alert_type = type;
    alert.alert_level = level;
    alert.message = message;
    alert.details = details;
    return alert;
}

}
