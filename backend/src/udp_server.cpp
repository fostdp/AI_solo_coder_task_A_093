#include "udp_server.h"
#include <sstream>
#include <cstring>
#include <iostream>

namespace seismograph {

UDPServer::UDPServer()
    : socket_(INVALID_SOCKET)
    , port_(0)
    , running_(false)
    , received_count_(0)
    , parse_raw_data_(false) {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
}

UDPServer::~UDPServer() {
    stop();
    WSACleanup();
}

bool UDPServer::start(int port, DataCallback callback) {
    if (running_) return false;
    
    port_ = port;
    data_callback_ = callback;
    
    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == INVALID_SOCKET) {
        std::cerr << "Failed to create UDP socket" << std::endl;
        return false;
    }
    
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port_);
    
    if (bind(socket_, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "Failed to bind UDP socket to port " << port_ << std::endl;
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }
    
    u_long mode = 1;
    ioctlsocket(socket_, FIONBIO, &mode);
    
    running_ = true;
    receive_thread_ = std::thread(&UDPServer::receive_loop, this);
    
    std::cout << "UDP Server started on port " << port_ << std::endl;
    return true;
}

void UDPServer::stop() {
    running_ = false;
    
    if (receive_thread_.joinable()) {
        receive_thread_.join();
    }
    
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
    
    std::cout << "UDP Server stopped. Received " << received_count_ << " packets" << std::endl;
}

bool UDPServer::is_running() const {
    return running_;
}

