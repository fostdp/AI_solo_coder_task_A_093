#include "column_simulation.h"
#include <cmath>
#include <algorithm>
#include <random>

namespace seismograph {

ColumnSimulation::ColumnSimulation()
    : rng_(std::random_device{}()) {
    column_params_.mass = 1000.0;
    column_params_.height = 2.5;
    column_params_.base_radius = 0.3;
    column_params_.top_radius = 0.15;
    column_params_.moment_of_inertia = (1.0 / 3.0) * column_params_.mass * column_params_.height * column_params_.height;
    column_params_.stiffness = 50000.0;
    column_params_.damping_coefficient = 500.0;
    column_params_.static_friction = 0.6;
    column_params_.dynamic_friction = 0.4;
    column_params_.trigger_threshold = 0.05;
    reset();
}

void ColumnSimulation::set_column_params(const ColumnParams& params) {
    column_params_ = params;
    column_params_.moment_of_inertia = (1.0 / 3.0) * params.mass * params.height * params.height;
}

const ColumnParams& ColumnSimulation::get_column_params() const {
    return column_params_;
}

void ColumnSimulation::reset() {
    state_disp_x_ = 0.0;
    state_disp_y_ = 0.0;
    state_disp_z_ = 0.0;
    state_angle_x_ = 0.0;
    state_angle_y_ = 0.0;
    state_vel_x_ = 0.0;
    state_vel_y_ = 0.0;
    state_vel_z_ = 0.0;
    state_ang_vel_x_ = 0.0;
    state_ang_vel_y_ = 0.0;
}

double ColumnSimulation::calculate_seismic_acceleration(double magnitude, double distance, double time) {
    double amplitude = calculate_wave_amplitude(magnitude, distance);
    double wave = generate_seismic_wave(time, magnitude, distance);
    return amplitude * wave;
}

double ColumnSimulation::calculate_wave_amplitude(double magnitude, double distance) {
    double a = 0.01 * std::pow(10, 0.5 * magnitude);
    double attenuation = std::exp(-0.001 * distance);
    double geometric_spreading = 1.0 / std::sqrt(distance + 1.0);
    return a * attenuation * geometric_spreading;
}

double ColumnSimulation::calculate_response_spectrum(double frequency, double damping) {
    double omega = 2.0 * M_PI * frequency;
    double omega_n = std::sqrt(column_params_.stiffness / column_params_.mass);
    double r = omega / omega_n;
    double denominator = std::sqrt(std::pow(1.0 - r * r, 2) + std::pow(2.0 * damping * r, 2));
    return 1.0 / denominator;
}

double ColumnSimulation::generate_seismic_wave(double time, double magnitude, double distance) {
    double p_wave = generate_p_wave(time, distance);
    double s_wave = generate_s_wave(time, distance);
    double rayleigh = generate_rayleigh_wave(time, distance);
    
    std::normal_distribution<double> noise(0.0, 0.1);
    double random_noise = noise(rng_);
    
    return 0.3 * p_wave + 0.5 * s_wave + 0.2 * rayleigh + random_noise;
}

double ColumnSimulation::generate_p_wave(double time, double distance) {
    double vp = 6.0;
    double arrival_time = distance / vp;
    if (time < arrival_time) return 0.0;
    
    double t = time - arrival_time;
    double freq = 5.0;
    double envelope = std::exp(-0.5 * t) * t;
    return envelope * std::sin(2.0 * M_PI * freq * t);
}

double ColumnSimulation::generate_s_wave(double time, double distance) {
    double vs = 3.5;
    double arrival_time = distance / vs;
    if (time < arrival_time) return 0.0;
    
    double t = time - arrival_time;
    double freq = 3.0;
    double envelope = std::exp(-0.3 * t) * std::sqrt(t);
    return envelope * std::sin(2.0 * M_PI * freq * t + M_PI / 4.0);
}

double ColumnSimulation::generate_rayleigh_wave(double time, double distance) {
    double vr = 3.0;
    double arrival_time = distance / vr;
    if (time < arrival_time) return 0.0;
    
    double t = time - arrival_time;
    double freq = 1.5;
    double envelope = std::exp(-0.2 * t) * std::pow(t, 0.25);
    return envelope * std::sin(2.0 * M_PI * freq * t);
}

void ColumnSimulation::update_dynamics(double seismic_accel_x, double seismic_accel_y,
                                       double seismic_accel_z, double dt) {
    double& m = column_params_.mass;
    double& k = column_params_.stiffness;
    double& c = column_params_.damping_coefficient;
    double& I = column_params_.moment_of_inertia;
    double& h = column_params_.height;
    double& g = 9.81;
    
    double inertia_force_x = m * seismic_accel_x;
    double inertia_force_y = m * seismic_accel_y;
    double inertia_force_z = m * seismic_accel_z;
    
    double restoring_force_x = calculate_restoring_force(state_disp_x_, k);
    double restoring_force_y = calculate_restoring_force(state_disp_y_, k);
    double restoring_force_z = calculate_restoring_force(state_disp_z_, k * 0.5);
    
    double damping_force_x = calculate_damping_force(state_vel_x_, c);
    double damping_force_y = calculate_damping_force(state_vel_y_, c);
    double damping_force_z = calculate_damping_force(state_vel_z_, c * 0.5);
    
    double normal_force = m * g + inertia_force_z;
    double friction_force_x = calculate_friction_force(state_vel_x_, normal_force);
    double friction_force_y = calculate_friction_force(state_vel_y_, normal_force);
    
    double net_force_x = inertia_force_x - restoring_force_x - damping_force_x - friction_force_x;
    double net_force_y = inertia_force_y - restoring_force_y - damping_force_y - friction_force_y;
    double net_force_z = inertia_force_z - restoring_force_z - damping_force_z;
    
    double accel_x = net_force_x / m;
    double accel_y = net_force_y / m;
    double accel_z = net_force_z / m;
    
    double gravity_torque_x = m * g * h * state_angle_x_;
    double gravity_torque_y = m * g * h * state_angle_y_;
    
    double inertia_torque_x = inertia_force_y * h * 0.5;
    double inertia_torque_y = -inertia_force_x * h * 0.5;
    
    double damping_torque_x = c * h * h * state_ang_vel_x_;
    double damping_torque_y = c * h * h * state_ang_vel_y_;
    
    double net_torque_x = inertia_torque_x - gravity_torque_x - damping_torque_x;
    double net_torque_y = inertia_torque_y - gravity_torque_y - damping_torque_y;
    
    double ang_accel_x = net_torque_x / I;
    double ang_accel_y = net_torque_y / I;
    
    state_vel_x_ += accel_x * dt;
    state_vel_y_ += accel_y * dt;
    state_vel_z_ += accel_z * dt;
    state_disp_x_ += state_vel_x_ * dt;
    state_disp_y_ += state_vel_y_ * dt;
    state_disp_z_ += state_vel_z_ * dt;
    
    state_ang_vel_x_ += ang_accel_x * dt;
    state_ang_vel_y_ += ang_accel_y * dt;
    state_angle_x_ += state_ang_vel_x_ * dt;
    state_angle_y_ += state_ang_vel_y_ * dt;
    
    double max_angle = std::atan2(column_params_.base_radius, h);
    state_angle_x_ = std::clamp(state_angle_x_, -max_angle, max_angle);
    state_angle_y_ = std::clamp(state_angle_y_, -max_angle, max_angle);
}

int ColumnSimulation::determine_trigger_direction(double angle_x, double angle_y) {
    double angle = std::atan2(angle_y, angle_x);
    if (angle < 0) angle += 2.0 * M_PI;
    
    int direction = static_cast<int>(std::round(angle / (M_PI / 4.0))) % 8;
    return direction;
}

void ColumnSimulation::update_dragon_triggers(SimulationResult& result) {
    double threshold = column_params_.trigger_threshold;
    double total_angle = std::sqrt(state_angle_x_ * state_angle_x_ + state_angle_y_ * state_angle_y_);
    
    for (int i = 0; i < 8; ++i) {
        double dir_angle = i * (M_PI / 4.0);
        double dir_x = std::cos(dir_angle);
        double dir_y = std::sin(dir_angle);
        
        double projection = state_angle_x_ * dir_x + state_angle_y_ * dir_y;
        result.dragon_triggers[i] = (projection > threshold && total_angle > threshold * 0.8) ? 1 : 0;
    }
}

double ColumnSimulation::calculate_equivalent_force(double seismic_accel) {
    return column_params_.mass * seismic_accel;
}

double ColumnSimulation::calculate_restoring_force(double displacement, double stiffness) {
    return stiffness * displacement + 0.01 * stiffness * displacement * displacement * displacement;
}

double ColumnSimulation::calculate_damping_force(double velocity, double damping) {
    return damping * velocity + 0.05 * damping * velocity * std::abs(velocity);
}

double ColumnSimulation::calculate_friction_force(double velocity, double normal_force) {
    double friction_force;
    if (std::abs(velocity) < 0.001) {
        friction_force = column_params_.static_friction * normal_force * std::tanh(velocity * 1000.0);
    } else {
        friction_force = column_params_.dynamic_friction * normal_force * std::tanh(velocity * 10.0);
    }
    return friction_force;
}

bool ColumnSimulation::check_static_equilibrium() {
    double max_angle = std::atan2(column_params_.base_radius, column_params_.height);
    double current_angle = std::sqrt(state_angle_x_ * state_angle_x_ + state_angle_y_ * state_angle_y_);
    return current_angle < max_angle * 0.9;
}

double ColumnSimulation::calculate_stability_margin() {
    double max_angle = std::atan2(column_params_.base_radius, column_params_.height);
    double current_angle = std::sqrt(state_angle_x_ * state_angle_x_ + state_angle_y_ * state_angle_y_);
    return 1.0 - (current_angle / max_angle);
}

SimulationResult ColumnSimulation::simulate(const SeismicWaveParams& wave_params, double dt) {
    SimulationResult result;
    reset();
    
    double duration = wave_params.duration > 0 ? wave_params.duration : 10.0;
    double time = 0.0;
    double trigger_time = -1.0;
    double trigger_angle_x = 0.0;
    double trigger_angle_y = 0.0;
    
    std::uniform_real_distribution<double> angle_dist(-0.2, 0.2);
    double incidence_angle = angle_dist(rng_);
    
    while (time < duration) {
        double base_accel = calculate_seismic_acceleration(
            wave_params.magnitude, wave_params.epicenter_distance, time
        );
        
        double accel_x = base_accel * std::cos(incidence_angle);
        double accel_y = base_accel * std::sin(incidence_angle);
        double accel_z = base_accel * 0.3;
        
        update_dynamics(accel_x, accel_y, accel_z, dt);
        
        double total_angle = std::sqrt(state_angle_x_ * state_angle_x_ + state_angle_y_ * state_angle_y_);
        if (total_angle > column_params_.trigger_threshold && trigger_time < 0) {
            trigger_time = time;
            trigger_angle_x = state_angle_x_;
            trigger_angle_y = state_angle_y_;
        }
        
        time += dt;
    }
    
    result.displacement_x = state_disp_x_;
    result.displacement_y = state_disp_y_;
    result.displacement_z = state_disp_z_;
    result.angle_x = state_angle_x_;
    result.angle_y = state_angle_y_;
    result.angular_velocity_x = state_ang_vel_x_;
    result.angular_velocity_y = state_ang_vel_y_;
    result.velocity_x = state_vel_x_;
    result.velocity_y = state_vel_y_;
    result.velocity_z = state_vel_z_;
    
    update_dragon_triggers(result);
    
    if (trigger_time > 0) {
        result.is_triggered = true;
        result.trigger_direction = determine_trigger_direction(trigger_angle_x, trigger_angle_y);
        result.response_time_ms = trigger_time * 1000.0;
    } else {
        result.is_triggered = false;
        result.trigger_direction = -1;
        result.response_time_ms = duration * 1000.0;
    }
    
    return result;
}

std::vector<std::pair<double, SimulationResult>> ColumnSimulation::simulate_timeseries(
    const SeismicWaveParams& wave_params, double duration, double dt) {
    
    std::vector<std::pair<double, SimulationResult>> timeseries;
    reset();
    
    double time = 0.0;
    double trigger_time = -1.0;
    
    std::uniform_real_distribution<double> angle_dist(-0.2, 0.2);
    double incidence_angle = angle_dist(rng_);
    
    while (time < duration) {
        double base_accel = calculate_seismic_acceleration(
            wave_params.magnitude, wave_params.epicenter_distance, time
        );
        
        double accel_x = base_accel * std::cos(incidence_angle);
        double accel_y = base_accel * std::sin(incidence_angle);
        double accel_z = base_accel * 0.3;
        
        update_dynamics(accel_x, accel_y, accel_z, dt);
        
        SimulationResult result;
        result.displacement_x = state_disp_x_;
        result.displacement_y = state_disp_y_;
        result.displacement_z = state_disp_z_;
        result.angle_x = state_angle_x_;
        result.angle_y = state_angle_y_;
        result.angular_velocity_x = state_ang_vel_x_;
        result.angular_velocity_y = state_ang_vel_y_;
        result.velocity_x = state_vel_x_;
        result.velocity_y = state_vel_y_;
        result.velocity_z = state_vel_z_;
        
        update_dragon_triggers(result);
        
        double total_angle = std::sqrt(state_angle_x_ * state_angle_x_ + state_angle_y_ * state_angle_y_);
        if (total_angle > column_params_.trigger_threshold && trigger_time < 0) {
            trigger_time = time;
            result.is_triggered = true;
            result.trigger_direction = determine_trigger_direction(state_angle_x_, state_angle_y_);
            result.response_time_ms = trigger_time * 1000.0;
        } else if (trigger_time > 0) {
            result.is_triggered = true;
            result.trigger_direction = determine_trigger_direction(state_angle_x_, state_angle_y_);
            result.response_time_ms = trigger_time * 1000.0;
        } else {
            result.is_triggered = false;
            result.trigger_direction = -1;
            result.response_time_ms = time * 1000.0;
        }
        
        timeseries.emplace_back(time, result);
        time += dt;
    }
    
    return timeseries;
}

}
