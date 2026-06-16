#include "sensitivity_analyzer.h"
#include <iostream>
#include <chrono>

namespace seismograph {

SensitivityAnalyzer::SensitivityAnalyzer()
    : running_(false)
    , output_queue_(nullptr)
    , analysis_(nullptr)
    , soil_type_(SoilType::SOIL_MEDIUM) {
    
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.total_analyses = 0;
    stats_.total_trials = 0;
    stats_.last_analysis_time = 0;
    stats_.avg_analysis_ms = 0.0;
}

SensitivityAnalyzer::~SensitivityAnalyzer() {
    stop();
}

bool SensitivityAnalyzer::start(std::shared_ptr<MessageQueue<SensitivityMessage>> output_queue) {
    if (running_) return false;
    
    output_queue_ = output_queue;
    analysis_ = std::make_unique<SensitivityAnalysis>();
    
    running_ = true;
    worker_thread_ = std::thread(&SensitivityAnalyzer::worker_loop, this);
    
    std::cout << "[SensitivityAnalyzer] Started" << std::endl;
    return true;
}

void SensitivityAnalyzer::stop() {
    running_ = false;
    
    {
        std::lock_guard<std::mutex> lock(task_mutex_);
        while (!task_queue_.empty()) task_queue_.pop();
    }
    task_cv_.notify_all();
    
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    
    auto s = get_stats();
    std::cout << "[SensitivityAnalyzer] Stopped. Ran " 
              << s.total_analyses << " analyses, "
              << s.total_trials << " trials" << std::endl;
}

bool SensitivityAnalyzer::is_running() const {
    return running_;
}

void SensitivityAnalyzer::set_dynamics_config(const DynamicsConfig& config) {
    dynamics_config_ = config;
}

void SensitivityAnalyzer::set_sensitivity_config(const SensitivityConfig& config) {
    sensitivity_config_ = config;
}

void SensitivityAnalyzer::set_soil_type(SoilType type) {
    soil_type_ = type;
}

void SensitivityAnalyzer::set_column_simulation(ColumnSimulation* sim) {
    if (analysis_) {
        std::lock_guard<std::mutex> lock(analysis_mutex_);
        analysis_->set_column_simulation(sim);
    }
}

std::vector<SensitivityResult> SensitivityAnalyzer::analyze_magnitude_sensitivity(
    double min_mag, double max_mag, int steps, double fixed_dist, SoilType soil, int trials) {
    
    std::lock_guard<std::mutex> lock(analysis_mutex_);
    auto t0 = std::chrono::high_resolution_clock::now();
    
    auto results = analysis_->analyze_magnitude_sensitivity(
        min_mag, max_mag, steps, fixed_dist, soil, trials);
    
    auto t1 = std::chrono::high_resolution_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    
    {
        std::lock_guard<std::mutex> slock(stats_mutex_);
        stats_.total_analyses++;
        stats_.total_trials += steps * trials;
        stats_.last_analysis_time = current_timestamp_ms();
        double n = static_cast<double>(stats_.total_analyses);
        stats_.avg_analysis_ms = stats_.avg_analysis_ms * ((n - 1.0) / n) + elapsed_ms / n;
    }
    
    if (output_queue_) {
        for (const auto& r : results) {
            SensitivityMessage msg;
            msg.result = r;
            msg.analysis_type = "magnitude";
            msg.computed_at = current_timestamp_ms();
            output_queue_->push(msg);
        }
    }
    
    return results;
}

std::vector<SensitivityResult> SensitivityAnalyzer::analyze_distance_sensitivity(
    double min_dist, double max_dist, int steps, double fixed_mag, SoilType soil, int trials) {
    
    std::lock_guard<std::mutex> lock(analysis_mutex_);
    auto t0 = std::chrono::high_resolution_clock::now();
    
    auto results = analysis_->analyze_distance_sensitivity(
        min_dist, max_dist, steps, fixed_mag, soil, trials);
    
    auto t1 = std::chrono::high_resolution_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    
    {
        std::lock_guard<std::mutex> slock(stats_mutex_);
        stats_.total_analyses++;
        stats_.total_trials += steps * trials;
        stats_.last_analysis_time = current_timestamp_ms();
        double n = static_cast<double>(stats_.total_analyses);
        stats_.avg_analysis_ms = stats_.avg_analysis_ms * ((n - 1.0) / n) + elapsed_ms / n;
    }
    
    if (output_queue_) {
        for (const auto& r : results) {
            SensitivityMessage msg;
            msg.result = r;
            msg.analysis_type = "distance";
            msg.computed_at = current_timestamp_ms();
            output_queue_->push(msg);
        }
    }
    
    return results;
}

std::vector<std::vector<SensitivityResult>> SensitivityAnalyzer::analyze_2d_sensitivity(
    double min_mag, double max_mag, int mag_steps,
    double min_dist, double max_dist, int dist_steps,
    SoilType soil, int trials) {
    
    std::lock_guard<std::mutex> lock(analysis_mutex_);
    auto t0 = std::chrono::high_resolution_clock::now();
    
    auto results = analysis_->analyze_2d_sensitivity(
        min_mag, max_mag, mag_steps,
        min_dist, max_dist, dist_steps,
        soil, trials);
    
    auto t1 = std::chrono::high_resolution_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    
    {
        std::lock_guard<std::mutex> slock(stats_mutex_);
        stats_.total_analyses++;
        stats_.total_trials += mag_steps * dist_steps * trials;
        stats_.last_analysis_time = current_timestamp_ms();
        double n = static_cast<double>(stats_.total_analyses);
        stats_.avg_analysis_ms = stats_.avg_analysis_ms * ((n - 1.0) / n) + elapsed_ms / n;
    }
    
    return results;
}

SensitivityAnalysis::DetectionRange SensitivityAnalyzer::calculate_detection_range(
    double threshold, double false_alarm_limit, SoilType soil) {
    
    std::lock_guard<std::mutex> lock(analysis_mutex_);
    
    auto mag_results = analysis_->analyze_magnitude_sensitivity(
        sensitivity_config_.min_magnitude,
        sensitivity_config_.max_magnitude,
        sensitivity_config_.magnitude_steps,
        50.0, soil, sensitivity_config_.num_trials);
    
    SensitivityAnalysis::DetectionRange range;
    range.min_magnitude = 10.0;
    range.max_magnitude = 0.0;
    range.min_distance = 1000.0;
    range.max_distance = 0.0;
    
    for (const auto& r : mag_results) {
        if (r.detection_probability >= threshold && 
            r.false_alarm_rate <= false_alarm_limit) {
            range.min_magnitude = std::min(range.min_magnitude, r.test_magnitude);
            range.max_magnitude = std::max(range.max_magnitude, r.test_magnitude);
        }
    }
    
    auto dist_results = analysis_->analyze_distance_sensitivity(
        sensitivity_config_.min_distance,
        sensitivity_config_.max_distance,
        sensitivity_config_.distance_steps,
        5.0, soil, sensitivity_config_.num_trials);
    
    for (const auto& r : dist_results) {
        if (r.detection_probability >= threshold && 
            r.false_alarm_rate <= false_alarm_limit) {
            range.min_distance = std::min(range.min_distance, r.test_distance);
            range.max_distance = std::max(range.max_distance, r.test_distance);
        }
    }
    
    range.effective_radius = range.max_distance;
    range.magnitude_threshold = range.min_magnitude;
    
    {
        std::lock_guard<std::mutex> slock(stats_mutex_);
        stats_.total_analyses++;
        stats_.last_analysis_time = current_timestamp_ms();
    }
    
    return range;
}

SensitivityAnalysis::OptimalParams SensitivityAnalyzer::optimize_parameters(
    double target_mag, double target_dist, int iterations, SoilType soil) {
    
    std::lock_guard<std::mutex> lock(analysis_mutex_);
    return analysis_->optimize_parameters(target_mag, target_dist, iterations);
}

void SensitivityAnalyzer::schedule_analysis(const std::string& type, double min_val, double max_val, int steps) {
    {
        std::lock_guard<std::mutex> lock(task_mutex_);
        task_queue_.push([=]() {
            if (type == "magnitude") {
                analyze_magnitude_sensitivity(
                    min_val, max_val, steps, 
                    sensitivity_config_.min_distance,
                    soil_type_, sensitivity_config_.num_trials);
            } else if (type == "distance") {
                analyze_distance_sensitivity(
                    min_val, max_val, steps,
                    (sensitivity_config_.min_magnitude + sensitivity_config_.max_magnitude) / 2.0,
                    soil_type_, sensitivity_config_.num_trials);
            }
        });
    }
    task_cv_.notify_one();
}

SensitivityAnalyzer::Stats SensitivityAnalyzer::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void SensitivityAnalyzer::worker_loop() {
    while (running_) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(task_mutex_);
            task_cv_.wait_for(lock, std::chrono::milliseconds(100), [&] {
                return !task_queue_.empty() || !running_;
            });
            if (!running_ && task_queue_.empty()) break;
            if (task_queue_.empty()) continue;
            task = std::move(task_queue_.front());
            task_queue_.pop();
        }
        
        if (task) {
            try {
                task();
            } catch (const std::exception& e) {
                std::cerr << "[SensitivityAnalyzer] Task error: " << e.what() << std::endl;
            }
        }
    }
}

}
