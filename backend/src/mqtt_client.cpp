#include "mqtt_client.h"
#include <sstream>
#include <iostream>
#include <WinSock2.h>
#include <WS2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

namespace seismograph {

struct MqttClient::Impl {
    SOCKET socket;
    std::string broker_address;
    int port;
    std::string client_id;
    std::string username;
    std::string password;
    std::string will_topic;
    std::string will_payload;
    int will_qos;
    bool will_retained;
    
    Impl() : socket(INVALID_SOCKET), port(1883), will_qos(0), will_retained(false) {}
};

MqttClient::MqttClient()
    : impl_(std::make_unique<Impl>())
    , connected_(false)
    , async_running_(false)
    , alert_topic_prefix_("seismograph/alerts/") {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.messages_published = 0;
    stats_.messages_received = 0;
    stats_.connection_attempts = 0;
    stats_.successful_connections = 0;
    stats_.last_message_time = 0;
}

MqttClient::~MqttClient() {
    stop_async_publisher();
    disconnect();
    WSACleanup();
}

bool MqttClient::connect(const std::string& broker_address,
                         int port,
                         const std::string& client_id,
                         const std::string& username,
                         const std::string& password) {
    impl_->broker_address = broker_address;
    impl_->port = port;
    impl_->client_id = client_id;
    impl_->username = username;
    impl_->password = password;
    
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.connection_attempts++;
    
    impl_->socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (impl_->socket == INVALID_SOCKET) {
        std::cerr << "Failed to create MQTT socket" << std::endl;
        return false;
    }
    
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, broker_address.c_str(), &server_addr.sin_addr);
    
    if (::connect(impl_->socket, reinterpret_cast<sockaddr*>(&server_addr),
                  sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "Failed to connect to MQTT broker at " << broker_address << ":" << port << std::endl;
        closesocket(impl_->socket);
        impl_->socket = INVALID_SOCKET;
        return false;
    }
    
    u_long mode = 1;
    ioctlsocket(impl_->socket, FIONBIO, &mode);
    
    std::vector<uint8_t> connect_packet;
    
    uint8_t packet_type = 0x10;
    uint8_t remaining_length = 10 + client_id.size();
    if (!username.empty()) remaining_length += 2 + username.size();
    if (!password.empty()) remaining_length += 2 + password.size();
    
    connect_packet.push_back(packet_type);
    connect_packet.push_back(static_cast<uint8_t>(remaining_length));
    
    connect_packet.push_back(0x00);
    connect_packet.push_back(0x04);
    connect_packet.push_back('M');
    connect_packet.push_back('Q');
    connect_packet.push_back('T');
    connect_packet.push_back('T');
    connect_packet.push_back(0x04);
    
    uint8_t connect_flags = 0x02;
    if (!username.empty()) connect_flags |= 0x80;
    if (!password.empty()) connect_flags |= 0x40;
    connect_packet.push_back(connect_flags);
    
    connect_packet.push_back(0x00);
    connect_packet.push_back(0x3C);
    
    connect_packet.push_back(static_cast<uint8_t>(client_id.size() >> 8));
    connect_packet.push_back(static_cast<uint8_t>(client_id.size() & 0xFF));
    connect_packet.insert(connect_packet.end(), client_id.begin(), client_id.end());
    
    if (!username.empty()) {
        connect_packet.push_back(static_cast<uint8_t>(username.size() >> 8));
        connect_packet.push_back(static_cast<uint8_t>(username.size() & 0xFF));
        connect_packet.insert(connect_packet.end(), username.begin(), username.end());
    }
    
    if (!password.empty()) {
        connect_packet.push_back(static_cast<uint8_t>(password.size() >> 8));
        connect_packet.push_back(static_cast<uint8_t>(password.size() & 0xFF));
        connect_packet.insert(connect_packet.end(), password.begin(), password.end());
    }
    
    int bytes_sent = send(impl_->socket, reinterpret_cast<char*>(connect_packet.data()),
                          static_cast<int>(connect_packet.size()), 0);
    
    if (bytes_sent == SOCKET_ERROR) {
        closesocket(impl_->socket);
        impl_->socket = INVALID_SOCKET;
        return false;
    }
    
    char buffer[4];
    fd_set read_fds;
    timeval timeout{2, 0};
    
    FD_ZERO(&read_fds);
    FD_SET(impl_->socket, &read_fds);
    
    int select_result = select(0, &read_fds, nullptr, nullptr, &timeout);
    if (select_result > 0 && FD_ISSET(impl_->socket, &read_fds)) {
        int bytes_received = recv(impl_->socket, buffer, sizeof(buffer), 0);
        if (bytes_received >= 4 && buffer[0] == 0x20 && buffer[3] == 0x00) {
            connected_ = true;
            stats_.successful_connections++;
            std::cout << "Connected to MQTT broker at " << broker_address << ":" << port << std::endl;
            return true;
        }
    }
    
    closesocket(impl_->socket);
    impl_->socket = INVALID_SOCKET;
    return false;
}

void MqttClient::disconnect() {
    if (impl_->socket != INVALID_SOCKET && connected_) {
        uint8_t disconnect_packet[2] = {0xE0, 0x00};
        send(impl_->socket, reinterpret_cast<char*>(disconnect_packet), 2, 0);
        closesocket(impl_->socket);
        impl_->socket = INVALID_SOCKET;
    }
    connected_ = false;
}

bool MqttClient::is_connected() const {
    return connected_;
}

bool MqttClient::publish(const std::string& topic,
                         const std::string& payload,
                         int qos,
                         bool retained) {
    if (!connected_) return false;
    
    std::vector<uint8_t> packet;
    
    uint8_t packet_type = 0x30;
    if (qos == 1) packet_type |= 0x02;
    else if (qos == 2) packet_type |= 0x04;
    if (retained) packet_type |= 0x01;
    
    uint32_t remaining_length = 2 + topic.size() + payload.size();
    if (qos > 0) remaining_length += 2;
    
    packet.push_back(packet_type);
    
    uint32_t len = remaining_length;
    do {
        uint8_t byte = len & 0x7F;
        len >>= 7;
        if (len > 0) byte |= 0x80;
        packet.push_back(byte);
    } while (len > 0);
    
    packet.push_back(static_cast<uint8_t>(topic.size() >> 8));
    packet.push_back(static_cast<uint8_t>(topic.size() & 0xFF));
    packet.insert(packet.end(), topic.begin(), topic.end());
    
    if (qos > 0) {
        static uint16_t packet_id = 1;
        packet.push_back(static_cast<uint8_t>(packet_id >> 8));
        packet.push_back(static_cast<uint8_t>(packet_id & 0xFF));
        packet_id++;
    }
    
    packet.insert(packet.end(), payload.begin(), payload.end());
    
    int bytes_sent = send(impl_->socket, reinterpret_cast<char*>(packet.data()),
                          static_cast<int>(packet.size()), 0);
    
    if (bytes_sent != SOCKET_ERROR) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.messages_published++;
        stats_.last_message_time = current_timestamp_ms();
        return true;
    }
    
