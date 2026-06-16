#include "column_simulation.h"
#include <cmath>
#include <algorithm>
#include <random>

namespace seismograph {

ColumnSimulation::ColumnSimulation()
    : rng_(std::random_device{}()),
      soil_type_(SoilType::SOIL_MEDIUM) {
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

void ColumnSimulation::set_soil_type(SoilType type) {
    soil_type_ = type;
}

ColumnSimulation::SoilType ColumnSimulation::get_soil_type() const {
    return soil_type_;
}

double ColumnSimulation::calculate_soil_amplification(double frequency) const {
    struct SoilParams {
        double Tg;
        double Amax;
        double k;
    };

    static const SoilParams soil_params[] = {
        {0.20, 0.90, 0.95},
        {0.35, 1.00, 1.00},
        {0.45, 1.10, 1.05},
        {0.65, 1.20, 1.10}
    };

    const SoilParams& sp = soil_params[static_cast<int>(soil_type_)];
    double T = 1.0 / std::max(frequency, 0.1);
    double amp;

    if (T < 0.1) {
        amp = 0.45 + 5.5 * (T - 0.1);
    } else if (T < sp.Tg) {
        amp = sp.Amax;
    } else if (T < 5.0) {
        amp = sp.Amax * std::pow(sp.Tg / T, 0.9);
    } else {
        amp = sp.Amax * std::pow(sp.Tg / 5.0, 0.9) * (5.0 / T);
    }

    return amp * sp.k;
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

    double freq_p = 5.0;
    double freq_s = 3.0;
    double freq_r = 1.5;
    double weighted_amp = (0.3 * calculate_soil_amplification(freq_p)
                         + 0.5 * calculate_soil_amplification(freq_s)
                         + 0.2 * calculate_soil_amplification(freq_r));

    return amplitude * wave * weighted_amp;
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
    double g = 9.81;

    double omega_n = std::sqrt(k / m);
    double max_disp = column_params_.base_radius * 0.8;
    double max_angle = std::atan2(column_params_.base_radius, h);
    double k_penalty = k * 100.0;
    double c_penalty = c * 10.0;
    double alpha_rayleigh = 0.02;
    double beta_rayleigh = 0.001;

    double inertia_force_x = m * seismic_accel_x;
    double inertia_force_y = m * seismic_accel_y;
    double inertia_force_z = m * seismic_accel_z;

    double restoring_force_x = calculate_restoring_force(state_disp_x_, k);
    double restoring_force_y = calculate_restoring_force(state_disp_y_, k);
    double restoring_force_z = calculate_restoring_force(state_disp_z_, k * 0.5);

    double damping_force_x = calculate_damping_force(state_vel_x_, c);
    double damping_force_y = calculate_damping_force(state_vel_y_, c);
    double damping_force_z = calculate_damping_force(state_vel_z_, c * 0.5);

    double rayleigh_force_x = calculate_rayleigh_damping(
        alpha_rayleigh, beta_rayleigh, state_vel_x_, state_disp_x_, omega_n);
    double rayleigh_force_y = calculate_rayleigh_damping(
        alpha_rayleigh, beta_rayleigh, state_vel_y_, state_disp_y_, omega_n);
    double rayleigh_force_z = calculate_rayleigh_damping(
        alpha_rayleigh, beta_rayleigh, state_vel_z_, state_disp_z_, omega_n);

    double normal_force = std::max(m * g + inertia_force_z, 0.0);
    double friction_force_x = calculate_stribeck_friction(state_vel_x_, normal_force);
    double friction_force_y = calculate_stribeck_friction(state_vel_y_, normal_force);

    double penalty_x = calculate_penalty_force(state_disp_x_, max_disp, k_penalty, c_penalty, state_vel_x_);
    double penalty_y = calculate_penalty_force(state_disp_y_, max_disp, k_penalty, c_penalty, state_vel_y_);
    double penalty_z = calculate_penalty_force(state_disp_z_, max_disp * 0.5, k_penalty, c_penalty, state_vel_z_);

    double net_force_x = inertia_force_x - restoring_force_x - damping_force_x
                        - friction_force_x - rayleigh_force_x + penalty_x;
    double net_force_y = inertia_force_y - restoring_force_y - damping_force_y
                        - friction_force_y - rayleigh_force_y + penalty_y;
    double net_force_z = inertia_force_z - restoring_force_z - damping_force_z
                        - rayleigh_force_z + penalty_z;

    double accel_x = net_force_x / m;
    double accel_y = net_force_y / m;
    double accel_z = net_force_z / m;

    state_vel_x_ += accel_x * dt;
    state_vel_y_ += accel_y * dt;
    state_vel_z_ += accel_z * dt;
    state_disp_x_ += state_vel_x_ * dt;
    state_disp_y_ += state_vel_y_ * dt;
    state_disp_z_ += state_vel_z_ * dt;

    double gravity_torque_x = m * g * h * state_angle_x_;
    double gravity_torque_y = m * g * h * state_angle_y_;

    double inertia_torque_x = inertia_force_y * h * 0.5;
    double inertia_torque_y = -inertia_force_x * h * 0.5;

    double damping_torque_x = c * h * h * state_ang_vel_x_;
    double damping_torque_y = c * h * h * state_ang_vel_y_;

    double rayleigh_torque_x = calculate_rayleigh_damping(
        alpha_rayleigh, beta_rayleigh, state_ang_vel_x_, state_angle_x_, omega_n);
    double rayleigh_torque_y = calculate_rayleigh_damping(
        alpha_rayleigh, beta_rayleigh, state_ang_vel_y_, state_angle_y_, omega_n);

    double penalty_angle_x = calculate_penalty_force(state_angle_x_, max_angle, k_penalty * h * h, c_penalty * h * h, state_ang_vel_x_);
    double penalty_angle_y = calculate_penalty_force(state_angle_y_, max_angle, k_penalty * h * h, c_penalty * h * h, state_ang_vel_y_);

    double net_torque_x = inertia_torque_x - gravity_torque_x - damping_torque_x
                         - rayleigh_torque_x + penalty_angle_x;
    double net_torque_y = inertia_torque_y - gravity_torque_y - damping_torque_y
                         - rayleigh_torque_y + penalty_angle_y;

    double ang_accel_x = net_torque_x / I;
    double ang_accel_y = net_torque_y / I;

    state_ang_vel_x_ += ang_accel_x * dt;
    state_ang_vel_y_ += ang_accel_y * dt;
    state_angle_x_ += state_ang_vel_x_ * dt;
    state_angle_y_ += state_ang_vel_y_ * dt;

    double soft_limit = max_angle * 0.999;
    if (std::abs(state_angle_x_) > soft_limit) {
        state_angle_x_ = std::copysign(soft_limit, state_angle_x_);
        state_ang_vel_x_ *= 0.1;
    }
    if (std::abs(state_angle_y_) > soft_limit) {
        state_angle_y_ = std::copysign(soft_limit, state_angle_y_);
        state_ang_vel_y_ *= 0.1;
    }
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
    return calculate_stribeck_friction(velocity, normal_force);
}

double ColumnSimulation::calculate_stribeck_friction(double velocity, double normal_force) {
    const double mu_s = column_params_.static_friction;
    const double mu_d = column_params_.dynamic_friction;
    const double v_s = 0.001;
    const double v_c = 0.01;
    const double sigma = 0.0001;

    double mu_eff;
    double v_abs = std::abs(velocity);

    if (v_abs < v_s) {
        double alpha = v_abs / v_s;
        mu_eff = mu_d + (mu_s - mu_d) * (1.0 - alpha * alpha * (3.0 - 2.0 * alpha));
    } else if (v_abs < v_c) {
        double t = (v_abs - v_s) / (v_c - v_s);
        mu_eff = mu_s + (mu_d - mu_s) * t * t * (3.0 - 2.0 * t);
    } else {
        mu_eff = mu_d * (1.0 + sigma / v_abs);
    }

    double smooth_sign = std::tanh(velocity / (v_s * 0.5));
    return mu_eff * normal_force * smooth_sign;
}

double ColumnSimulation::calculate_penalty_force(double displacement, double boundary,
                                                  double penalty_stiffness, double penalty_damping,
                                                  double velocity) {
    double force = 0.0;
    double penetration = 0.0;

    if (displacement > boundary) {
        penetration = displacement - boundary;
    } else if (displacement < -boundary) {
        penetration = -displacement - boundary;
    }

    if (penetration > 0.0) {
        double k_p = penalty_stiffness;
        double c_p = penalty_damping;
        double penalty = k_p * penetration * penetration + c_p * std::abs(velocity) * penetration;

        if (displacement > 0) {
            force = -penalty;
        } else {
            force = penalty;
        }
    }

    return force;
}

double ColumnSimulation::calculate_rayleigh_damping(double stiffness_ratio, double mass_ratio,
                                                     double velocity, double displacement,
                                                     double natural_frequency) {
    double omega_n = natural_frequency > 0 ? natural_frequency : 1.0;
    double alpha = 2.0 * stiffness_ratio * omega_n;
    double beta = 2.0 * mass_ratio / omega_n;
    return alpha * displacement + beta * velocity;
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
    set_soil_type(wave_params.soil_type);
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
    set_soil_type(wave_params.soil_type);
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