void UDPServer::receive_loop() {
    char buffer[BUFFER_SIZE];
    sockaddr_in client_addr{};
    int client_addr_len = sizeof(client_addr);
    
    while (running_) {
        memset(buffer, 0, BUFFER_SIZE);
        
        int bytes_received = recvfrom(socket_, buffer, BUFFER_SIZE - 1, 0,
                                     reinterpret_cast<sockaddr*>(&client_addr), &client_addr_len);
        
        if (bytes_received > 0) {
            SensorData data;
            if (parse_sensor_data(buffer, bytes_received, data)) {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                data_queue_.push(data);
                received_count_++;
                
                if (data_callback_) {
                    data_callback_(data);
                }
            }
        } else if (bytes_received == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error != WSAEWOULDBLOCK) {
                std::cerr << "UDP receive error: " << error << std::endl;
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

bool UDPServer::parse_sensor_data(const char* buffer, size_t len, SensorData& data) {
    std::string data_str(buffer, len);
    
    if (data_str.find('{') != std::string::npos && data_str.find('}') != std::string::npos) {
        return parse_json_data(data_str, data);
    }
    
    if (parse_raw_data_ && len >= 100) {
        return parse_binary_data(buffer, len, data);
    }
    
    std::stringstream ss(data_str);
    std::string token;
    std::vector<std::string> tokens;
    
    while (std::getline(ss, token, ',')) {
        tokens.push_back(token);
    }
    
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
            return true;
        } catch (...) {
            return false;
        }
    }
    
    return false;
}

bool UDPServer::parse_json_data(const std::string& json_str, SensorData& data) {
    data.timestamp = current_timestamp_ms();
    data.device_id = "device_001";
    
    size_t pos = 0;
    auto extract_value = [&](const std::string& key) -> std::string {
        size_t key_pos = json_str.find("\"" + key + "\"");
        if (key_pos == std::string::npos) return "";
        
        size_t colon_pos = json_str.find(":", key_pos);
        if (colon_pos == std::string::npos) return "";
        
        size_t value_start = colon_pos + 1;
        while (value_start < json_str.size() && json_str[value_start] == ' ') {
            value_start++;
        }
        
        if (json_str[value_start] == '"') {
            size_t value_end = json_str.find("\"", value_start + 1);
            if (value_end == std::string::npos) return "";
            return json_str.substr(value_start + 1, value_end - value_start - 1);
        } else {
            size_t value_end = json_str.find_first_of(",}", value_start);
            if (value_end == std::string::npos) value_end = json_str.size();
            return json_str.substr(value_start, value_end - value_start);
        }
    };
    
    try {
        std::string ts = extract_value("timestamp");
        if (!ts.empty()) data.timestamp = std::stoull(ts);
        
        std::string dev = extract_value("device_id");
        if (!dev.empty()) data.device_id = dev;
        
        data.column_disp_x = std::stod(extract_value("column_displacement_x"));
        data.column_disp_y = std::stod(extract_value("column_displacement_y"));
        data.column_disp_z = std::stod(extract_value("column_displacement_z"));
        data.column_angle_x = std::stod(extract_value("column_angle_x"));
        data.column_angle_y = std::stod(extract_value("column_angle_y"));
        data.seismic_accel_x = std::stod(extract_value("seismic_accel_x"));
        data.seismic_accel_y = std::stod(extract_value("seismic_accel_y"));
        data.seismic_accel_z = std::stod(extract_value("seismic_accel_z"));
        
        std::string triggers = extract_value("dragon_triggers");
        if (!triggers.empty()) {
            size_t start = triggers.find('[');
            size_t end = triggers.find(']');
            if (start != std::string::npos && end != std::string::npos) {
                std::string arr = triggers.substr(start + 1, end - start - 1);
                std::stringstream ss(arr);
                std::string val;
                int idx = 0;
                while (std::getline(ss, val, ',') && idx < 8) {
                    data.dragon_triggers[idx++] = static_cast<uint8_t>(std::stoi(val));
                }
            }
        }
        
        data.magnitude = std::stod(extract_value("magnitude"));
        data.epicenter_distance = std::stod(extract_value("epicenter_distance"));
        data.is_triggered = static_cast<uint8_t>(std::stoi(extract_value("is_triggered")));
        data.trigger_direction = std::stoi(extract_value("trigger_direction"));
        
        return true;
    } catch (...) {
        return false;
    }
}

bool UDPServer::parse_binary_data(const char* buffer, size_t len, SensorData& data) {
    if (len < 100) return false;
    
    size_t offset = 0;
    
    std::memcpy(&data.timestamp, buffer + offset, sizeof(uint64_t));
    offset += sizeof(uint64_t);
    
    char device_id[64] = {0};
    std::memcpy(device_id, buffer + offset, 32);
    data.device_id = device_id;
    offset += 32;
    
    std::memcpy(&data.column_disp_x, buffer + offset, sizeof(double));
    offset += sizeof(double);
    std::memcpy(&data.column_disp_y, buffer + offset, sizeof(double));
    offset += sizeof(double);
    std::memcpy(&data.column_disp_z, buffer + offset, sizeof(double));
    offset += sizeof(double);
    
    std::memcpy(&data.column_angle_x, buffer + offset, sizeof(double));
    offset += sizeof(double);
    std::memcpy(&data.column_angle_y, buffer + offset, sizeof(double));
    offset += sizeof(double);
    
    std::memcpy(&data.seismic_accel_x, buffer + offset, sizeof(double));
    offset += sizeof(double);
    std::memcpy(&data.seismic_accel_y, buffer + offset, sizeof(double));
    offset += sizeof(double);
    std::memcpy(&data.seismic_accel_z, buffer + offset, sizeof(double));
    offset += sizeof(double);
    
    std::memcpy(data.dragon_triggers.data(), buffer + offset, 8);
    offset += 8;
    
    std::memcpy(&data.magnitude, buffer + offset, sizeof(double));
    offset += sizeof(double);
    std::memcpy(&data.epicenter_distance, buffer + offset, sizeof(double));
    offset += sizeof(double);
    
    std::memcpy(&data.is_triggered, buffer + offset, sizeof(uint8_t));
    offset += sizeof(uint8_t);
    std::memcpy(&data.trigger_direction, buffer + offset, sizeof(int32_t));
    offset += sizeof(int32_t);
    
    return true;
}

UDPClient::UDPClient()
    : socket_(INVALID_SOCKET)
    , connected_(false) {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
}

UDPClient::~UDPClient() {
    close();
    WSACleanup();
}

bool UDPClient::connect(const std::string& host, int port) {
    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == INVALID_SOCKET) {
        std::cerr << "Failed to create UDP client socket" << std::endl;
        return false;
    }
    
    server_addr_.sin_family = AF_INET;
    server_addr_.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &server_addr_.sin_addr);
    
    connected_ = true;
    return true;
}