    connected_ = false;
    return false;
}

bool MqttClient::publish_alert(const Alert& alert, int qos) {
    std::string topic = alert_topic_prefix_ + alert.device_id + "/" + alert.alert_type;
    std::string payload = alert_to_json(alert);
    return publish(topic, payload, qos);
}

bool MqttClient::subscribe(const std::string& topic,
                           MqttMessageCallback callback,
                           int qos) {
    if (!connected_) return false;
    
    std::vector<uint8_t> packet;
    
    packet.push_back(0x82);
    
    uint32_t remaining_length = 2 + 2 + topic.size() + 1;
    uint32_t len = remaining_length;
    do {
        uint8_t byte = len & 0x7F;
        len >>= 7;
        if (len > 0) byte |= 0x80;
        packet.push_back(byte);
    } while (len > 0);
    
    static uint16_t packet_id = 1;
    packet.push_back(static_cast<uint8_t>(packet_id >> 8));
    packet.push_back(static_cast<uint8_t>(packet_id & 0xFF));
    packet_id++;
    
    packet.push_back(static_cast<uint8_t>(topic.size() >> 8));
    packet.push_back(static_cast<uint8_t>(topic.size() & 0xFF));
    packet.insert(packet.end(), topic.begin(), topic.end());
    packet.push_back(static_cast<uint8_t>(qos));
    
    int bytes_sent = send(impl_->socket, reinterpret_cast<char*>(packet.data()),
                          static_cast<int>(packet.size()), 0);
    
    return bytes_sent != SOCKET_ERROR;
}

bool MqttClient::unsubscribe(const std::string& topic) {
    if (!connected_) return false;
    
    std::vector<uint8_t> packet;
    
    packet.push_back(0xA2);
    
    uint32_t remaining_length = 2 + 2 + topic.size();
    uint32_t len = remaining_length;
    do {
        uint8_t byte = len & 0x7F;
        len >>= 7;
        if (len > 0) byte |= 0x80;
        packet.push_back(byte);
    } while (len > 0);
    
    static uint16_t packet_id = 1;
    packet.push_back(static_cast<uint8_t>(packet_id >> 8));
    packet.push_back(static_cast<uint8_t>(packet_id & 0xFF));
    packet_id++;
    
    packet.push_back(static_cast<uint8_t>(topic.size() >> 8));
    packet.push_back(static_cast<uint8_t>(topic.size() & 0xFF));
    packet.insert(packet.end(), topic.begin(), topic.end());
    
    int bytes_sent = send(impl_->socket, reinterpret_cast<char*>(packet.data()),
                          static_cast<int>(packet.size()), 0);
    
    return bytes_sent != SOCKET_ERROR;
}

void MqttClient::set_will_message(const std::string& topic,
                                  const std::string& payload,
                                  int qos,
                                  bool retained) {
    impl_->will_topic = topic;
    impl_->will_payload = payload;
    impl_->will_qos = qos;
    impl_->will_retained = retained;
}

MqttClient::Stats MqttClient::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void MqttClient::start_async_publisher() {
    if (async_running_) return;
    
    async_running_ = true;
    async_thread_ = std::thread(&MqttClient::async_publisher_loop, this);
}

void MqttClient::stop_async_publisher() {
    async_running_ = false;
    if (async_thread_.joinable()) {
        async_thread_.join();
    }
}

void MqttClient::async_publisher_loop() {
    while (async_running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        std::pair<std::string, std::string> message;
        bool has_message = false;
        
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (!publish_queue_.empty()) {
                message = publish_queue_.front();
                publish_queue_.pop();
                has_message = true;
            }
        }
        
        if (has_message) {
            publish(message.first, message.second);
        }
    }
}

std::string MqttClient::alert_to_json(const Alert& alert) {
    std::stringstream ss;
    ss << "{";
    ss << "\"timestamp\":" << alert.timestamp << ",";
    ss << "\"device_id\":\"" << alert.device_id << "\",";
    ss << "\"alert_type\":\"" << alert.alert_type << "\",";
    ss << "\"alert_level\":\"" << alert.alert_level << "\",";
    ss << "\"message\":\"" << alert.message << "\",";
    ss << "\"details\":\"" << alert.details << "\"";
    ss << "}";
    return ss.str();
}

}
