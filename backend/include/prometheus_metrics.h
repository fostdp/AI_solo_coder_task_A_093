#pragma once

#include <string>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <functional>
#include <sstream>

namespace seismograph {

enum class MetricType {
    COUNTER,
    GAUGE,
    HISTOGRAM
};

struct MetricSample {
    std::string name;
    std::unordered_map<std::string, std::string> labels;
    double value;
    MetricType type;
    std::string help;
};

class PrometheusRegistry {
public:
    static PrometheusRegistry& instance();

    void register_counter(const std::string& name, const std::string& help,
                          const std::unordered_map<std::string, std::string>& labels = {});
    void register_gauge(const std::string& name, const std::string& help,
                        const std::unordered_map<std::string, std::string>& labels = {});

    void increment_counter(const std::string& name, double value = 1.0);
    void set_gauge(const std::string& name, double value);
    void add_gauge(const std::string& name, double value);

    double get_counter(const std::string& name);
    double get_gauge(const std::string& name);

    void register_callback(const std::string& name,
                           std::function<double()> callback,
                           MetricType type = MetricType::GAUGE,
                           const std::string& help = "");

    std::string expose();

    void reset();

private:
    PrometheusRegistry() = default;
    ~PrometheusRegistry() = default;
    PrometheusRegistry(const PrometheusRegistry&) = delete;
    PrometheusRegistry& operator=(const PrometheusRegistry&) = delete;

    std::string format_labels(const std::unordered_map<std::string, std::string>& labels);

    std::mutex mutex_;
    std::unordered_map<std::string, std::atomic<double>> counters_;
    std::unordered_map<std::string, std::atomic<double>> gauges_;
    std::unordered_map<std::string, std::string> counter_helps_;
    std::unordered_map<std::string, std::string> gauge_helps_;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> counter_labels_;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> gauge_labels_;

    struct CallbackMetric {
        std::string name;
        std::string help;
        MetricType type;
        std::function<double()> fn;
    };
    std::vector<CallbackMetric> callbacks_;
};

class ScopedTimer {
public:
    explicit ScopedTimer(const std::string& metric_name)
        : name_(metric_name), start_(std::chrono::high_resolution_clock::now()) {}
    
    ~ScopedTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(end - start_).count();
        PrometheusRegistry::instance().set_gauge(name_ + "_seconds", ms / 1000.0);
        PrometheusRegistry::instance().increment_counter(name_ + "_total");
    }

private:
    std::string name_;
    std::chrono::high_resolution_clock::time_point start_;
};

}