bool UDPClient::send(const SensorData& data) {
    return send_json(data);
}

bool UDPClient::send_json(const SensorData& data) {
    if (!connected_) return false;
    
    std::string json = serialize_to_json(data);
    
    int bytes_sent = sendto(socket_, json.c_str(), static_cast<int>(json.size()), 0,
                           reinterpret_cast<sockaddr*>(&server_addr_), sizeof(server_addr_));
    
    return bytes_sent != SOCKET_ERROR;
}

bool UDPClient::send_binary(const SensorData& data) {
    if (!connected_) return false;
    
    std::vector<char> binary = serialize_to_binary(data);
    
    int bytes_sent = sendto(socket_, binary.data(), static_cast<int>(binary.size()), 0,
                           reinterpret_cast<sockaddr*>(&server_addr_), sizeof(server_addr_));
    
    return bytes_sent != SOCKET_ERROR;
}

void UDPClient::close() {
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
    connected_ = false;
}

std::string UDPClient::serialize_to_json(const SensorData& data) {
    std::stringstream ss;
    ss << "{";
    ss << "\"timestamp\":" << data.timestamp << ",";
    ss << "\"device_id\":\"" << data.device_id << "\",";
    ss << "\"column_displacement_x\":" << data.column_disp_x << ",";
    ss << "\"column_displacement_y\":" << data.column_disp_y << ",";
    ss << "\"column_displacement_z\":" << data.column_disp_z << ",";
    ss << "\"column_angle_x\":" << data.column_angle_x << ",";
    ss << "\"column_angle_y\":" << data.column_angle_y << ",";
    ss << "\"seismic_accel_x\":" << data.seismic_accel_x << ",";
    ss << "\"seismic_accel_y\":" << data.seismic_accel_y << ",";
    ss << "\"seismic_accel_z\":" << data.seismic_accel_z << ",";
    ss << "\"dragon_triggers\":[";
    for (int i = 0; i < 8; ++i) {
        ss << static_cast<int>(data.dragon_triggers[i]);
        if (i < 7) ss << ",";
    }
    ss << "],";
    ss << "\"magnitude\":" << data.magnitude << ",";
    ss << "\"epicenter_distance\":" << data.epicenter_distance << ",";
    ss << "\"is_triggered\":" << static_cast<int>(data.is_triggered) << ",";
    ss << "\"trigger_direction\":" << data.trigger_direction;
    ss << "}";
    return ss.str();
}

std::vector<char> UDPClient::serialize_to_binary(const SensorData& data) {
    std::vector<char> buffer;
    buffer.reserve(128);
    
    buffer.insert(buffer.end(), 
                  reinterpret_cast<const char*>(&data.timestamp),
                  reinterpret_cast<const char*>(&data.timestamp) + sizeof(uint64_t));
    
    char device_id[32] = {0};
    std::strncpy(device_id, data.device_id.c_str(), 31);
    buffer.insert(buffer.end(), device_id, device_id + 32);
    
    auto append_double = [&](double val) {
        buffer.insert(buffer.end(),
                      reinterpret_cast<const char*>(&val),
                      reinterpret_cast<const char*>(&val) + sizeof(double));
    };
    
    append_double(data.column_disp_x);
    append_double(data.column_disp_y);
    append_double(data.column_disp_z);
    append_double(data.column_angle_x);
    append_double(data.column_angle_y);
    append_double(data.seismic_accel_x);
    append_double(data.seismic_accel_y);
    append_double(data.seismic_accel_z);
    
    buffer.insert(buffer.end(),
                  data.dragon_triggers.begin(),
                  data.dragon_triggers.end());
    
    append_double(data.magnitude);
    append_double(data.epicenter_distance);
    
    buffer.push_back(static_cast<char>(data.is_triggered));
    buffer.insert(buffer.end(),
                  reinterpret_cast<const char*>(&data.trigger_direction),
                  reinterpret_cast<const char*>(&data.trigger_direction) + sizeof(int32_t));
    
    return buffer;
}

}
