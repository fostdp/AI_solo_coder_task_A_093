#pragma once

#include "common.h"
#include <string>
#include <vector>

namespace seismograph {

struct DynamicsConfig {
    double mass;
    double height;
    double base_radius;
    double top_radius;
    double stiffness;
    double damping_coefficient;
    double static_friction;
    double dynamic_friction;
    double trigger_threshold;
    double max_angle;
    double time_step;
};

struct SeismicWaveConfig {
    double default_magnitude;
    double default_distance;
    double default_duration;
    double p_wave_freq;
    double s_wave_freq;
    double rayleigh_freq;
    double p_wave_amp_ratio;
    double s_wave_amp_ratio;
    double rayleigh_amp_ratio;
};

struct SoilConfig {
    std::string default_type;
};

struct SensitivityConfig {
    double min_magnitude;
    double max_magnitude;
    int magnitude_steps;
    double min_distance;
    double max_distance;
    int distance_steps;
    int num_trials;
    double detection_threshold;
    double false_alarm_limit;
    int analysis_interval_minutes;
};

struct AlertConfig {
    double false_trigger_threshold;
    double sensitivity_decrease_threshold;
    double baseline_sensitivity;
    double detection_magnitude_threshold;
    int history_window_seconds;
};

struct ServicesConfig {
    int udp_port;
    int http_port;
    std::string clickhouse_host;
    int clickhouse_port;
    std::string clickhouse_database;
    std::string mqtt_host;
    int mqtt_port;
    std::string device_id;
};

struct AppConfig {
    DynamicsConfig dynamics;
    SeismicWaveConfig seismic_wave;
    SoilConfig soil;
    SensitivityConfig sensitivity;
    AlertConfig alert;
    ServicesConfig services;
};

class ConfigLoader {
public:
    static bool load(const std::string& config_path, AppConfig& config);
    static bool save(const std::string& config_path, const AppConfig& config);
    
    static ColumnParams to_column_params(const DynamicsConfig& dynamics);
    static SeismicWaveParams to_wave_params(const SeismicWaveConfig& wave, 
                                           const SoilConfig& soil);
    static SoilType soil_type_from_string(const std::string& type_str);
    static std::string soil_type_to_string(SoilType type);

private:
    static bool parse_json(const std::string& json_str, AppConfig& config);
    static std::string extract_json_value(const std::string& json, const std::string& key);
    static std::string extract_json_object(const std::string& json, const std::string& key);
    static std::vector<std::string> extract_json_keys(const std::string& json);
    static std::string trim(const std::string& s);
    static double to_double(const std::string& s, double default_val);
    static int to_int(const std::string& s, int default_val);
};

}
