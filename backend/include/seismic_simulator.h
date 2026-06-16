#pragma once

#include "common.h"
#include "message_queue.h"
#include "config_loader.h"
#include "column_simulation.h"
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>

namespace seismograph {

class SeismicSimulator {
public:
    SeismicSimulator();
    ~SeismicSimulator();

    bool start(std::shared_ptr<MessageQueue<SensorMessage>> input_queue,
               std::shared_ptr<MessageQueue<SimulationMessage>> output_queue);
    void stop();
    bool is_running() const;

    void set_dynamics_config(const DynamicsConfig& config);
    void set_soil_type(SoilType type);

    SimulationResult simulate(const SeismicWaveParams& params, double time_step = 0.001);
    std::vector<std::pair<double, SimulationResult>> simulate_timeseries(
        const SeismicWaveParams& params, double duration, double output_dt);

    struct Stats {
        uint64_t processed_messages;
        uint64_t total_simulations;
        uint64_t triggered_count;
        uint64_t last_sim_time;
        double avg_processing_ms;
    };

    Stats get_stats() const;

private:
    void worker_loop();
    void process_sensor_message(const SensorMessage& msg);

    std::atomic<bool> running_;
    std::thread worker_thread_;

    std::shared_ptr<MessageQueue<SensorMessage>> input_queue_;
    std::shared_ptr<MessageQueue<SimulationMessage>> output_queue_;

    std::unique_ptr<ColumnSimulation> simulation_;
    std::mutex simulation_mutex_;

    DynamicsConfig dynamics_config_;

    mutable std::mutex stats_mutex_;
    Stats stats_;
};

}
