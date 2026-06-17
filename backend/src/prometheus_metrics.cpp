#include "prometheus_metrics.h"
#include <chrono>
#include <algorithm>
#include <iomanip>

namespace seismograph {

PrometheusRegistry& PrometheusRegistry::instance() {
    static PrometheusRegistry s;
    return s;
}

void PrometheusRegistry::register_counter(const std::string& name, const std::string& help,
                                           const std::unordered_map<std::string, std::string>& labels) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (counters_.find(name) == counters_.end()) {
        counters_[name].store(0.0);
        counter_helps_[name] = help;
        counter_labels_[name] = labels;
    }
}

void PrometheusRegistry::register_gauge(const std::string& name, const std::string& help,
                                         const std::unordered_map<std::string, std::string>& labels) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (gauges_.find(name) == gauges_.end()) {
        gauges_[name].store(0.0);
        gauge_helps_[name] = help;
        gauge_labels_[name] = labels;
    }
}

void PrometheusRegistry::increment_counter(const std::string& name, double value) {
    auto it = counters_.find(name);
    if (it != counters_.end()) {
        it->second.fetch_add(value);
    } else {
        std::lock_guard<std::mutex> lock(mutex_);
        counters_[name].store(value);
        counter_helps_[name] = "";
    }
}

void PrometheusRegistry::set_gauge(const std::string& name, double value) {
    auto it = gauges_.find(name);
    if (it != gauges_.end()) {
        it->second.store(value);
    } else {
        std::lock_guard<std::mutex> lock(mutex_);
        gauges_[name].store(value);
        gauge_helps_[name] = "";
    }
}

void PrometheusRegistry::add_gauge(const std::string& name, double value) {
    auto it = gauges_.find(name);
    if (it != gauges_.end()) {
        it->second.fetch_add(value);
    } else {
        std::lock_guard<std::mutex> lock(mutex_);
        gauges_[name].store(value);
        gauge_helps_[name] = "";
    }
}

double PrometheusRegistry::get_counter(const std::string& name) {
    auto it = counters_.find(name);
    return it != counters_.end() ? it->second.load() : 0.0;
}

double PrometheusRegistry::get_gauge(const std::string& name) {
    auto it = gauges_.find(name);
    return it != gauges_.end() ? it->second.load() : 0.0;
}

void PrometheusRegistry::register_callback(const std::string& name,
                                            std::function<double()> callback,
                                            MetricType type,
                                            const std::string& help) {
    std::lock_guard<std::mutex> lock(mutex_);
    callbacks_.push_back({name, help, type, std::move(callback)});
}

std::string PrometheusRegistry::format_labels(const std::unordered_map<std::string, std::string>& labels) {
    if (labels.empty()) return "";
    std::ostringstream ss;
    ss << "{";
    bool first = true;
    for (const auto& [k, v] : labels) {
        if (!first) ss << ",";
        ss << k << "=\"" << v << "\"";
        first = false;
    }
    ss << "}";
    return ss.str();
}

std::string PrometheusRegistry::expose() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(6);

    for (const auto& [name, help] : counter_helps_) {
        if (!help.empty()) ss << "# HELP " << name << " " << help << "\n";
        ss << "# TYPE " << name << " counter\n";
        auto it = counters_.find(name);
        double val = it != counters_.end() ? it->second.load() : 0.0;
        auto lit = counter_labels_.find(name);
        ss << name;
        if (lit != counter_labels_.end()) ss << format_labels(lit->second);
        ss << " " << val << "\n";
    }

    for (const auto& [name, help] : gauge_helps_) {
        if (!help.empty()) ss << "# HELP " << name << " " << help << "\n";
        ss << "# TYPE " << name << " gauge\n";
        auto it = gauges_.find(name);
        double val = it != gauges_.end() ? it->second.load() : 0.0;
        auto lit = gauge_labels_.find(name);
        ss << name;
        if (lit != gauge_labels_.end()) ss << format_labels(lit->second);
        ss << " " << val << "\n";
    }

    for (const auto& cb : callbacks_) {
        double val = 0.0;
        try { val = cb.fn ? cb.fn() : 0.0; } catch (...) {}
        
        if (!cb.help.empty()) ss << "# HELP " << cb.name << " " << cb.help << "\n";
        const char* type_str = cb.type == MetricType::COUNTER ? "counter" : "gauge";
        ss << "# TYPE " << cb.name << " " << type_str << "\n";
        ss << cb.name << " " << val << "\n";
    }

    return ss.str();
}

void PrometheusRegistry::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [name, val] : counters_) val.store(0.0);
    for (auto& [name, val] : gauges_) val.store(0.0);
}

}
