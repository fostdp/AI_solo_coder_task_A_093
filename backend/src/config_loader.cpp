#include "config_loader.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cctype>

namespace seismograph {

bool ConfigLoader::load(const std::string& config_path, AppConfig& config) {
    std::ifstream file(config_path);
    if (!file.is_open()) {
        std::cerr << "Failed to open config file: " << config_path << std::endl;
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json_str = buffer.str();

    return parse_json(json_str, config);
}

bool ConfigLoader::save(const std::string& config_path, const AppConfig& config) {
    std::ofstream file(config_path);
    if (!file.is_open()) {
        std::cerr << "Failed to write config file: " << config_path << std::endl;
        return false;
    }

    file << "{\n";
    file << "    \"dynamics\": {\n";
    file << "        \"mass\": " << config.dynamics.mass << ",\n";
    file << "        \"height\": " << config.dynamics.height << ",\n";
    file << "        \"base_radius\": " << config.dynamics.base_radius << ",\n";
    file << "        \"top_radius\": " << config.dynamics.top_radius << ",\n";
    file << "        \"stiffness\": " << config.dynamics.stiffness << ",\n";
    file << "        \"damping_coefficient\": " << config.dynamics.damping_coefficient << ",\n";
    file << "        \"static_friction\": " << config.dynamics.static_friction << ",\n";
    file << "        \"dynamic_friction\": " << config.dynamics.dynamic_friction << ",\n";
    file << "        \"trigger_threshold\": " << config.dynamics.trigger_threshold << ",\n";
    file << "        \"max_angle\": " << config.dynamics.max_angle << ",\n";
    file << "        \"time_step\": " << config.dynamics.time_step << "\n";
    file << "    },\n";
    file << "    \"seismic_wave\": {\n";
    file << "        \"default_magnitude\": " << config.seismic_wave.default_magnitude << ",\n";
    file << "        \"default_distance\": " << config.seismic_wave.default_distance << ",\n";
    file << "        \"default_duration\": " << config.seismic_wave.default_duration << ",\n";
    file << "        \"p_wave_freq\": " << config.seismic_wave.p_wave_freq << ",\n";
    file << "        \"s_wave_freq\": " << config.seismic_wave.s_wave_freq << ",\n";
    file << "        \"rayleigh_freq\": " << config.seismic_wave.rayleigh_freq << ",\n";
    file << "        \"p_wave_amp_ratio\": " << config.seismic_wave.p_wave_amp_ratio << ",\n";
    file << "        \"s_wave_amp_ratio\": " << config.seismic_wave.s_wave_amp_ratio << ",\n";
    file << "        \"rayleigh_amp_ratio\": " << config.seismic_wave.rayleigh_amp_ratio << "\n";
    file << "    },\n";
    file << "    \"soil\": {\n";
    file << "        \"default_type\": \"" << config.soil.default_type << "\"\n";
    file << "    },\n";
    file << "    \"sensitivity\": {\n";
    file << "        \"min_magnitude\": " << config.sensitivity.min_magnitude << ",\n";
    file << "        \"max_magnitude\": " << config.sensitivity.max_magnitude << ",\n";
    file << "        \"magnitude_steps\": " << config.sensitivity.magnitude_steps << ",\n";
    file << "        \"min_distance\": " << config.sensitivity.min_distance << ",\n";
    file << "        \"max_distance\": " << config.sensitivity.max_distance << ",\n";
    file << "        \"distance_steps\": " << config.sensitivity.distance_steps << ",\n";
    file << "        \"num_trials\": " << config.sensitivity.num_trials << ",\n";
    file << "        \"detection_threshold\": " << config.sensitivity.detection_threshold << ",\n";
    file << "        \"false_alarm_limit\": " << config.sensitivity.false_alarm_limit << ",\n";
    file << "        \"analysis_interval_minutes\": " << config.sensitivity.analysis_interval_minutes << "\n";
    file << "    },\n";
    file << "    \"alert\": {\n";
    file << "        \"false_trigger_threshold\": " << config.alert.false_trigger_threshold << ",\n";
    file << "        \"sensitivity_decrease_threshold\": " << config.alert.sensitivity_decrease_threshold << ",\n";
    file << "        \"baseline_sensitivity\": " << config.alert.baseline_sensitivity << ",\n";
    file << "        \"detection_magnitude_threshold\": " << config.alert.detection_magnitude_threshold << ",\n";
    file << "        \"history_window_seconds\": " << config.alert.history_window_seconds << "\n";
    file << "    },\n";
    file << "    \"services\": {\n";
    file << "        \"udp_port\": " << config.services.udp_port << ",\n";
    file << "        \"http_port\": " << config.services.http_port << ",\n";
    file << "        \"clickhouse_host\": \"" << config.services.clickhouse_host << "\",\n";
    file << "        \"clickhouse_port\": " << config.services.clickhouse_port << ",\n";
    file << "        \"clickhouse_database\": \"" << config.services.clickhouse_database << "\",\n";
    file << "        \"mqtt_host\": \"" << config.services.mqtt_host << "\",\n";
    file << "        \"mqtt_port\": " << config.services.mqtt_port << ",\n";
    file << "        \"device_id\": \"" << config.services.device_id << "\"\n";
    file << "    }\n";
    file << "}\n";

    return true;
}

ColumnParams ConfigLoader::to_column_params(const DynamicsConfig& dynamics) {
    ColumnParams params;
    params.mass = dynamics.mass;
    params.height = dynamics.height;
    params.base_radius = dynamics.base_radius;
    params.top_radius = dynamics.top_radius;
    params.stiffness = dynamics.stiffness;
    params.damping_coefficient = dynamics.damping_coefficient;
    params.static_friction = dynamics.static_friction;
    params.dynamic_friction = dynamics.dynamic_friction;
    params.trigger_threshold = dynamics.trigger_threshold;
    return params;
}

SeismicWaveParams ConfigLoader::to_wave_params(const SeismicWaveConfig& wave,
                                               const SoilConfig& soil) {
    SeismicWaveParams params;
    params.magnitude = wave.default_magnitude;
    params.epicenter_distance = wave.default_distance;
    params.duration = wave.default_duration;
    params.soil_type = soil_type_from_string(soil.default_type);
    return params;
}

SoilType ConfigLoader::soil_type_from_string(const std::string& type_str) {
    if (type_str == "ROCK_HARD") return SoilType::ROCK_HARD;
    if (type_str == "SOIL_MEDIUM") return SoilType::SOIL_MEDIUM;
    if (type_str == "SOIL_SOFT") return SoilType::SOIL_SOFT;
    if (type_str == "SOIL_VERY_SOFT") return SoilType::SOIL_VERY_SOFT;
    return SoilType::SOIL_MEDIUM;
}

std::string ConfigLoader::soil_type_to_string(SoilType type) {
    switch (type) {
        case SoilType::ROCK_HARD: return "ROCK_HARD";
        case SoilType::SOIL_MEDIUM: return "SOIL_MEDIUM";
        case SoilType::SOIL_SOFT: return "SOIL_SOFT";
        case SoilType::SOIL_VERY_SOFT: return "SOIL_VERY_SOFT";
        default: return "SOIL_MEDIUM";
    }
}

std::string ConfigLoader::trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    size_t end = s.find_last_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    return s.substr(start, end - start + 1);
}

double ConfigLoader::to_double(const std::string& s, double default_val) {
    try {
        return std::stod(s);
    } catch (...) {
        return default_val;
    }
}

int ConfigLoader::to_int(const std::string& s, int default_val) {
    try {
        return std::stoi(s);
    } catch (...) {
        return default_val;
    }
}

std::string ConfigLoader::extract_json_value(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t key_pos = json.find(search);
    if (key_pos == std::string::npos) return "";

    size_t colon_pos = json.find(':', key_pos);
    if (colon_pos == std::string::npos) return "";

    size_t value_start = colon_pos + 1;
    while (value_start < json.size() && (json[value_start] == ' ' || json[value_start] == '\t' || json[value_start] == '\n' || json[value_start] == '\r')) {
        value_start++;
    }

    if (value_start >= json.size()) return "";

    if (json[value_start] == '"') {
        value_start++;
        size_t value_end = json.find('"', value_start);
        if (value_end == std::string::npos) return "";
        return json.substr(value_start, value_end - value_start);
    } else {
        size_t value_end = json.find_first_of(",\n\r}", value_start);
        if (value_end == std::string::npos) value_end = json.size();
        return trim(json.substr(value_start, value_end - value_start));
    }
}

std::string ConfigLoader::extract_json_object(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t key_pos = json.find(search);
    if (key_pos == std::string::npos) return "";

    size_t brace_pos = json.find('{', key_pos);
    if (brace_pos == std::string::npos) return "";

    int depth = 1;
    size_t pos = brace_pos + 1;
    while (pos < json.size() && depth > 0) {
        if (json[pos] == '{') depth++;
        else if (json[pos] == '}') depth--;
        pos++;
    }

    return json.substr(brace_pos, pos - brace_pos);
}

bool ConfigLoader::parse_json(const std::string& json_str, AppConfig& config) {
    std::string dynamics_obj = extract_json_object(json_str, "dynamics");
    if (!dynamics_obj.empty()) {
        config.dynamics.mass = to_double(extract_json_value(dynamics_obj, "mass"), 1000.0);
        config.dynamics.height = to_double(extract_json_value(dynamics_obj, "height"), 2.5);
        config.dynamics.base_radius = to_double(extract_json_value(dynamics_obj, "base_radius"), 0.3);
        config.dynamics.top_radius = to_double(extract_json_value(dynamics_obj, "top_radius"), 0.15);
        config.dynamics.stiffness = to_double(extract_json_value(dynamics_obj, "stiffness"), 50000.0);
        config.dynamics.damping_coefficient = to_double(extract_json_value(dynamics_obj, "damping_coefficient"), 500.0);
        config.dynamics.static_friction = to_double(extract_json_value(dynamics_obj, "static_friction"), 0.6);
        config.dynamics.dynamic_friction = to_double(extract_json_value(dynamics_obj, "dynamic_friction"), 0.4);
        config.dynamics.trigger_threshold = to_double(extract_json_value(dynamics_obj, "trigger_threshold"), 0.05);
        config.dynamics.max_angle = to_double(extract_json_value(dynamics_obj, "max_angle"), 0.2);
        config.dynamics.time_step = to_double(extract_json_value(dynamics_obj, "time_step"), 0.001);
    }

    std::string wave_obj = extract_json_object(json_str, "seismic_wave");
    if (!wave_obj.empty()) {
        config.seismic_wave.default_magnitude = to_double(extract_json_value(wave_obj, "default_magnitude"), 5.0);
        config.seismic_wave.default_distance = to_double(extract_json_value(wave_obj, "default_distance"), 50.0);
        config.seismic_wave.default_duration = to_double(extract_json_value(wave_obj, "default_duration"), 10.0);
        config.seismic_wave.p_wave_freq = to_double(extract_json_value(wave_obj, "p_wave_freq"), 5.0);
        config.seismic_wave.s_wave_freq = to_double(extract_json_value(wave_obj, "s_wave_freq"), 3.0);
        config.seismic_wave.rayleigh_freq = to_double(extract_json_value(wave_obj, "rayleigh_freq"), 1.5);
        config.seismic_wave.p_wave_amp_ratio = to_double(extract_json_value(wave_obj, "p_wave_amp_ratio"), 0.3);
        config.seismic_wave.s_wave_amp_ratio = to_double(extract_json_value(wave_obj, "s_wave_amp_ratio"), 0.5);
        config.seismic_wave.rayleigh_amp_ratio = to_double(extract_json_value(wave_obj, "rayleigh_amp_ratio"), 0.2);
    }

    std::string soil_obj = extract_json_object(json_str, "soil");
    if (!soil_obj.empty()) {
        config.soil.default_type = extract_json_value(soil_obj, "default_type");
        if (config.soil.default_type.empty()) config.soil.default_type = "SOIL_MEDIUM";
    }

    std::string sens_obj = extract_json_object(json_str, "sensitivity");
    if (!sens_obj.empty()) {
        config.sensitivity.min_magnitude = to_double(extract_json_value(sens_obj, "min_magnitude"), 2.0);
        config.sensitivity.max_magnitude = to_double(extract_json_value(sens_obj, "max_magnitude"), 8.0);
        config.sensitivity.magnitude_steps = to_int(extract_json_value(sens_obj, "magnitude_steps"), 13);
        config.sensitivity.min_distance = to_double(extract_json_value(sens_obj, "min_distance"), 10.0);
        config.sensitivity.max_distance = to_double(extract_json_value(sens_obj, "max_distance"), 500.0);
        config.sensitivity.distance_steps = to_int(extract_json_value(sens_obj, "distance_steps"), 50);
        config.sensitivity.num_trials = to_int(extract_json_value(sens_obj, "num_trials"), 50);
        config.sensitivity.detection_threshold = to_double(extract_json_value(sens_obj, "detection_threshold"), 0.9);
        config.sensitivity.false_alarm_limit = to_double(extract_json_value(sens_obj, "false_alarm_limit"), 0.1);
        config.sensitivity.analysis_interval_minutes = to_int(extract_json_value(sens_obj, "analysis_interval_minutes"), 5);
    }

    std::string alert_obj = extract_json_object(json_str, "alert");
    if (!alert_obj.empty()) {
        config.alert.false_trigger_threshold = to_double(extract_json_value(alert_obj, "false_trigger_threshold"), 0.1);
        config.alert.sensitivity_decrease_threshold = to_double(extract_json_value(alert_obj, "sensitivity_decrease_threshold"), 0.2);
        config.alert.baseline_sensitivity = to_double(extract_json_value(alert_obj, "baseline_sensitivity"), 0.8);
        config.alert.detection_magnitude_threshold = to_double(extract_json_value(alert_obj, "detection_magnitude_threshold"), 3.0);
        config.alert.history_window_seconds = to_int(extract_json_value(alert_obj, "history_window_seconds"), 3600);
    }

    std::string svc_obj = extract_json_object(json_str, "services");
    if (!svc_obj.empty()) {
        config.services.udp_port = to_int(extract_json_value(svc_obj, "udp_port"), 12345);
        config.services.http_port = to_int(extract_json_value(svc_obj, "http_port"), 8080);
        config.services.clickhouse_host = extract_json_value(svc_obj, "clickhouse_host");
        if (config.services.clickhouse_host.empty()) config.services.clickhouse_host = "127.0.0.1";
        config.services.clickhouse_port = to_int(extract_json_value(svc_obj, "clickhouse_port"), 8123);
        config.services.clickhouse_database = extract_json_value(svc_obj, "clickhouse_database");
        if (config.services.clickhouse_database.empty()) config.services.clickhouse_database = "seismograph";
        config.services.mqtt_host = extract_json_value(svc_obj, "mqtt_host");
        if (config.services.mqtt_host.empty()) config.services.mqtt_host = "127.0.0.1";
        config.services.mqtt_port = to_int(extract_json_value(svc_obj, "mqtt_port"), 1883);
        config.services.device_id = extract_json_value(svc_obj, "device_id");
        if (config.services.device_id.empty()) config.services.device_id = "device_001";
    }

    return true;
}

}
