#pragma once

#include "common.h"
#include "message_queue.h"
#include "config_loader.h"
#include "mqtt_client.h"
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <deque>
#include <string>
#include <unordered_map>

namespace seismograph {

class AlarmMQTT {
public:
    AlarmMQTT();
    ~AlarmMQTT();

    bool start(std::shared_ptr<MessageQueue<SensorMessage>> sensor_queue,
               std::shared_ptr<MessageQueue<SimulationMessage>> sim_queue,
               std::shared_ptr<MessageQueue<SensitivityMessage>> sensitivity_queue,
               const std::string& mqtt_host, int mqtt_port,
               const std::string& client_id);

    void stop();
    bool is_running() const;

    void set_alert_config(const AlertConfig& config);

    void publish_alert(const Alert& alert);

    struct TriggerHistory {
        uint64_t timestamp;
        bool is_triggered;
        double magnitude;
        double expected_trigger;
    };

    struct Stats {
        uint64_t total_alerts;
        uint64_t false_trigger_alerts;
        uint64_t sensitivity_alerts;
        uint64_t last_alert_time;
        uint64_t mqtt_messages_sent;
        double current_sensitivity_score;
        double false_trigger_rate;
    };

    Stats get_stats() const;

private:
    void worker_loop();
    bool check_false_trigger(const SensorData& data, const SimulationResult& sim_result);
    bool check_sensitivity_decrease(const SensitivityResult& result);
    double calculate_sensitivity_score(const std::string& device_id);
    double calculate_false_trigger_rate(const std::string& device_id);
    Alert create_alert(const std::string& type, const std::string& level,
                       const std::string& device_id, const std::string& message,
                       const std::string& details);

    std::atomic<bool> running_;
    std::thread worker_thread_;

    std::shared_ptr<MessageQueue<SensorMessage>> sensor_queue_;
    std::shared_ptr<MessageQueue<SimulationMessage>> sim_queue_;
    std::shared_ptr<MessageQueue<SensitivityMessage>> sensitivity_queue_;

    std::unique_ptr<MqttClient> mqtt_client_;
    std::mutex mqtt_mutex_;

    AlertConfig alert_config_;

    std::unordered_map<std::string, std::deque<TriggerHistory>> trigger_history_;
    std::mutex history_mutex_;

    mutable std::mutex stats_mutex_;
    Stats stats_;

    std::string alert_topic_prefix_;
};

}
