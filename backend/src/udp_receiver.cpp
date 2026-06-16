#include "udp_receiver.h"
#include "udp_server.h"
#include <iostream>
#include <chrono>

namespace seismograph {

UDPReceiver::UDPReceiver()
    : port_(0)
    , running_(false)
    , socket_(INVALID_SOCKET)
    , validate_range_(true)
    , output_queue_(nullptr) {
    
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.total_packets = 0;
    stats_.valid_packets = 0;
    stats_.invalid_packets = 0;
    stats_.queue_full_dropped = 0;
    stats_.last_packet_time = 0;
}

UDPReceiver::~UDPReceiver() {
    stop();
    WSACleanup();
}

bool UDPReceiver::start(int port, std::shared_ptr<MessageQueue<SensorMessage>> output_queue) {
    if (running_) return false;
    
    port_ = port;
    output_queue_ = output_queue;
    
    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == INVALID_SOCKET) {
        std::cerr << "[UDPReceiver] Failed to create socket" << std::endl;
        return false;
    }
    
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port_);
    
    if (bind(socket_, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "[UDPReceiver] Failed to bind port " << port_ << std::endl;
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }
    
    u_long mode = 1;
    ioctlsocket(socket_, FIONBIO, &mode);
    
    running_ = true;
    receive_thread_ = std::thread(&UDPReceiver::receive_loop, this);
    
    std::cout << "[UDPReceiver] Started on port " << port_ << std::endl;
    return true;
}

void UDPReceiver::stop() {
    running_ = false;
    
    if (receive_thread_.joinable()) {
        receive_thread_.join();
    }
    
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
    
    auto s = get_stats();
    std::cout << "[UDPReceiver] Stopped. Received " << s.total_packets 
              << " packets (" << s.valid_packets << " valid, " 
              << s.invalid_packets << " invalid)" << std::endl;
}

bool UDPReceiver::is_running() const {
    return running_;
}

UDPReceiver::Stats UDPReceiver::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

bool UDPReceiver::validate_sensor_data(const SensorData& data) {
    if (data.device_id.empty()) return false;
    if (data.timestamp == 0) return false;
    
    if (validate_range_) {
        const double max_disp = 2.0;
        if (std::abs(data.column_disp_x) > max_disp) return false;
        if (std::abs(data.column_disp_y) > max_disp) return false;
        if (std::abs(data.column_disp_z) > max_disp) return false;
        
        const double max_angle = 1.0;
        if (std::abs(data.column_angle_x) > max_angle) return false;
        if (std::abs(data.column_angle_y) > max_angle) return false;
        
        const double max_accel = 100.0;
        if (std::abs(data.seismic_accel_x) > max_accel) return false;
        if (std::abs(data.seismic_accel_y) > max_accel) return false;
        if (std::abs(data.seismic_accel_z) > max_accel) return false;
        
        if (data.magnitude < 0.0 || data.magnitude > 12.0) return false;
        if (data.epicenter_distance < 0.0 || data.epicenter_distance > 10000.0) return false;
        
        if (data.trigger_direction < -1 || data.trigger_direction > 7) return false;
    }
    
    return true;
}

