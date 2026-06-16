#pragma once

#include "common.h"
#include <memory>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>

#ifdef SEISMOGRAPH_USE_BOOST_LOCKFREE
#include <boost/lockfree/queue.hpp>
#endif

namespace seismograph {

template <typename T>
class MessageQueue {
public:
    explicit MessageQueue(size_t capacity = 4096) 
        : capacity_(capacity)
        , dropped_count_(0)
#ifdef SEISMOGRAPH_USE_BOOST_LOCKFREE
        , queue_(capacity)
#endif
    {
    }

    ~MessageQueue() = default;

    bool push(const T& item) {
#ifdef SEISMOGRAPH_USE_BOOST_LOCKFREE
        if (queue_.push(item)) {
            return true;
        } else {
            dropped_count_++;
            return false;
        }
#else
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.size() >= capacity_) {
            dropped_count_++;
            return false;
        }
        queue_.push(item);
        return true;
#endif
    }

    bool pop(T& item) {
#ifdef SEISMOGRAPH_USE_BOOST_LOCKFREE
        return queue_.pop(item);
#else
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return false;
        item = queue_.front();
        queue_.pop();
        return true;
#endif
    }

    bool empty() const {
#ifdef SEISMOGRAPH_USE_BOOST_LOCKFREE
        return queue_.empty();
#else
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
#endif
    }

    size_t dropped_count() const { return dropped_count_.load(); }

    size_t approximate_size() const {
#ifdef SEISMOGRAPH_USE_BOOST_LOCKFREE
        return 0;
#else
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
#endif
    }

    void reset_dropped_count() { dropped_count_ = 0; }

private:
    size_t capacity_;
    std::atomic<size_t> dropped_count_;

#ifdef SEISMOGRAPH_USE_BOOST_LOCKFREE
    boost::lockfree::queue<T> queue_;
#else
    mutable std::mutex mutex_;
    std::queue<T> queue_;
#endif
};

struct SensorMessage {
    SensorData data;
    uint64_t received_at;
};

struct SimulationMessage {
    SimulationResult result;
    SensorData source_data;
    uint64_t computed_at;
};

struct AlertMessage {
    Alert alert;
    uint64_t generated_at;
};

struct SensitivityMessage {
    SensitivityResult result;
    std::string analysis_type;
    uint64_t computed_at;
};

}
