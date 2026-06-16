#include "clickhouse_client.h"
#include <sstream>
#include <iostream>
#include <WinSock2.h>
#include <WS2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

namespace seismograph {

struct ClickHouseClient::Impl {
    SOCKET socket;
    std::string host;
    int port;
    std::string database;
    std::string user;
    std::string password;
    bool connected;
    Stats stats;
    
    Impl() : socket(INVALID_SOCKET), port(8123), connected(false) {
        stats.total_inserts = 0;
        stats.failed_inserts = 0;
        stats.last_insert_time = 0;
        stats.current_queue_size = 0;
    }
};

ClickHouseClient::ClickHouseClient()
    : impl_(std::make_unique<Impl>())
    , batch_size_(1000)
    , auto_flush_interval_ms_(5000)
    , async_running_(false) {
}

ClickHouseClient::~ClickHouseClient() {
    stop_async_writer();
    flush();
    disconnect();
}

bool ClickHouseClient::connect(const std::string& host, int port,
                              const std::string& database,
                              const std::string& user,
                              const std::string& password) {
    impl_->host = host;
    impl_->port = port;
    impl_->database = database;
    impl_->user = user;
    impl_->password = password;
    
    impl_->socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (impl_->socket == INVALID_SOCKET) {
        std::cerr << "Failed to create ClickHouse socket" << std::endl;
        return false;
    }
    
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr);
    
