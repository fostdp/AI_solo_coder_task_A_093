#pragma once
#include "common.h"
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <functional>
#include <mutex>
#include <queue>
#include <WinSock2.h>
#include <WS2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

namespace seismograph {

using DataCallback = std::function<void(const SensorData&)>;

class UDPServer {
public:
    UDPServer();
    ~UDPServer();
    
    bool start(int port, DataCallback callback);
    void stop();
    bool is_running() const;
    
    size_t get_received_count() const { return received_count_; }
    
    void set_parse_raw_data(bool parse) { parse_raw_data_ = parse; }

private:
    void receive_loop();
    bool parse_sensor_data(const char* buffer, size_t len, SensorData& data);
    bool parse_json_data(const std::string& json_str, SensorData& data);
    bool parse_binary_data(const char* buffer, size_t len, SensorData& data);
    
    SOCKET socket_;
    int port_;
    std::atomic<bool> running_;
    std::atomic<size_t> received_count_;
    std::thread receive_thread_;
    DataCallback data_callback_;
    bool parse_raw_data_;
    
    mutable std::mutex queue_mutex_;
    std::queue<SensorData> data_queue_;
    static constexpr size_t BUFFER_SIZE = 4096;
};

class UDPClient {
public:
    UDPClient();
    ~UDPClient();
    
    bool connect(const std::string& host, int port);
    bool send(const SensorData& data);
    bool send_json(const SensorData& data);
    bool send_binary(const SensorData& data);
    void close();

private:
    SOCKET socket_;
    sockaddr_in server_addr_;
    bool connected_;
    
    std::string serialize_to_json(const SensorData& data);
    std::vector<char> serialize_to_binary(const SensorData& data);
};

}
