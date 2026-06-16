#pragma once

#include "common.h"
#include "message_queue.h"
#include "config_loader.h"
#include <string>
#include <thread>
#include <atomic>
#include <mutex>

namespace seismograph {

class UDPReceiver {
public:
    UDPReceiver();
    ~UDPReceiver();

    bool start(int port, std::shared_ptr<MessageQueue<SensorMessage>> output_queue);
    void stop();
    bool is_running() const;

    void set_config(const DynamicsConfig& dynamics_config) {
        std::ignore = dynamics_config; }
    void set_validate_range(bool enable) { validate_range_ = enable; }

    struct Stats {
        uint64_t total_packets;
        uint64_t valid_packets;
        uint64_t invalid_packets;
        uint64_t queue_full_dropped;
        uint64_t last_packet_time;
    };

    Stats get_stats() const;

private:
    void receive_loop();
    bool validate_sensor_data(const SensorData& data);

    int port_;
    std::atomic<bool> running_;
    std::thread receive_thread_;

    SOCKET socket_;

    std::shared_ptr<MessageQueue<SensorMessage>> output_queue_;

    DynamicsConfig dynamics_config_;
    bool validate_range_;

    mutable std::mutex stats_mutex_;
    Stats stats_;
};

}