void UDPReceiver::receive_loop() {
    const size_t BUFFER_SIZE = 2048;
    char buffer[BUFFER_SIZE];
    sockaddr_in client_addr{};
    int client_addr_len = sizeof(client_addr);
    
    while (running_) {
        memset(buffer, 0, BUFFER_SIZE);
        
        int bytes_received = recvfrom(socket_, buffer, BUFFER_SIZE - 1, 0,
                                     reinterpret_cast<sockaddr*>(&client_addr), &client_addr_len);
        
        if (bytes_received > 0) {
            SensorData data{};
            bool parse_ok = false;
            
            std::string data_str(buffer, bytes_received);
            if (data_str.find('{') != std::string::npos) {
                parse_ok = [&]() -> bool {
                    data.timestamp = current_timestamp_ms();
                    data.device_id = "device_001";
                    
                    auto extract = [&](const std::string& key) -> std::string {
                        size_t kp = data_str.find("\"" + key + "\"");
                        if (kp == std::string::npos) return "";
                        size_t cp = data_str.find(':', kp);
                        if (cp == std::string::npos) return "";
                        size_t vs = cp + 1;
                        while (vs < data_str.size() && (data_str[vs] == ' ' || data_str[vs] == '\t')) vs++;
                        if (vs >= data_str.size()) return "";
                        if (data_str[vs] == '"') {
                            vs++;
                            size_t ve = data_str.find('"', vs);
                            return ve == std::string::npos ? "" : data_str.substr(vs, ve - vs);
                        } else {
                            size_t ve = data_str.find_first_of(",}", vs);
                            if (ve == std::string::npos) ve = data_str.size();
                            return data_str.substr(vs, ve - vs);
                        }
                    };
                    
                    try {
                        std::string ts = extract("timestamp");
                        if (!ts.empty()) data.timestamp = std::stoull(ts);
                        std::string dev = extract("device_id");
                        if (!dev.empty()) data.device_id = dev;
                        data.column_disp_x = std::stod(extract("column_displacement_x"));
                        data.column_disp_y = std::stod(extract("column_displacement_y"));
                        data.column_disp_z = std::stod(extract("column_displacement_z"));
                        data.column_angle_x = std::stod(extract("column_angle_x"));
                        data.column_angle_y = std::stod(extract("column_angle_y"));
                        data.seismic_accel_x = std::stod(extract("seismic_accel_x"));
                        data.seismic_accel_y = std::stod(extract("seismic_accel_y"));
                        data.seismic_accel_z = std::stod(extract("seismic_accel_z"));
                        data.magnitude = std::stod(extract("magnitude"));
                        data.epicenter_distance = std::stod(extract("epicenter_distance"));
                        data.is_triggered = static_cast<uint8_t>(std::stoi(extract("is_triggered")));
                        data.trigger_direction = std::stoi(extract("trigger_direction"));
                        return true;
                    } catch (...) {
                        return false;
                    }
                }();
            } else {
                std::stringstream ss(data_str);
                std::string token;
                std::vector<std::string> tokens;
                while (std::getline(ss, token, ',')) tokens.push_back(token);
                if (tokens.size() >= 15) {
                    try {
                        data.timestamp = std::stoull(tokens[0]);
                        data.device_id = tokens[1];
                        data.column_disp_x = std::stod(tokens[2]);
                        data.column_disp_y = std::stod(tokens[3]);
                        data.column_disp_z = std::stod(tokens[4]);
                        data.column_angle_x = std::stod(tokens[5]);
                        data.column_angle_y = std::stod(tokens[6]);
                        data.seismic_accel_x = std::stod(tokens[7]);
                        data.seismic_accel_y = std::stod(tokens[8]);
                        data.seismic_accel_z = std::stod(tokens[9]);
                        for (int i = 0; i < 8; ++i) {
                            data.dragon_triggers[i] = static_cast<uint8_t>(std::stoi(tokens[10 + i]));
                        }
                        data.magnitude = std::stod(tokens[18]);
                        data.epicenter_distance = std::stod(tokens[19]);
                        data.is_triggered = static_cast<uint8_t>(std::stoi(tokens[20]));
                        data.trigger_direction = std::stoi(tokens[21]);
                        parse_ok = true;
                    } catch (...) {
                        parse_ok = false;
                    }
                }
            }
            
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.total_packets++;
                stats_.last_packet_time = current_timestamp_ms();
            }
            
            if (parse_ok && validate_sensor_data(data)) {
                SensorMessage msg;
                msg.data = data;
                msg.received_at = current_timestamp_ms();
                
                if (output_queue_ && output_queue_->push(msg)) {
                    std::lock_guard<std::mutex> lock(stats_mutex_);
                    stats_.valid_packets++;
                } else {
                    std::lock_guard<std::mutex> lock(stats_mutex_);
                    stats_.queue_full_dropped++;
                    stats_.invalid_packets++;
                }
            } else {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.invalid_packets++;
            }
        } else if (bytes_received == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error != WSAEWOULDBLOCK) {
                std::cerr << "[UDPReceiver] Receive error: " << error << std::endl;
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

}
