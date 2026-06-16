#pragma once
#include "common.h"
#include "column_simulation.h"
#include <vector>
#include <tuple>
#include <functional>

namespace seismograph {

class SensitivityAnalysis {
public:
    SensitivityAnalysis();
    
    void set_column_simulation(ColumnSimulation* simulation);
    
    std::vector<SensitivityResult> analyze_magnitude_sensitivity(
        double min_magnitude, double max_magnitude, int steps,
        double fixed_distance, SoilType soil_type, int num_trials = 100
    );
    
    std::vector<SensitivityResult> analyze_distance_sensitivity(
        double min_distance, double max_distance, int steps,
        double fixed_magnitude, SoilType soil_type, int num_trials = 100
    );
    
    std::vector<std::vector<SensitivityResult>> analyze_2d_sensitivity(
        double min_magnitude, double max_magnitude, int mag_steps,
        double min_distance, double max_distance, int dist_steps,
        SoilType soil_type, int num_trials = 50
    );
    
    std::vector<SensitivityResult> analyze_parameter_sensitivity(
        const std::string& param_name,
        double min_val, double max_val, int steps,
        double fixed_magnitude, double fixed_distance,
        SoilType soil_type, int num_trials = 100
    );
    
    struct DetectionMetrics {
        double detection_probability;
        double false_alarm_rate;
        double mean_response_time;
        double std_response_time;
        double mean_trigger_direction;
        double direction_accuracy;
    };
    
    DetectionMetrics calculate_metrics(
        double magnitude, double distance,
        const std::vector<SimulationResult>& trials
    );
    
    struct DetectionRange {
        double min_magnitude;
        double max_magnitude;
        double min_distance;
        double max_distance;
        double effective_radius;
        double magnitude_threshold;
    };
    
    DetectionRange calculate_detection_range(
        double detection_threshold = 0.8,
        double false_alarm_limit = 0.05
    );
    
    struct OptimalParams {
        double stiffness;
        double damping;
        double trigger_threshold;
        double performance_score;
    };
    
    OptimalParams optimize_parameters(
        double target_magnitude, double target_distance,
        int num_iterations = 100
    );
    
    double calculate_sensitivity_index(
        const std::function<double(double)>& performance_func,
        double param_value, double perturbation = 0.01
    );

private:
    ColumnSimulation* simulation_;
    
    bool run_single_trial(double magnitude, double distance, 
                         SimulationResult& result);
    
    double calculate_false_alarm_rate(
        const std::vector<SimulationResult>& trials,
        double actual_magnitude
    );
    
    double calculate_detection_probability(
        const std::vector<SimulationResult>& trials,
        double threshold_magnitude = 3.0
    );
    
    std::tuple<double, double> calculate_trigger_accuracy(
        const std::vector<SimulationResult>& trials,
        double expected_direction
    );
};

}
