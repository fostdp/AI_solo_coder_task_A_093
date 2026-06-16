#pragma once
#include "common.h"
#include <string>
#include <functional>
#include <memory>
#include <atomic>
#include <mutex>
#include <queue>
#include <thread>

namespace seismograph {

using MqttMessageCallback = std::function<void(const std::string& topic, const std::string& payload)>;

class MqttClient {
public:
    MqttClient();
    ~MqttClient();
    
    bool connect(const std::string& broker_address,
                 int port,
                 const std::string& client_id,
                 const std::string& username = "",
                 const std::string& password = "");
    
    void disconnect();
    bool is_connected() const;
    
    bool publish(const std::string& topic,
                 const std::string& payload,
                 int qos = 1,
                 bool retained = false);
    
    bool publish_alert(const Alert& alert, int qos = 1);
    
    bool subscribe(const std::string& topic, 
                   MqttMessageCallback callback,
                   int qos = 1);
    
    bool unsubscribe(const std::string& topic);
    
    void set_will_message(const std::string& topic,
                          const std::string& payload,
                          int qos = 1,
                          bool retained = false);
    
    struct Stats {
        uint64_t messages_published;
        uint64_t messages_received;
        uint64_t connection_attempts;
        uint64_t successful_connections;
        uint64_t last_message_time;
    };
    
    Stats get_stats() const;
    
    void start_async_publisher();
    void stop_async_publisher();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    
    std::atomic<bool> connected_;
    std::atomic<bool> async_running_;
    std::thread async_thread_;
    
    mutable std::mutex stats_mutex_;
    Stats stats_;
    
    std::mutex queue_mutex_;
    std::queue<std::pair<std::string, std::string>> publish_queue_;
    
    std::string alert_topic_prefix_;
    
    void async_publisher_loop();
    
    std::string alert_to_json(const Alert& alert);
};

}
