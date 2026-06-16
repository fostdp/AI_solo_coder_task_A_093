#include "sensitivity_analysis.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <random>

namespace seismograph {

SensitivityAnalysis::SensitivityAnalysis()
    : simulation_(nullptr) {
}

void SensitivityAnalysis::set_column_simulation(ColumnSimulation* simulation) {
    simulation_ = simulation;
}

bool SensitivityAnalysis::run_single_trial(double magnitude, double distance,
                                          SoilType soil_type,
                                          SimulationResult& result) {
    if (!simulation_) return false;
    
    SeismicWaveParams params;
    params.magnitude = magnitude;
    params.epicenter_distance = distance;
    params.depth = 10.0;
    params.frequency = 2.0;
    params.duration = 10.0;
    params.soil_type = soil_type;
    
    result = simulation_->simulate(params, 0.001);
    return true;
}

double SensitivityAnalysis::calculate_detection_probability(
    const std::vector<SimulationResult>& trials,
    double threshold_magnitude) {
    
    if (trials.empty()) return 0.0;
    
    size_t detected = 0;
    for (const auto& trial : trials) {
        if (trial.is_triggered) {
            ++detected;
        }
    }
    
    return static_cast<double>(detected) / trials.size();
}

double SensitivityAnalysis::calculate_false_alarm_rate(
    const std::vector<SimulationResult>& trials,
    double actual_magnitude) {
    
    if (trials.empty()) return 0.0;
    
    size_t false_alarms = 0;
    double detection_threshold = 3.0;
    
    for (const auto& trial : trials) {
        if (trial.is_triggered && actual_magnitude < detection_threshold) {
            ++false_alarms;
        }
    }
    
    return static_cast<double>(false_alarms) / trials.size();
}

std::tuple<double, double> SensitivityAnalysis::calculate_trigger_accuracy(
    const std::vector<SimulationResult>& trials,
    double expected_direction) {
    
    if (trials.empty()) return {0.0, 0.0};
    
    std::vector<double> directions;
    size_t valid_triggers = 0;
    
    for (const auto& trial : trials) {
        if (trial.is_triggered && trial.trigger_direction >= 0) {
            directions.push_back(trial.trigger_direction);
            ++valid_triggers;
        }
    }
    
    if (directions.empty()) return {0.0, 0.0};
    
    double mean_dir = std::accumulate(directions.begin(), directions.end(), 0.0) / directions.size();
    
    double accuracy = 0.0;
    for (double dir : directions) {
        double diff = std::abs(dir - expected_direction);
        diff = std::min(diff, 8.0 - diff);
        accuracy += 1.0 - (diff / 4.0);
    }
    accuracy /= directions.size();
    
    return {mean_dir, accuracy};
}

SensitivityAnalysis::DetectionMetrics SensitivityAnalysis::calculate_metrics(
    double magnitude, double distance,
    const std::vector<SimulationResult>& trials) {
    
    DetectionMetrics metrics;
    metrics.detection_probability = calculate_detection_probability(trials);
    metrics.false_alarm_rate = calculate_false_alarm_rate(trials, magnitude);
    
    std::vector<double> response_times;
    for (const auto& trial : trials) {
        if (trial.is_triggered) {
            response_times.push_back(trial.response_time_ms);
        }
    }
    
    if (!response_times.empty()) {
        metrics.mean_response_time = std::accumulate(
            response_times.begin(), response_times.end(), 0.0
        ) / response_times.size();
        
        double sum_sq = 0.0;
        for (double rt : response_times) {
            sum_sq += (rt - metrics.mean_response_time) * (rt - metrics.mean_response_time);
        }
        metrics.std_response_time = std::sqrt(sum_sq / response_times.size());
    } else {
        metrics.mean_response_time = 0.0;
        metrics.std_response_time = 0.0;
    }
    
    double expected_direction = 0.0;
    auto [mean_dir, accuracy] = calculate_trigger_accuracy(trials, expected_direction);
    metrics.mean_trigger_direction = mean_dir;
    metrics.direction_accuracy = accuracy;
    
    return metrics;
}

std::vector<SensitivityResult> SensitivityAnalysis::analyze_magnitude_sensitivity(
    double min_magnitude, double max_magnitude, int steps,
    double fixed_distance, SoilType soil_type, int num_trials) {
    
    std::vector<SensitivityResult> results;
    double mag_step = (max_magnitude - min_magnitude) / (steps - 1);

    if (simulation_) {
        simulation_->set_soil_type(static_cast<ColumnSimulation::SoilType>(soil_type));
    }

    double amp_ref = 0.0;
    if (simulation_) {
        amp_ref = (0.3 * simulation_->calculate_soil_amplification(5.0)
                 + 0.5 * simulation_->calculate_soil_amplification(3.0)
                 + 0.2 * simulation_->calculate_soil_amplification(1.5));
    }
    
    for (int i = 0; i < steps; ++i) {
        double magnitude = min_magnitude + i * mag_step;
        
        std::vector<SimulationResult> trials;
        for (int t = 0; t < num_trials; ++t) {
            SimulationResult result;
            if (run_single_trial(magnitude, fixed_distance, soil_type, result)) {
                trials.push_back(result);
            }
        }
        
        DetectionMetrics metrics = calculate_metrics(magnitude, fixed_distance, trials);
        
        SensitivityResult sr;
        sr.test_magnitude = magnitude;
        sr.test_distance = fixed_distance;
        sr.soil_type = soil_type;
        sr.detection_probability = metrics.detection_probability;
        sr.false_alarm_rate = metrics.false_alarm_rate;
        sr.response_time_ms = metrics.mean_response_time;
        sr.trigger_direction = static_cast<int>(metrics.mean_trigger_direction);
        sr.column_stiffness = simulation_ ? simulation_->get_column_params().stiffness : 0.0;
        sr.damping_coefficient = simulation_ ? simulation_->get_column_params().damping_coefficient : 0.0;
        sr.soil_amplification_factor = amp_ref;
        
        results.push_back(sr);
    }
    
    return results;
}

std::vector<SensitivityResult> SensitivityAnalysis::analyze_distance_sensitivity(
    double min_distance, double max_distance, int steps,
    double fixed_magnitude, SoilType soil_type, int num_trials) {
    
    std::vector<SensitivityResult> results;
    double dist_step = (max_distance - min_distance) / (steps - 1);

    if (simulation_) {
        simulation_->set_soil_type(static_cast<ColumnSimulation::SoilType>(soil_type));
    }

    double amp_ref = 0.0;
    if (simulation_) {
        amp_ref = (0.3 * simulation_->calculate_soil_amplification(5.0)
                 + 0.5 * simulation_->calculate_soil_amplification(3.0)
                 + 0.2 * simulation_->calculate_soil_amplification(1.5));
    }
    
    for (int i = 0; i < steps; ++i) {
        double distance = min_distance + i * dist_step;
        
        std::vector<SimulationResult> trials;
        for (int t = 0; t < num_trials; ++t) {
            SimulationResult result;
            if (run_single_trial(fixed_magnitude, distance, soil_type, result)) {
                trials.push_back(result);
            }
        }
        
        DetectionMetrics metrics = calculate_metrics(fixed_magnitude, distance, trials);
        
        SensitivityResult sr;
        sr.test_magnitude = fixed_magnitude;
        sr.test_distance = distance;
        sr.soil_type = soil_type;
        sr.detection_probability = metrics.detection_probability;
        sr.false_alarm_rate = metrics.false_alarm_rate;
        sr.response_time_ms = metrics.mean_response_time;
        sr.trigger_direction = static_cast<int>(metrics.mean_trigger_direction);
        sr.column_stiffness = simulation_ ? simulation_->get_column_params().stiffness : 0.0;
        sr.damping_coefficient = simulation_ ? simulation_->get_column_params().damping_coefficient : 0.0;
        sr.soil_amplification_factor = amp_ref;
        
        results.push_back(sr);
    }
    
    return results;
}

std::vector<std::vector<SensitivityResult>> SensitivityAnalysis::analyze_2d_sensitivity(
    double min_magnitude, double max_magnitude, int mag_steps,
    double min_distance, double max_distance, int dist_steps,
    SoilType soil_type, int num_trials) {
    
    std::vector<std::vector<SensitivityResult>> results;
    double mag_step = (max_magnitude - min_magnitude) / (mag_steps - 1);
    double dist_step = (max_distance - min_distance) / (dist_steps - 1);

    if (simulation_) {
        simulation_->set_soil_type(static_cast<ColumnSimulation::SoilType>(soil_type));
    }

    double amp_ref = 0.0;
    if (simulation_) {
        amp_ref = (0.3 * simulation_->calculate_soil_amplification(5.0)
                 + 0.5 * simulation_->calculate_soil_amplification(3.0)
                 + 0.2 * simulation_->calculate_soil_amplification(1.5));
    }
    
    for (int i = 0; i < mag_steps; ++i) {
        std::vector<SensitivityResult> row;
        double magnitude = min_magnitude + i * mag_step;
        
        for (int j = 0; j < dist_steps; ++j) {
            double distance = min_distance + j * dist_step;
            
            std::vector<SimulationResult> trials;
            for (int t = 0; t < num_trials; ++t) {
                SimulationResult result;
                if (run_single_trial(magnitude, distance, soil_type, result)) {
                    trials.push_back(result);
                }
            }
            
            DetectionMetrics metrics = calculate_metrics(magnitude, distance, trials);
            
            SensitivityResult sr;
            sr.test_magnitude = magnitude;
            sr.test_distance = distance;
            sr.soil_type = soil_type;
            sr.detection_probability = metrics.detection_probability;
            sr.false_alarm_rate = metrics.false_alarm_rate;
            sr.response_time_ms = metrics.mean_response_time;
            sr.trigger_direction = static_cast<int>(metrics.mean_trigger_direction);
            sr.column_stiffness = simulation_ ? simulation_->get_column_params().stiffness : 0.0;
            sr.damping_coefficient = simulation_ ? simulation_->get_column_params().damping_coefficient : 0.0;
            sr.soil_amplification_factor = amp_ref;
            
            row.push_back(sr);
        }
        
        results.push_back(row);
    }
    
    return results;
}

std::vector<SensitivityResult> SensitivityAnalysis::analyze_parameter_sensitivity(
    const std::string& param_name,
    double min_val, double max_val, int steps,
    double fixed_magnitude, double fixed_distance,
    SoilType soil_type, int num_trials) {
    
    std::vector<SensitivityResult> results;
    double step = (max_val - min_val) / (steps - 1);
    
    ColumnParams original_params;
    if (simulation_) {
        original_params = simulation_->get_column_params();
    }
    
    for (int i = 0; i < steps; ++i) {
        double val = min_val + i * step;
        
        if (simulation_) {
            ColumnParams params = original_params;
            
            if (param_name == "stiffness") {
                params.stiffness = val;
            } else if (param_name == "damping") {
                params.damping_coefficient = val;
            } else if (param_name == "mass") {
                params.mass = val;
            } else if (param_name == "height") {
                params.height = val;
            } else if (param_name == "trigger_threshold") {
                params.trigger_threshold = val;
            } else if (param_name == "base_radius") {
                params.base_radius = val;
            }
            
            simulation_->set_column_params(params);
        }
        
        std::vector<SimulationResult> trials;
        for (int t = 0; t < num_trials; ++t) {
            SimulationResult result;
            if (run_single_trial(fixed_magnitude, fixed_distance, soil_type, result)) {
                trials.push_back(result);
            }
        }
        
        DetectionMetrics metrics = calculate_metrics(fixed_magnitude, fixed_distance, trials);
        
        SensitivityResult sr;
        sr.test_magnitude = fixed_magnitude;
        sr.test_distance = fixed_distance;
        sr.soil_type = soil_type;
        sr.detection_probability = metrics.detection_probability;
        sr.false_alarm_rate = metrics.false_alarm_rate;
        sr.response_time_ms = metrics.mean_response_time;
        sr.trigger_direction = static_cast<int>(metrics.mean_trigger_direction);
        sr.column_stiffness = simulation_ ? simulation_->get_column_params().stiffness : 0.0;
        sr.damping_coefficient = simulation_ ? simulation_->get_column_params().damping_coefficient : 0.0;
        
        results.push_back(sr);
    }
    
    if (simulation_) {
        simulation_->set_column_params(original_params);
    }
    
    return results;
}

SensitivityAnalysis::DetectionRange SensitivityAnalysis::calculate_detection_range(
    double detection_threshold, double false_alarm_limit) {
    
    DetectionRange range;
    range.min_magnitude = 10.0;
    range.max_magnitude = 0.0;
    range.min_distance = 1000.0;
    range.max_distance = 0.0;
    
    SoilType default_soil = SoilType::SOIL_MEDIUM;
    auto mag_results = analyze_magnitude_sensitivity(2.0, 8.0, 13, 50.0, default_soil, 50);
    
    for (const auto& result : mag_results) {
        if (result.detection_probability >= detection_threshold && 
            result.false_alarm_rate <= false_alarm_limit) {
            range.min_magnitude = std::min(range.min_magnitude, result.test_magnitude);
            range.max_magnitude = std::max(range.max_magnitude, result.test_magnitude);
        }
    }
    
    auto dist_results = analyze_distance_sensitivity(10.0, 500.0, 50, 5.0, default_soil, 50);
    
    for (const auto& result : dist_results) {
        if (result.detection_probability >= detection_threshold && 
            result.false_alarm_rate <= false_alarm_limit) {
            range.min_distance = std::min(range.min_distance, result.test_distance);
            range.max_distance = std::max(range.max_distance, result.test_distance);
        }
    }
    
    range.effective_radius = range.max_distance;
    range.magnitude_threshold = range.min_magnitude;
    
    return range;
}

SensitivityAnalysis::OptimalParams SensitivityAnalysis::optimize_parameters(
    double target_magnitude, double target_distance,
    int num_iterations) {
    
    OptimalParams best_params;
    best_params.performance_score = -1.0;
    
    if (!simulation_) return best_params;
    
    ColumnParams original_params = simulation_->get_column_params();
    
    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<double> stiffness_dist(10000.0, 100000.0);
    std::uniform_real_distribution<double> damping_dist(100.0, 1000.0);
    std::uniform_real_distribution<double> threshold_dist(0.01, 0.1);
    
    for (int iter = 0; iter < num_iterations; ++iter) {
        ColumnParams params = original_params;
        params.stiffness = stiffness_dist(rng);
        params.damping_coefficient = damping_dist(rng);
        params.trigger_threshold = threshold_dist(rng);
        
        simulation_->set_column_params(params);
        
        SoilType default_soil = SoilType::SOIL_MEDIUM;
        std::vector<SimulationResult> trials;
        for (int t = 0; t < 20; ++t) {
            SimulationResult result;
            if (run_single_trial(target_magnitude, target_distance, default_soil, result)) {
                trials.push_back(result);
            }
        }
        
        DetectionMetrics metrics = calculate_metrics(target_magnitude, target_distance, trials);
        
        double score = metrics.detection_probability * 0.6 
                     - metrics.false_alarm_rate * 0.2 
                     + (1.0 / (1.0 + metrics.mean_response_time / 1000.0)) * 0.2;
        
        if (score > best_params.performance_score) {
            best_params.performance_score = score;
            best_params.stiffness = params.stiffness;
            best_params.damping = params.damping_coefficient;
            best_params.trigger_threshold = params.trigger_threshold;
        }
    }
    
    simulation_->set_column_params(original_params);
    
    return best_params;
}

double SensitivityAnalysis::calculate_sensitivity_index(
    const std::function<double(double)>& performance_func,
    double param_value, double perturbation) {
    
    double p_plus = param_value * (1.0 + perturbation);
    double p_minus = param_value * (1.0 - perturbation);
    
    double f_plus = performance_func(p_plus);
    double f_minus = performance_func(p_minus);
    
    double delta_f = f_plus - f_minus;
    double delta_p = p_plus - p_minus;
    
    return (delta_f / f_plus) / (delta_p / param_value);
}

}
