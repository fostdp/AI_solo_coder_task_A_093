#pragma once
#include "common.h"
#include <string>
#include <functional>
#include <vector>
#include <map>
#include <thread>
#include <atomic>
#include <mutex>

namespace seismograph {

struct HttpRequest {
    std::string method;
    std::string path;
    std::string query;
    std::map<std::string, std::string> headers;
    std::string body;
    std::map<std::string, std::string> params;
};

struct HttpResponse {
    int status_code;
    std::map<std::string, std::string> headers;
    std::string body;
    std::string content_type;
    
    HttpResponse() : status_code(200), content_type("application/json") {}
};

using HttpHandler = std::function<HttpResponse(const HttpRequest&)>;

class HttpServer {
public:
    HttpServer();
    ~HttpServer();
    
    bool start(int port);
    void stop();
    bool is_running() const;
    
    void add_route(const std::string& method, 
                   const std::string& path, 
                   HttpHandler handler);
    
    void enable_cors(const std::string& allow_origin = "*",
                     const std::string& allow_methods = "GET, POST, PUT, DELETE, OPTIONS",
                     const std::string& allow_headers = "Content-Type");
    
    struct Stats {
        uint64_t total_requests;
        uint64_t successful_requests;
        uint64_t failed_requests;
        uint64_t last_request_time;
        double average_response_time_ms;
    };
    
    Stats get_stats() const;

private:
    struct Route {
        std::string method;
        std::string path;
        HttpHandler handler;
    };
    
    int port_;
    std::atomic<bool> running_;
    std::thread accept_thread_;
    std::vector<Route> routes_;
    bool cors_enabled_;
    std::string cors_origin_;
    std::string cors_methods_;
    std::string cors_headers_;
    
    mutable std::mutex stats_mutex_;
    Stats stats_;
    
    void accept_loop();
    void handle_client(SOCKET client_socket);
    bool parse_request(const std::string& raw_request, HttpRequest& request);
    HttpResponse route_request(const HttpRequest& request);
    std::string serialize_response(const HttpResponse& response);
    
    bool match_route(const std::string& path, const Route& route, 
                    std::map<std::string, std::string>& params);
    
    HttpResponse handle_options(const HttpRequest& request);
    HttpResponse not_found_response();
    HttpResponse server_error_response(const std::string& error);
    
    HttpResponse api_get_sensor_data(const HttpRequest& request);
    HttpResponse api_post_sensor_data(const HttpRequest& request);
    HttpResponse api_get_alerts(const HttpRequest& request);
    HttpResponse api_get_sensitivity(const HttpRequest& request);
    HttpResponse api_run_simulation(const HttpRequest& request);
    HttpResponse api_run_sensitivity_analysis(const HttpRequest& request);
    HttpResponse api_get_stats(const HttpRequest& request);
    HttpResponse api_get_metrics(const HttpRequest& request);
};

}