    if (::connect(impl_->socket, reinterpret_cast<sockaddr*>(&server_addr), 
                  sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "Failed to connect to ClickHouse at " << host << ":" << port << std::endl;
        closesocket(impl_->socket);
        impl_->socket = INVALID_SOCKET;
        return false;
    }
    
    u_long mode = 1;
    ioctlsocket(impl_->socket, FIONBIO, &mode);
    
    impl_->connected = true;
    
    std::string query = "USE " + database;
    execute_query(query);
    
    std::cout << "Connected to ClickHouse at " << host << ":" << port << ", database: " << database << std::endl;
    return true;
}

void ClickHouseClient::disconnect() {
    if (impl_->socket != INVALID_SOCKET) {
        closesocket(impl_->socket);
        impl_->socket = INVALID_SOCKET;
    }
    impl_->connected = false;
}

bool ClickHouseClient::is_connected() const {
    return impl_->connected;
}

bool ClickHouseClient::execute_query(const std::string& query) {
    if (!impl_->connected) return false;
    
    std::string request = "POST / HTTP/1.1\r\n";
    request += "Host: " + impl_->host + ":" + std::to_string(impl_->port) + "\r\n";
    request += "Content-Type: text/plain\r\n";
    request += "Content-Length: " + std::to_string(query.size()) + "\r\n";
    request += "Connection: keep-alive\r\n\r\n";
    request += query;
    
    int bytes_sent = send(impl_->socket, request.c_str(), static_cast<int>(request.size()), 0);
    if (bytes_sent == SOCKET_ERROR) {
        impl_->connected = false;
        return false;
    }
    
    char buffer[4096];
    std::string response;
    fd_set read_fds;
    timeval timeout{0, 100000};
    
    FD_ZERO(&read_fds);
    FD_SET(impl_->socket, &read_fds);
    
    int select_result = select(0, &read_fds, nullptr, nullptr, &timeout);
    if (select_result > 0 && FD_ISSET(impl_->socket, &read_fds)) {
        int bytes_received;
        do {
            bytes_received = recv(impl_->socket, buffer, sizeof(buffer) - 1, 0);
            if (bytes_received > 0) {
                buffer[bytes_received] = '\0';
                response += buffer;
            }
        } while (bytes_received > 0);
    }
    
    return response.find("200 OK") != std::string::npos || response.empty();
}

std::string ClickHouseClient::format_insert_query(const SensorData& data) {
    std::stringstream ss;
    ss << "INSERT INTO seismograph.sensor_data VALUES (";
    ss << "toDateTime64(" << data.timestamp << ", 3),";
    ss << "'" << data.device_id << "',";
    ss << data.column_disp_x << ",";
    ss << data.column_disp_y << ",";
    ss << data.column_disp_z << ",";
    ss << data.column_angle_x << ",";
    ss << data.column_angle_y << ",";
    ss << data.seismic_accel_x << ",";
    ss << data.seismic_accel_y << ",";
    ss << data.seismic_accel_z << ",";
    ss << "[";
    for (int i = 0; i < 8; ++i) {
        ss << static_cast<int>(data.dragon_triggers[i]);
        if (i < 7) ss << ",";
    }
    ss << "],";
    ss << data.magnitude << ",";
    ss << data.epicenter_distance << ",";
    ss << static_cast<int>(data.is_triggered) << ",";
    ss << data.trigger_direction;
    ss << ")";
    return ss.str();
}

std::string ClickHouseClient::format_insert_query(const SensitivityResult& result, 
                                                  const std::string& device_id) {
    std::stringstream ss;
    ss << "INSERT INTO seismograph.sensitivity_analysis VALUES (";
    ss << "toDateTime64(" << current_timestamp_ms() << ", 3),";
    ss << "'" << device_id << "',";
    ss << result.test_magnitude << ",";
    ss << result.test_distance << ",";
    ss << result.detection_probability << ",";
    ss << result.false_alarm_rate << ",";
    ss << result.response_time_ms << ",";
    ss << result.trigger_direction << ",";
    ss << result.column_stiffness << ",";
    ss << result.damping_coefficient;
    ss << ")";
    return ss.str();
}

std::string ClickHouseClient::format_insert_query(const Alert& alert) {
    std::stringstream ss;
    ss << "INSERT INTO seismograph.alerts VALUES (";
    ss << "toDateTime64(" << alert.timestamp << ", 3),";
    ss << "'" << alert.device_id << "',";
    ss << "'" << alert.alert_type << "',";
    ss << "'" << alert.alert_level << "',";
    ss << "'" << alert.message << "',";
    ss << "'" << alert.details << "'";
    ss << ")";
    return ss.str();
}

bool ClickHouseClient::insert_sensor_data(const SensorData& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    sensor_data_queue_.push_back(data);
    
    if (sensor_data_queue_.size() >= batch_size_) {
        return insert_sensor_data_batch(sensor_data_queue_);
    }
    
    return true;
}

bool ClickHouseClient::insert_sensor_data_batch(const std::vector<SensorData>& data) {
    if (data.empty()) return true;
    
    std::string query;
    for (size_t i = 0; i < data.size(); ++i) {
        query += format_insert_query(data[i]);
        query += ";";
    }
    
    bool success = execute_query(query);
    
    std::lock_guard<std::mutex> lock(mutex_);
    impl_->stats.total_inserts += data.size();
    if (!success) {
        impl_->stats.failed_inserts += data.size();
    }
    impl_->stats.last_insert_time = current_timestamp_ms();
    
    return success;
}

bool ClickHouseClient::insert_sensitivity_result(const SensitivityResult& result, 
                                                 const std::string& device_id) {
    std::string query = format_insert_query(result, device_id);
    return execute_query(query);
}

bool ClickHouseClient::insert_sensitivity_batch(
    const std::vector<std::pair<SensitivityResult, std::string>>& batch) {
    
    std::string query;
    for (const auto& [result, device_id] : batch) {
        query += format_insert_query(result, device_id);
        query += ";";
    }
    return execute_query(query);
}

bool ClickHouseClient::insert_alert(const Alert& alert) {
    std::string query = format_insert_query(alert);
    return execute_query(query);
}

bool ClickHouseClient::insert_simulation_run(const std::string& device_id,
                                             const std::string& simulation_id,
                                             uint64_t start_time,
                                             uint64_t end_time,
                                             const std::string& parameters,
                                             const std::string& result) {
    std::stringstream ss;
    ss << "INSERT INTO seismograph.simulation_runs VALUES (";
    ss << "toDateTime64(" << current_timestamp_ms() << ", 3),";
    ss << "'" << device_id << "',";
    ss << "'" << simulation_id << "',";
    ss << "toDateTime64(" << start_time << ", 3),";
    ss << "toDateTime64(" << end_time << ", 3),";
    ss << "'" << parameters << "',";
    ss << "'" << result << "'";
    ss << ")";
    return execute_query(ss.str());
}

std::vector<SensorData> ClickHouseClient::query_sensor_data(
    const std::string& device_id,
    uint64_t start_time,
    uint64_t end_time,
    size_t limit) {
    
    std::vector<SensorData> results;
    
    std::stringstream ss;
    ss << "SELECT timestamp, device_id, column_displacement_x, column_displacement_y, ";
    ss << "column_displacement_z, column_angle_x, column_angle_y, ";
    ss << "seismic_accel_x, seismic_accel_y, seismic_accel_z, ";
    ss << "dragon_triggers, magnitude, epicenter_distance, is_triggered, trigger_direction ";
    ss << "FROM seismograph.sensor_data ";
    ss << "WHERE device_id = '" << device_id << "' ";
    ss << "AND timestamp >= toDateTime64(" << start_time << ", 3) ";
    ss << "AND timestamp <= toDateTime64(" << end_time << ", 3) ";
    ss << "ORDER BY timestamp DESC LIMIT " << limit;
    ss << " FORMAT TabSeparated";
    
    std::string query = ss.str();
    
    if (!impl_->connected) return results;
    
    std::string request = "POST /?query=" + query + " HTTP/1.1\r\n";
    request += "Host: " + impl_->host + ":" + std::to_string(impl_->port) + "\r\n";
    request += "Connection: keep-alive\r\n\r\n";
    
    int bytes_sent = send(impl_->socket, request.c_str(), static_cast<int>(request.size()), 0);
    if (bytes_sent == SOCKET_ERROR) {
        impl_->connected = false;
        return results;
    }
    
    char buffer[4096];
    std::string response;
    fd_set read_fds;
    timeval timeout{0, 500000};
    
    FD_ZERO(&read_fds);
    FD_SET(impl_->socket, &read_fds);
    
    int select_result = select(0, &read_fds, nullptr, nullptr, &timeout);
    if (select_result > 0 && FD_ISSET(impl_->socket, &read_fds)) {
        int bytes_received;
        do {
            bytes_received = recv(impl_->socket, buffer, sizeof(buffer) - 1, 0);
            if (bytes_received > 0) {
                buffer[bytes_received] = '\0';
                response += buffer;
            }
        } while (bytes_received > 0);
    }
    
    size_t body_start = response.find("\r\n\r\n");
    if (body_start != std::string::npos) {
        std::string body = response.substr(body_start + 4);
        std::stringstream ss_body(body);
        std::string line;
        
        while (std::getline(ss_body, line) && results.size() < limit) {
            if (line.empty()) continue;
            
            std::stringstream line_ss(line);
            std::string token;
            std::vector<std::string> tokens;
            
            while (std::getline(line_ss, token, '\t')) {
                tokens.push_back(token);
            }
            
            if (tokens.size() >= 15) {
                try {
                    SensorData data{};
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
                    
                    std::string triggers = tokens[10];
                    size_t start = triggers.find('[');
                    size_t end = triggers.find(']');
                    if (start != std::string::npos && end != std::string::npos) {
                        std::string arr = triggers.substr(start + 1, end - start - 1);
                        std::stringstream arr_ss(arr);
                        std::string val;
                        int idx = 0;
                        while (std::getline(arr_ss, val, ',') && idx < 8) {
                            data.dragon_triggers[idx++] = static_cast<uint8_t>(std::stoi(val));
                        }
                    }
                    
                    data.magnitude = std::stod(tokens[11]);
                    data.epicenter_distance = std::stod(tokens[12]);
                    data.is_triggered = static_cast<uint8_t>(std::stoi(tokens[13]));
                    data.trigger_direction = std::stoi(tokens[14]);
                    
                    results.push_back(data);
                } catch (...) {
                }
            }
        }
    }
    
    return results;
}

std::vector<SensitivityResult> ClickHouseClient::query_sensitivity_analysis(
    const std::string& device_id,
    uint64_t start_time,
    uint64_t end_time,
    size_t limit) {
    
    std::vector<SensitivityResult> results;
    
    std::stringstream ss;
    ss << "SELECT test_magnitude, test_distance, detection_probability, ";
    ss << "false_alarm_rate, response_time_ms, trigger_direction, ";
    ss << "column_stiffness, damping_coefficient ";
    ss << "FROM seismograph.sensitivity_analysis ";
    ss << "WHERE device_id = '" << device_id << "' ";
    ss << "AND timestamp >= toDateTime64(" << start_time << ", 3) ";
    ss << "AND timestamp <= toDateTime64(" << end_time << ", 3) ";
    ss << "ORDER BY timestamp DESC LIMIT " << limit;
    ss << " FORMAT TabSeparated";
    
    std::string query = ss.str();
    
    if (!impl_->connected) return results;
    
    std::string request = "POST /?query=" + query + " HTTP/1.1\r\n";
    request += "Host: " + impl_->host + ":" + std::to_string(impl_->port) + "\r\n";
    request += "Connection: keep-alive\r\n\r\n";
    
    int bytes_sent = send(impl_->socket, request.c_str(), static_cast<int>(request.size()), 0);
    if (bytes_sent == SOCKET_ERROR) {
        impl_->connected = false;
        return results;
    }
    
    char buffer[4096];
    std::string response;
    fd_set read_fds;
    timeval timeout{0, 500000};
    
    FD_ZERO(&read_fds);
    FD_SET(impl_->socket, &read_fds);
    
    int select_result = select(0, &read_fds, nullptr, nullptr, &timeout);
    if (select_result > 0 && FD_ISSET(impl_->socket, &read_fds)) {
        int bytes_received;
        do {
            bytes_received = recv(impl_->socket, buffer, sizeof(buffer) - 1, 0);
            if (bytes_received > 0) {
                buffer[bytes_received] = '\0';
                response += buffer;
            }
        } while (bytes_received > 0);
    }
    
    size_t body_start = response.find("\r\n\r\n");
    if (body_start != std::string::npos) {
        std::string body = response.substr(body_start + 4);
        std::stringstream ss_body(body);
        std::string line;
        
        while (std::getline(ss_body, line) && results.size() < limit) {
            if (line.empty()) continue;
            
            std::stringstream line_ss(line);
            std::string token;
            std::vector<std::string> tokens;
            
            while (std::getline(line_ss, token, '\t')) {
                tokens.push_back(token);
            }
            
            if (tokens.size() >= 8) {
                try {
                    SensitivityResult data{};
                    data.test_magnitude = std::stod(tokens[0]);
                    data.test_distance = std::stod(tokens[1]);
                    data.detection_probability = std::stod(tokens[2]);
                    data.false_alarm_rate = std::stod(tokens[3]);
                    data.response_time_ms = std::stod(tokens[4]);
                    data.trigger_direction = std::stoi(tokens[5]);
                    data.column_stiffness = std::stod(tokens[6]);
                    data.damping_coefficient = std::stod(tokens[7]);
                    results.push_back(data);
                } catch (...) {
                }
            }
        }
    }
    
    return results;
}

std::vector<Alert> ClickHouseClient::query_alerts(
    const std::string& device_id,
    uint64_t start_time,
    uint64_t end_time,
    const std::string& alert_type,
    size_t limit) {
    
    std::vector<Alert> results;
    
    std::stringstream ss;
    ss << "SELECT timestamp, device_id, alert_type, alert_level, message, details ";
    ss << "FROM seismograph.alerts ";
    ss << "WHERE device_id = '" << device_id << "' ";
    ss << "AND timestamp >= toDateTime64(" << start_time << ", 3) ";
    ss << "AND timestamp <= toDateTime64(" << end_time << ", 3) ";
    if (!alert_type.empty()) {
        ss << "AND alert_type = '" << alert_type << "' ";
    }
    ss << "ORDER BY timestamp DESC LIMIT " << limit;
    ss << " FORMAT TabSeparated";
    
    std::string query = ss.str();
    
    if (!impl_->connected) return results;
    
    std::string request = "POST /?query=" + query + " HTTP/1.1\r\n";
    request += "Host: " + impl_->host + ":" + std::to_string(impl_->port) + "\r\n";
    request += "Connection: keep-alive\r\n\r\n";
    
    int bytes_sent = send(impl_->socket, request.c_str(), static_cast<int>(request.size()), 0);
    if (bytes_sent == SOCKET_ERROR) {
        impl_->connected = false;
        return results;
    }
    
    char buffer[4096];
    std::string response;
    fd_set read_fds;
    timeval timeout{0, 500000};
    
    FD_ZERO(&read_fds);
    FD_SET(impl_->socket, &read_fds);
    
    int select_result = select(0, &read_fds, nullptr, nullptr, &timeout);
    if (select_result > 0 && FD_ISSET(impl_->socket, &read_fds)) {
        int bytes_received;
        do {
            bytes_received = recv(impl_->socket, buffer, sizeof(buffer) - 1, 0);
            if (bytes_received > 0) {
                buffer[bytes_received] = '\0';
                response += buffer;
            }
        } while (bytes_received > 0);
    }
    
    size_t body_start = response.find("\r\n\r\n");
    if (body_start != std::string::npos) {
        std::string body = response.substr(body_start + 4);
        std::stringstream ss_body(body);
        std::string line;
        
        while (std::getline(ss_body, line) && results.size() < limit) {
            if (line.empty()) continue;
            
            std::stringstream line_ss(line);
            std::string token;
            std::vector<std::string> tokens;
            
            while (std::getline(line_ss, token, '\t')) {
                tokens.push_back(token);
            }
            
            if (tokens.size() >= 6) {
                try {
                    Alert data{};
                    data.timestamp = std::stoull(tokens[0]);
                    data.device_id = tokens[1];
                    data.alert_type = tokens[2];
                    data.alert_level = tokens[3];
                    data.message = tokens[4];
                    data.details = tokens[5];
                    results.push_back(data);
                } catch (...) {
                }
            }
        }
    }
    
    return results;
}

ClickHouseClient::Stats ClickHouseClient::get_stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    impl_->stats.current_queue_size = sensor_data_queue_.size();
    return impl_->stats;
}

void ClickHouseClient::start_async_writer() {
    if (async_running_) return;
    
    async_running_ = true;
    async_thread_ = std::thread(&ClickHouseClient::async_writer_loop, this);
}

void ClickHouseClient::stop_async_writer() {
    async_running_ = false;
    if (async_thread_.joinable()) {
        async_thread_.join();
    }
}

void ClickHouseClient::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!sensor_data_queue_.empty()) {
        insert_sensor_data_batch(sensor_data_queue_);
        sensor_data_queue_.clear();
    }
}

void ClickHouseClient::async_writer_loop() {
    while (async_running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(auto_flush_interval_ms_));
        
        std::lock_guard<std::mutex> lock(mutex_);
        if (sensor_data_queue_.size() >= batch_size_) {
            insert_sensor_data_batch(sensor_data_queue_);
            sensor_data_queue_.clear();
        }
    }
}

}
