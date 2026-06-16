#pragma once
#include <string>
#include <vector>
#include <chrono>
#include <array>

namespace seismograph {

struct SensorData {
    uint64_t timestamp;
    std::string device_id;
    double column_disp_x;
    double column_disp_y;
    double column_disp_z;
    double column_angle_x;
    double column_angle_y;
    double seismic_accel_x;
    double seismic_accel_y;
    double seismic_accel_z;
    std::array<uint8_t, 8> dragon_triggers;
    double magnitude;
    double epicenter_distance;
    uint8_t is_triggered;
    int32_t trigger_direction;
};

enum class SoilType {
    ROCK_HARD = 0,
    SOIL_MEDIUM = 1,
    SOIL_SOFT = 2,
    SOIL_VERY_SOFT = 3
};

struct SeismicWaveParams {
    double magnitude;
    double epicenter_distance;
    double depth;
    double frequency;
    double duration;
    SoilType soil_type;
};

struct ColumnParams {
    double mass;
    double height;
    double base_radius;
    double top_radius;
    double moment_of_inertia;
    double stiffness;
    double damping_coefficient;
    double static_friction;
    double dynamic_friction;
    double trigger_threshold;
};

struct SimulationResult {
    double displacement_x;
    double displacement_y;
    double displacement_z;
    double angle_x;
    double angle_y;
    double angular_velocity_x;
    double angular_velocity_y;
    double velocity_x;
    double velocity_y;
    double velocity_z;
    bool is_triggered;
    int trigger_direction;
    double response_time_ms;
    std::array<uint8_t, 8> dragon_triggers;
};

struct SensitivityResult {
    double test_magnitude;
    double test_distance;
    SoilType soil_type;
    double detection_probability;
    double false_alarm_rate;
    double response_time_ms;
    int trigger_direction;
    double column_stiffness;
    double damping_coefficient;
    double soil_amplification_factor;
};

struct Alert {
    uint64_t timestamp;
    std::string device_id;
    std::string alert_type;
    std::string alert_level;
    std::string message;
    std::string details;
};

enum class AlertType {
    FALSE_TRIGGER,
    SENSITIVITY_DECREASE,
    SENSOR_FAILURE,
    CALIBRATION_NEEDED
};

enum class AlertLevel {
    INFO,
    WARNING,
    ERROR,
    CRITICAL
};

inline uint64_t current_timestamp_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

}
