#pragma once

#include "common.h"
#include "message_queue.h"
#include "config_loader.h"
#include "sensitivity_analysis.h"
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <functional>
#include <queue>

namespace seismograph {

class SensitivityAnalyzer {
public:
    using ResultCallback = std::function<void(const SensitivityResult&)>;

    SensitivityAnalyzer();
    ~SensitivityAnalyzer();

    bool start(std::shared_ptr<MessageQueue<SensitivityMessage>> output_queue = nullptr);
    void stop();
    bool is_running() const;

    void set_dynamics_config(const DynamicsConfig& config);
    void set_sensitivity_config(const SensitivityConfig& config);
    void set_soil_type(SoilType type);

    void set_column_simulation(ColumnSimulation* sim);

    std::vector<SensitivityResult> analyze_magnitude_sensitivity(
        double min_mag, double max_mag, int steps, double fixed_dist, SoilType soil, int trials);

    std::vector<SensitivityResult> analyze_distance_sensitivity(
        double min_dist, double max_dist, int steps, double fixed_mag, SoilType soil, int trials);

    std::vector<std::vector<SensitivityResult>> analyze_2d_sensitivity(
        double min_mag, double max_mag, int mag_steps,
        double min_dist, double max_dist, int dist_steps,
        SoilType soil, int trials);

    SensitivityAnalysis::DetectionRange calculate_detection_range(
        double threshold, double false_alarm_limit, SoilType soil);

    SensitivityAnalysis::OptimalParams optimize_parameters(
        double target_mag, double target_dist, int iterations, SoilType soil);

    void schedule_analysis(const std::string& type, double min_val, double max_val, int steps);

    struct Stats {
        uint64_t total_analyses;
        uint64_t total_trials;
        uint64_t last_analysis_time;
        double avg_analysis_ms;
    };

    Stats get_stats() const;

private:
    void worker_loop();

    std::atomic<bool> running_;
    std::thread worker_thread_;

    std::shared_ptr<MessageQueue<SensitivityMessage>> output_queue_;

    std::unique_ptr<SensitivityAnalysis> analysis_;
    std::mutex analysis_mutex_;

    DynamicsConfig dynamics_config_;
    SensitivityConfig sensitivity_config_;
    SoilType soil_type_;

    std::queue<std::function<void()>> task_queue_;
    std::mutex task_mutex_;
    std::condition_variable task_cv_;

    mutable std::mutex stats_mutex_;
    Stats stats_;
};

}
