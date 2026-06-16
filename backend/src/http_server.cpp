#include "http_server.h"
#include "column_simulation.h"
#include "sensitivity_analysis.h"
#include "clickhouse_client.h"
#include <sstream>
#include <iostream>
#include <chrono>
#include <WinSock2.h>
#include <WS2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

namespace seismograph {

extern ColumnSimulation* g_simulation;
extern SensitivityAnalysis* g_sensitivity;
extern std::shared_ptr<ClickHouseClient> g_clickhouse;

HttpServer::HttpServer()
    : port_(0)
    , running_(false)
    , cors_enabled_(true)
    , cors_origin_("*")
    , cors_methods_("GET, POST, PUT, DELETE, OPTIONS")
    , cors_headers_("Content-Type") {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.total_requests = 0;
    stats_.successful_requests = 0;
    stats_.failed_requests = 0;
    stats_.last_request_time = 0;
    stats_.average_response_time_ms = 0.0;
}

HttpServer::~HttpServer() {
    stop();
    WSACleanup();
}

bool HttpServer::start(int port) {
    if (running_) return false;
    
    port_ = port;
    
    SOCKET listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket == INVALID_SOCKET) {
        std::cerr << "Failed to create HTTP server socket" << std::endl;
        return false;
    }
    
    int opt = 1;
    setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, 
               reinterpret_cast<char*>(&opt), sizeof(opt));
    
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port_);
    
    if (bind(listen_socket, reinterpret_cast<sockaddr*>(&server_addr), 
             sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "Failed to bind HTTP server to port " << port_ << std::endl;
        closesocket(listen_socket);
        return false;
    }
    
    if (listen(listen_socket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Failed to listen on port " << port_ << std::endl;
        closesocket(listen_socket);
        return false;
    }
    
    u_long mode = 1;
    ioctlsocket(listen_socket, FIONBIO, &mode);
    
    running_ = true;
    accept_thread_ = std::thread([this, listen_socket]() {
        while (running_) {
            sockaddr_in client_addr{};
            int client_addr_len = sizeof(client_addr);
            
            SOCKET client_socket = accept(listen_socket, 
                                          reinterpret_cast<sockaddr*>(&client_addr),
                                          &client_addr_len);
            
            if (client_socket != INVALID_SOCKET) {
                std::thread(&HttpServer::handle_client, this, client_socket).detach();
            } else {
                int error = WSAGetLastError();
                if (error != WSAEWOULDBLOCK) {
                    std::cerr << "HTTP accept error: " << error << std::endl;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        closesocket(listen_socket);
    });
    
    add_route("GET", "/api/sensor-data", 
              std::bind(&HttpServer::api_get_sensor_data, this, std::placeholders::_1));
    add_route("POST", "/api/sensor-data", 
              std::bind(&HttpServer::api_post_sensor_data, this, std::placeholders::_1));
    add_route("GET", "/api/alerts", 
              std::bind(&HttpServer::api_get_alerts, this, std::placeholders::_1));
    add_route("GET", "/api/sensitivity", 
              std::bind(&HttpServer::api_get_sensitivity, this, std::placeholders::_1));
    add_route("POST", "/api/simulation", 
              std::bind(&HttpServer::api_run_simulation, this, std::placeholders::_1));
    add_route("POST", "/api/sensitivity-analysis", 
              std::bind(&HttpServer::api_run_sensitivity_analysis, this, std::placeholders::_1));
    add_route("GET", "/api/stats", 
              std::bind(&HttpServer::api_get_stats, this, std::placeholders::_1));
    
    std::cout << "HTTP Server started on port " << port_ << std::endl;
    return true;
}

void HttpServer::stop() {
    running_ = false;
    
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
    
    std::cout << "HTTP Server stopped" << std::endl;
}

bool HttpServer::is_running() const {
    return running_;
}

void HttpServer::add_route(const std::string& method, 
                           const std::string& path, 
                           HttpHandler handler) {
    routes_.push_back({method, path, handler});
}

void HttpServer::enable_cors(const std::string& allow_origin,
                             const std::string& allow_methods,
                             const std::string& allow_headers) {
    cors_enabled_ = true;
    cors_origin_ = allow_origin;
    cors_methods_ = allow_methods;
    cors_headers_ = allow_headers;
}

void HttpServer::handle_client(SOCKET client_socket) {
    char buffer[8192];
    std::string request_data;
    
    timeval timeout{5, 0};
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(client_socket, &read_fds);
    
    while (running_) {
        int select_result = select(0, &read_fds, nullptr, nullptr, &timeout);
        if (select_result <= 0) break;
        
        int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) break;
        
        buffer[bytes_received] = '\0';
        request_data += buffer;
        
        if (request_data.find("\r\n\r\n") != std::string::npos) {
            size_t content_length_pos = request_data.find("Content-Length:");
            if (content_length_pos != std::string::npos) {
                size_t end_pos = request_data.find("\r\n", content_length_pos);
                int content_length = std::stoi(request_data.substr(content_length_pos + 15, end_pos - content_length_pos - 15));
                
                size_t body_start = request_data.find("\r\n\r\n") + 4;
                if (request_data.size() - body_start < static_cast<size_t>(content_length)) {
                    continue;
                }
            }
            break;
        }
    }
    
    HttpRequest request;
    HttpResponse response;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    if (parse_request(request_data, request)) {
        response = route_request(request);
    } else {
        response = server_error_response("Invalid request");
    }
    
    std::string response_str = serialize_response(response);
    send(client_socket, response_str.c_str(), static_cast<int>(response_str.size()), 0;
    
    auto end_time = std::chrono::high_resolution_clock::now();
    double response_time = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.total_requests++;
        if (response.status_code >= 200 && response.status_code < 400) {
            stats_.successful_requests++;
        } else {
            stats_.failed_requests++;
        }
        stats_.last_request_time = current_timestamp_ms();
        stats_.average_response_time_ms = 
            (stats_.average_response_time_ms * (stats_.total_requests - 1) + response_time) / stats_.total_requests;
    }
    
    closesocket(client_socket);
}

bool HttpServer::parse_request(const std::string& raw_request, HttpRequest& request) {
    size_t line_end = raw_request.find("\r\n");
    if (line_end == std::string::npos) return false;
    
    std::string first_line = raw_request.substr(0, line_end);
    size_t method_end = first_line.find(' ');
    if (method_end == std::string::npos) return false;
    
    request.method = first_line.substr(0, method_end);
    
    size_t path_end = first_line.find(' ', method_end + 1);
    if (path_end == std::string::npos) return false;
    
    std::string full_path = first_line.substr(method_end + 1, path_end - method_end - 1);
    size_t query_pos = full_path.find('?');
    if (query_pos != std::string::npos) {
        request.path = full_path.substr(0, query_pos);
        request.query = full_path.substr(query_pos + 1);
    } else {
        request.path = full_path;
    }
    
    size_t header_start = line_end + 2;
    size_t header_end = raw_request.find("\r\n\r\n");
    if (header_end == std::string::npos) return false;
    
    std::string headers_str = raw_request.substr(header_start, header_end - header_start);
    std::stringstream header_ss(headers_str);
    std::string header_line;
    
    while (std::getline(header_ss, header_line) && !header_line.empty()) {
        if (header_line.back() == '\r') header_line.pop_back();
        size_t colon_pos = header_line.find(':');
        if (colon_pos != std::string::npos) {
            std::string key = header_line.substr(0, colon_pos);
            std::string value = header_line.substr(colon_pos + 2);
            request.headers[key] = value;
        }
    }
    
    request.body = raw_request.substr(header_end + 4);
    
    return true;
}

HttpResponse HttpServer::route_request(const HttpRequest& request) {
    if (cors_enabled_ && request.method == "OPTIONS") {
        return handle_options(request);
    }
    
    for (const auto& route : routes_) {
        std::map<std::string, std::string> params;
        if (route.method == request.method && match_route(request.path, route, params)) {
            HttpRequest req_with_params = request;
            req_with_params.params = params;
            return route.handler(req_with_params);
        }
    }
    
    return not_found_response();
}

bool HttpServer::match_route(const std::string& path, const Route& route,
                             std::map<std::string, std::string>& params) {
    if (route.path == path) return true;
    
    size_t path_pos = 0;
    size_t route_pos = 0;
    
    while (path_pos < path.size() && route_pos < route.path.size()) {
        if (route.path[route_pos] == '{') {
            size_t end_brace = route.path.find('}', route_pos);
            if (end_brace == std::string::npos) return false;
            
            std::string param_name = route.path.substr(route_pos + 1, end_brace - route_pos - 1);
            
            size_t next_slash = path.find('/', path_pos);
            if (next_slash == std::string::npos) next_slash = path.size();
            
            params[param_name] = path.substr(path_pos, next_slash - path_pos);
            path_pos = next_slash;
            route_pos = end_brace + 1;
        } else if (path[path_pos] == route.path[route_pos]) {
            path_pos++;
            route_pos++;
        } else {
            return false;
        }
    }
    
    return path_pos == path.size() && route_pos == route.path.size();
}

std::string HttpServer::serialize_response(const HttpResponse& response) {
    std::stringstream ss;
    
    std::string status_text = "OK";
    if (response.status_code == 404) status_text = "Not Found";
    else if (response.status_code == 500) status_text = "Internal Server Error";
    else if (response.status_code == 400) status_text = "Bad Request";
    
    ss << "HTTP/1.1 " << response.status_code << " " << status_text << "\r\n";
    
    if (cors_enabled_) {
        ss << "Access-Control-Allow-Origin: " << cors_origin_ << "\r\n";
        ss << "Access-Control-Allow-Methods: " << cors_methods_ << "\r\n";
        ss << "Access-Control-Allow-Headers: " << cors_headers_ << "\r\n";
    }
    
    ss << "Content-Type: " << response.content_type << "; charset=utf-8\r\n";
    ss << "Content-Length: " << response.body.size() << "\r\n";
    ss << "Connection: close\r\n";
    
    for (const auto& [key, value] : response.headers) {
        ss << key << ": " << value << "\r\n";
    }
    
    ss << "\r\n" << response.body;
    
    return ss.str();
}

HttpResponse HttpServer::handle_options(const HttpRequest&) {
    HttpResponse response;
    response.status_code = 204;
    response.headers["Access-Control-Allow-Origin"] = cors_origin_;
    response.headers["Access-Control-Allow-Methods"] = cors_methods_;
    response.headers["Access-Control-Allow-Headers"] = cors_headers_;
    return response;
}

HttpResponse HttpServer::not_found_response() {
    HttpResponse response;
    response.status_code = 404;
    response.body = "{\"error\":\"Not Found\"}";
    return response;
}

HttpResponse HttpServer::server_error_response(const std::string& error) {
    HttpResponse response;
    response.status_code = 500;
    response.body = "{\"error\":\"" + error + "\"}";
    return response;
}

HttpResponse HttpServer::api_get_sensor_data(const HttpRequest& request) {
    HttpResponse response;
    
    std::string device_id = "device_001";
    uint64_t end_time = current_timestamp_ms();
    uint64_t start_time = end_time - 3600000;
    size_t limit = 1000;
    
    size_t pos = 0;
    while (pos < request.query.size()) {
        size_t eq_pos = request.query.find('=', pos);
        if (eq_pos == std::string::npos) break;
        
        size_t amp_pos = request.query.find('&', eq_pos);
        if (amp_pos == std::string::npos) amp_pos = request.query.size();
        
        std::string key = request.query.substr(pos, eq_pos - pos);
        std::string value = request.query.substr(eq_pos + 1, amp_pos - eq_pos - 1);
        
        if (key == "device_id") device_id = value;
        else if (key == "start_time") start_time = std::stoull(value);
        else if (key == "end_time") end_time = std::stoull(value);
        else if (key == "limit") limit = std::stoull(value);
        
        pos = amp_pos + 1;
    }
    
    std::vector<SensorData> data;
    if (g_clickhouse && g_clickhouse->is_connected()) {
        data = g_clickhouse->query_sensor_data(device_id, start_time, end_time, limit);
    }
    
    std::stringstream ss;
    ss << "{\"data\":[";
    for (size_t i = 0; i < data.size(); ++i) {
        if (i > 0) ss << ",";
        ss << "{";
        ss << "\"timestamp\":" << data[i].timestamp << ",";
        ss << "\"column_displacement_x\":" << data[i].column_disp_x << ",";
        ss << "\"column_displacement_y\":" << data[i].column_disp_y << ",";
        ss << "\"column_displacement_z\":" << data[i].column_disp_z << ",";
        ss << "\"column_angle_x\":" << data[i].column_angle_x << ",";
        ss << "\"column_angle_y\":" << data[i].column_angle_y << ",";
        ss << "\"seismic_accel_x\":" << data[i].seismic_accel_x << ",";
        ss << "\"seismic_accel_y\":" << data[i].seismic_accel_y << ",";
        ss << "\"seismic_accel_z\":" << data[i].seismic_accel_z << ",";
        ss << "\"magnitude\":" << data[i].magnitude << ",";
        ss << "\"epicenter_distance\":" << data[i].epicenter_distance << ",";
        ss << "\"is_triggered\":" << static_cast<int>(data[i].is_triggered) << ",";
        ss << "\"trigger_direction\":" << data[i].trigger_direction << ",";
        ss << "\"dragon_triggers\":[";
        for (int j = 0; j < 8; ++j) {
            if (j > 0) ss << ",";
            ss << static_cast<int>(data[i].dragon_triggers[j]);
        }
        ss << "]}";
    }
    ss << "],\"count\":" << data.size() << "}";
    
    response.body = ss.str();
    return response;
}

HttpResponse HttpServer::api_post_sensor_data(const HttpRequest&) {
    HttpResponse response;
    response.status_code = 200;
    response.body = "{\"status\":\"ok\"}";
    return response;
}

HttpResponse HttpServer::api_get_alerts(const HttpRequest& request) {
    HttpResponse response;
    
    std::string device_id = "device_001";
    uint64_t end_time = current_timestamp_ms();
    uint64_t start_time = end_time - 86400000;
    std::string alert_type;
    size_t limit = 100;
    
    size_t pos = 0;
    while (pos < request.query.size()) {
        size_t eq_pos = request.query.find('=', pos);
        if (eq_pos == std::string::npos) break;
        
        size_t amp_pos = request.query.find('&', eq_pos);
        if (amp_pos == std::string::npos) amp_pos = request.query.size();
        
        std::string key = request.query.substr(pos, eq_pos - pos);
        std::string value = request.query.substr(eq_pos + 1, amp_pos - eq_pos - 1);
        
        if (key == "device_id") device_id = value;
        else if (key == "start_time") start_time = std::stoull(value);
        else if (key == "end_time") end_time = std::stoull(value);
        else if (key == "alert_type") alert_type = value;
        else if (key == "limit") limit = std::stoull(value);
        
        pos = amp_pos + 1;
    }
    
    std::vector<Alert> alerts;
    if (g_clickhouse && g_clickhouse->is_connected()) {
        alerts = g_clickhouse->query_alerts(device_id, start_time, end_time, alert_type, limit);
    }
    
    std::stringstream ss;
    ss << "{\"data\":[";
    for (size_t i = 0; i < alerts.size(); ++i) {
        if (i > 0) ss << ",";
        ss << "{";
        ss << "\"timestamp\":" << alerts[i].timestamp << ",";
        ss << "\"device_id\":\"" << alerts[i].device_id << "\",";
        ss << "\"alert_type\":\"" << alerts[i].alert_type << "\",";
        ss << "\"alert_level\":\"" << alerts[i].alert_level << "\",";
        ss << "\"message\":\"" << alerts[i].message << "\",";
        ss << "\"details\":\"" << alerts[i].details << "\"";
        ss << "}";
    }
    ss << "],\"count\":" << alerts.size() << "}";
    
    response.body = ss.str();
    return response;
}

HttpResponse HttpServer::api_get_sensitivity(const HttpRequest& request) {
    HttpResponse response;
    
    std::string device_id = "device_001";
    uint64_t end_time = current_timestamp_ms();
    uint64_t start_time = end_time - 86400000;
    size_t limit = 100;
    
    size_t pos = 0;
    while (pos < request.query.size()) {
        size_t eq_pos = request.query.find('=', pos);
        if (eq_pos == std::string::npos) break;
        
        size_t amp_pos = request.query.find('&', eq_pos);
        if (amp_pos == std::string::npos) amp_pos = request.query.size();
        
        std::string key = request.query.substr(pos, eq_pos - pos);
        std::string value = request.query.substr(eq_pos + 1, amp_pos - eq_pos - 1);
        
        if (key == "device_id") device_id = value;
        else if (key == "start_time") start_time = std::stoull(value);
        else if (key == "end_time") end_time = std::stoull(value);
        else if (key == "limit") limit = std::stoull(value);
        
        pos = amp_pos + 1;
    }
    
    std::vector<SensitivityResult> results;
    if (g_clickhouse && g_clickhouse->is_connected()) {
        results = g_clickhouse->query_sensitivity_analysis(device_id, start_time, end_time, limit);
    }
    
    std::stringstream ss;
    ss << "{\"data\":[";
    for (size_t i = 0; i < results.size(); ++i) {
        if (i > 0) ss << ",";
        ss << "{";
        ss << "\"test_magnitude\":" << results[i].test_magnitude << ",";
        ss << "\"test_distance\":" << results[i].test_distance << ",";
        ss << "\"detection_probability\":" << results[i].detection_probability << ",";
        ss << "\"false_alarm_rate\":" << results[i].false_alarm_rate << ",";
        ss << "\"response_time_ms\":" << results[i].response_time_ms << ",";
        ss << "\"trigger_direction\":" << results[i].trigger_direction << ",";
        ss << "\"column_stiffness\":" << results[i].column_stiffness << ",";
        ss << "\"damping_coefficient\":" << results[i].damping_coefficient;
        ss << "}";
    }
    ss << "],\"count\":" << results.size() << "}";
    
    response.body = ss.str();
    return response;
}

HttpResponse HttpServer::api_run_simulation(const HttpRequest& request) {
    HttpResponse response;
    
    if (!g_simulation) {
        response.status_code = 500;
        response.body = "{\"error\":\"Simulation not initialized\"}";
        return response;
    }
    
    double magnitude = 5.0;
    double distance = 50.0;
    double duration = 10.0;
    
    size_t pos = request.body.find("\"magnitude\"");
    if (pos != std::string::npos) {
        size_t colon = request.body.find(':', pos);
        size_t end = request.body.find_first_of(",}", colon);
        magnitude = std::stod(request.body.substr(colon + 1, end - colon - 1));
    }
    
    pos = request.body.find("\"distance\"");
    if (pos != std::string::npos) {
        size_t colon = request.body.find(':', pos);
        size_t end = request.body.find_first_of(",}", colon);
        distance = std::stod(request.body.substr(colon + 1, end - colon - 1));
    }
    
    pos = request.body.find("\"duration\"");
    if (pos != std::string::npos) {
        size_t colon = request.body.find(':', pos);
        size_t end = request.body.find_first_of(",}", colon);
        duration = std::stod(request.body.substr(colon + 1, end - colon - 1));
    }
    
    SeismicWaveParams params;
    params.magnitude = magnitude;
    params.epicenter_distance = distance;
    params.duration = duration;
    
    auto timeseries = g_simulation->simulate_timeseries(params, duration, 0.01);
    
    std::stringstream ss;
    ss << "{\"timeseries\":[";
    for (size_t i = 0; i < timeseries.size(); ++i) {
        if (i > 0) ss << ",";
        ss << "{";
        ss << "\"time\":" << timeseries[i].first << ",";
        ss << "\"displacement_x\":" << timeseries[i].second.displacement_x << ",";
        ss << "\"displacement_y\":" << timeseries[i].second.displacement_y << ",";
        ss << "\"displacement_z\":" << timeseries[i].second.displacement_z << ",";
        ss << "\"angle_x\":" << timeseries[i].second.angle_x << ",";
        ss << "\"angle_y\":" << timeseries[i].second.angle_y << ",";
        ss << "\"velocity_x\":" << timeseries[i].second.velocity_x << ",";
        ss << "\"velocity_y\":" << timeseries[i].second.velocity_y << ",";
        ss << "\"is_triggered\":" << timeseries[i].second.is_triggered << ",";
        ss << "\"trigger_direction\":" << timeseries[i].second.trigger_direction << ",";
        ss << "\"response_time_ms\":" << timeseries[i].second.response_time_ms << ",";
        ss << "\"dragon_triggers\":[";
        for (int j = 0; j < 8; ++j) {
            if (j > 0) ss << ",";
            ss << static_cast<int>(timeseries[i].second.dragon_triggers[j]);
        }
        ss << "]}";
    }
    ss << "],\"count\":" << timeseries.size() << "}";
    
    response.body = ss.str();
    return response;
}

HttpResponse HttpServer::api_run_sensitivity_analysis(const HttpRequest& request) {
    HttpResponse response;
    
    if (!g_sensitivity || !g_simulation) {
        response.status_code = 500;
        response.body = "{\"error\":\"Sensitivity analysis not initialized\"}";
        return response;
    }
    
    std::string analysis_type = "magnitude";
    double min_val = 2.0;
    double max_val = 8.0;
    int steps = 13;
    double fixed_param = 50.0;
    int num_trials = 20;
    
    size_t pos = request.body.find("\"type\"");
    if (pos != std::string::npos) {
        size_t start = request.body.find('"', pos + 6) + 1;
        size_t end = request.body.find('"', start);
        analysis_type = request.body.substr(start, end - start);
    }
    
    pos = request.body.find("\"min_val\"");
    if (pos != std::string::npos) {
        size_t colon = request.body.find(':', pos);
        size_t end = request.body.find_first_of(",}", colon);
        min_val = std::stod(request.body.substr(colon + 1, end - colon - 1));
    }
    
    pos = request.body.find("\"max_val\"");
    if (pos != std::string::npos) {
        size_t colon = request.body.find(':', pos);
        size_t end = request.body.find_first_of(",}", colon);
        max_val = std::stod(request.body.substr(colon + 1, end - colon - 1));
    }
    
    pos = request.body.find("\"steps\"");
    if (pos != std::string::npos) {
        size_t colon = request.body.find(':', pos);
        size_t end = request.body.find_first_of(",}", colon);
        steps = std::stoi(request.body.substr(colon + 1, end - colon - 1));
    }
    
    pos = request.body.find("\"fixed_param\"");
    if (pos != std::string::npos) {
        size_t colon = request.body.find(':', pos);
        size_t end = request.body.find_first_of(",}", colon);
        fixed_param = std::stod(request.body.substr(colon + 1, end - colon - 1));
    }
    
    pos = request.body.find("\"num_trials\"");
    if (pos != std::string::npos) {
        size_t colon = request.body.find(':', pos);
        size_t end = request.body.find_first_of(",}", colon);
        num_trials = std::stoi(request.body.substr(colon + 1, end - colon - 1));
    }
    
    g_sensitivity->set_column_simulation(g_simulation);
    
    std::vector<SensitivityResult> results;
    
    if (analysis_type == "magnitude") {
        results = g_sensitivity->analyze_magnitude_sensitivity(min_val, max_val, steps, fixed_param, num_trials);
    } else if (analysis_type == "distance") {
        results = g_sensitivity->analyze_distance_sensitivity(min_val, max_val, steps, fixed_param, num_trials);
    } else if (analysis_type == "2d") {
        auto results_2d = g_sensitivity->analyze_2d_sensitivity(min_val, max_val, steps, 10, 500, 10, num_trials / 2);
        for (const auto& row : results_2d) {
            for (const auto& r : row) {
                results.push_back(r);
            }
        }
    } else if (analysis_type == "detection_range") {
        auto range = g_sensitivity->calculate_detection_range();
        std::stringstream ss;
        ss << "{";
        ss << "\"min_magnitude\":" << range.min_magnitude << ",";
        ss << "\"max_magnitude\":" << range.max_magnitude << ",";
        ss << "\"min_distance\":" << range.min_distance << ",";
        ss << "\"max_distance\":" << range.max_distance << ",";
        ss << "\"effective_radius\":" << range.effective_radius << ",";
        ss << "\"magnitude_threshold\":" << range.magnitude_threshold;
        ss << "}";
        response.body = ss.str();
        return response;
    } else if (analysis_type == "optimize") {
        auto optimal = g_sensitivity->optimize_parameters(fixed_param, 50.0, num_trials);
        std::stringstream ss;
        ss << "{";
        ss << "\"stiffness\":" << optimal.stiffness << ",";
        ss << "\"damping\":" << optimal.damping << ",";
        ss << "\"trigger_threshold\":" << optimal.trigger_threshold << ",";
        ss << "\"performance_score\":" << optimal.performance_score;
        ss << "}";
        response.body = ss.str();
        return response;
    }
    
    std::stringstream ss;
    ss << "{\"data\":[";
    for (size_t i = 0; i < results.size(); ++i) {
        if (i > 0) ss << ",";
        ss << "{";
        ss << "\"test_magnitude\":" << results[i].test_magnitude << ",";
        ss << "\"test_distance\":" << results[i].test_distance << ",";
        ss << "\"detection_probability\":" << results[i].detection_probability << ",";
        ss << "\"false_alarm_rate\":" << results[i].false_alarm_rate << ",";
        ss << "\"response_time_ms\":" << results[i].response_time_ms << ",";
        ss << "\"trigger_direction\":" << results[i].trigger_direction << ",";
        ss << "\"column_stiffness\":" << results[i].column_stiffness << ",";
        ss << "\"damping_coefficient\":" << results[i].damping_coefficient;
        ss << "}";
    }
    ss << "],\"count\":" << results.size() << "}";
    
    response.body = ss.str();
    return response;
}

HttpResponse HttpServer::api_get_stats(const HttpRequest&) {
    HttpResponse response;
    
    auto http_stats = get_stats();
    
    std::stringstream ss;
    ss << "{";
    ss << "\"http\":{";
    ss << "\"total_requests\":" << http_stats.total_requests << ",";
    ss << "\"successful_requests\":" << http_stats.successful_requests << ",";
    ss << "\"failed_requests\":" << http_stats.failed_requests << ",";
    ss << "\"last_request_time\":" << http_stats.last_request_time << ",";
    ss << "\"average_response_time_ms\":" << http_stats.average_response_time_ms;
    ss << "},\"timestamp\":" << current_timestamp_ms();
    ss << "}";
    
    response.body = ss.str();
    return response;
}

HttpServer::Stats HttpServer::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

}
