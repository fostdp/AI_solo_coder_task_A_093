#include "seismic_simulator.h"
#include <iostream>
#include <chrono>

namespace seismograph {

SeismicSimulator::SeismicSimulator()
    : running_(false)
    , input_queue_(nullptr)
    , output_queue_(nullptr)
    , simulation_(nullptr) {
    
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.processed_messages = 0;
    stats_.total_simulations = 0;
    stats_.triggered_count = 0;
    stats_.last_sim_time = 0;
    stats_.avg_processing_ms = 0.0;
}

SeismicSimulator::~SeismicSimulator() {
    stop();
}

bool SeismicSimulator::start(
    std::shared_ptr<MessageQueue<SensorMessage>> input_queue,
    std::shared_ptr<MessageQueue<SimulationMessage>> output_queue) {
    
    if (running_) return false;
    
    input_queue_ = input_queue;
    output_queue_ = output_queue;
    
    simulation_ = std::make_unique<ColumnSimulation>();
    
    auto params = ConfigLoader::to_column_params(dynamics_config_);
    simulation_->set_column_params(params);
    
    running_ = true;
    worker_thread_ = std::thread(&SeismicSimulator::worker_loop, this);
    
    std::cout << "[SeismicSimulator] Started" << std::endl;
    return true;
}

void SeismicSimulator::stop() {
    running_ = false;
    
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    
    auto s = get_stats();
    std::cout << "[SeismicSimulator] Stopped. Processed " 
              << s.processed_messages << " messages, "
              << s.triggered_count << " triggers" << std::endl;
}

bool SeismicSimulator::is_running() const {
    return running_;
}

void SeismicSimulator::set_dynamics_config(const DynamicsConfig& config) {
    dynamics_config_ = config;
    if (simulation_) {
        std::lock_guard<std::mutex> lock(simulation_mutex_);
        simulation_->set_column_params(ConfigLoader::to_column_params(config));
    }
}

void SeismicSimulator::set_soil_type(SoilType type) {
    if (simulation_) {
        std::lock_guard<std::mutex> lock(simulation_mutex_);
        simulation_->set_soil_type(type);
    }
}

SimulationResult SeismicSimulator::simulate(const SeismicWaveParams& params, double time_step) {
    std::lock_guard<std::mutex> lock(simulation_mutex_);
    return simulation_->simulate(params, time_step);
}

std::vector<std::pair<double, SimulationResult>> SeismicSimulator::simulate_timeseries(
    const SeismicWaveParams& params, double duration, double output_dt) {
    
    std::lock_guard<std::mutex> lock(simulation_mutex_);
    return simulation_->simulate_timeseries(params, duration, output_dt);
}

SeismicSimulator::Stats SeismicSimulator::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void SeismicSimulator::worker_loop() {
    SensorMessage msg;
    
    while (running_) {
        if (input_queue_ && input_queue_->pop(msg)) {
            process_sensor_message(msg);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

void SeismicSimulator::process_sensor_message(const SensorMessage& msg) {
    auto t0 = std::chrono::high_resolution_clock::now();
    
    SeismicWaveParams params;
    params.magnitude = msg.data.magnitude;
    params.epicenter_distance = msg.data.epicenter_distance;
    params.duration = 10.0;
    params.soil_type = SoilType::SOIL_MEDIUM;
    
    SimulationResult result;
    {
        std::lock_guard<std::mutex> lock(simulation_mutex_);
        result = simulation_->simulate(params, dynamics_config_.time_step);
    }
    
    auto t1 = std::chrono::high_resolution_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    
    if (output_queue_) {
        SimulationMessage sim_msg;
        sim_msg.result = result;
        sim_msg.source_data = msg.data;
        sim_msg.computed_at = current_timestamp_ms();
        output_queue_->push(sim_msg);
    }
    
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.processed_messages++;
        stats_.total_simulations++;
        if (result.is_triggered) stats_.triggered_count++;
        stats_.last_sim_time = current_timestamp_ms();
        double n = static_cast<double>(stats_.total_simulations);
        stats_.avg_processing_ms = stats_.avg_processing_ms * ((n - 1.0) / n) + elapsed_ms / n;
    }
}

}
