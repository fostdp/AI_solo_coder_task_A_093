#pragma once
#include "common.h"
#include <string>
#include <vector>
#include <memory>
#include <mutex>

namespace seismograph {

class ClickHouseClient {
public:
    ClickHouseClient();
    ~ClickHouseClient();
    
    bool connect(const std::string& host, int port, 
                 const std::string& database,
                 const std::string& user = "default",
                 const std::string& password = "");
    
    void disconnect();
    bool is_connected() const;
    
    bool insert_sensor_data(const SensorData& data);
    bool insert_sensor_data_batch(const std::vector<SensorData>& data);
    
    bool insert_sensitivity_result(const SensitivityResult& result, 
                                   const std::string& device_id);
    bool insert_sensitivity_batch(const std::vector<std::pair<SensitivityResult, std::string>>& batch);
    
    bool insert_alert(const Alert& alert);
    
    bool insert_simulation_run(const std::string& device_id,
                               const std::string& simulation_id,
                               uint64_t start_time,
                               uint64_t end_time,
                               const std::string& parameters,
                               const std::string& result);
    
    std::vector<SensorData> query_sensor_data(
        const std::string& device_id,
        uint64_t start_time,
        uint64_t end_time,
        size_t limit = 10000
    );
    
    std::vector<SensitivityResult> query_sensitivity_analysis(
        const std::string& device_id,
        uint64_t start_time,
        uint64_t end_time,
        size_t limit = 1000
    );
    
    std::vector<Alert> query_alerts(
        const std::string& device_id,
        uint64_t start_time,
        uint64_t end_time,
        const std::string& alert_type = "",
        size_t limit = 1000
    );
    
    struct Stats {
        uint64_t total_inserts;
        uint64_t failed_inserts;
        uint64_t last_insert_time;
        size_t current_queue_size;
    };
    
    Stats get_stats() const;
    
    void set_batch_size(size_t size) { batch_size_ = size; }
    void set_auto_flush_interval_ms(uint64_t ms) { auto_flush_interval_ms_ = ms; }
    
    void start_async_writer();
    void stop_async_writer();
    void flush();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    
    mutable std::mutex mutex_;
    size_t batch_size_;
    uint64_t auto_flush_interval_ms_;
    std::atomic<bool> async_running_;
    std::thread async_thread_;
    
    std::vector<SensorData> sensor_data_queue_;
    
    bool execute_query(const std::string& query);
    std::string format_insert_query(const SensorData& data);
    std::string format_insert_query(const SensitivityResult& result, const std::string& device_id);
    std::string format_insert_query(const Alert& alert);
    
    void async_writer_loop();
};

}
