#pragma once
#include "common.h"
#include <vector>
#include <cmath>
#include <random>

namespace seismograph {

class ColumnSimulation {
public:
    ColumnSimulation();
    
    void set_column_params(const ColumnParams& params);
    const ColumnParams& get_column_params() const;
    
    SimulationResult simulate(const SeismicWaveParams& wave_params, double dt = 0.001);
    
    std::vector<std::pair<double, SimulationResult>> simulate_timeseries(
        const SeismicWaveParams& wave_params, 
        double duration, 
        double dt = 0.001
    );
    
    double calculate_seismic_acceleration(double magnitude, double distance, double time);
    double calculate_wave_amplitude(double magnitude, double distance);
    double calculate_response_spectrum(double frequency, double damping);
    
    void reset();

private:
    ColumnParams column_params_;
    
    double state_disp_x_;
    double state_disp_y_;
    double state_disp_z_;
    double state_angle_x_;
    double state_angle_y_;
    double state_vel_x_;
    double state_vel_y_;
    double state_vel_z_;
    double state_ang_vel_x_;
    double state_ang_vel_y_;
    
    std::mt19937 rng_;
    
    void update_dynamics(double seismic_accel_x, double seismic_accel_y, 
                        double seismic_accel_z, double dt);
    
    int determine_trigger_direction(double angle_x, double angle_y);
    
    void update_dragon_triggers(SimulationResult& result);
    
    double calculate_equivalent_force(double seismic_accel);
    double calculate_restoring_force(double displacement, double stiffness);
    double calculate_damping_force(double velocity, double damping);
    double calculate_friction_force(double velocity, double normal_force);
    
    double generate_seismic_wave(double time, double magnitude, double distance);
    double generate_p_wave(double time, double distance);
    double generate_s_wave(double time, double distance);
    double generate_rayleigh_wave(double time, double distance);
    
    bool check_static_equilibrium();
    double calculate_stability_margin();
};

}
