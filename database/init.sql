CREATE DATABASE IF NOT EXISTS seismograph;

USE seismograph;

CREATE TABLE IF NOT EXISTS sensor_data (
    timestamp DateTime64(3) DEFAULT now64(3),
    device_id String,
    column_displacement_x Float64,
    column_displacement_y Float64,
    column_displacement_z Float64,
    column_angle_x Float64,
    column_angle_y Float64,
    seismic_accel_x Float64,
    seismic_accel_y Float64,
    seismic_accel_z Float64,
    dragon_triggers Array(UInt8),
    magnitude Float64,
    epicenter_distance Float64,
    is_triggered UInt8,
    trigger_direction Int32
) ENGINE = MergeTree()
PARTITION BY toYYYYMM(timestamp)
ORDER BY (device_id, timestamp)
TTL timestamp + INTERVAL 1 YEAR;

CREATE TABLE IF NOT EXISTS sensitivity_analysis (
    timestamp DateTime64(3) DEFAULT now64(3),
    device_id String,
    test_magnitude Float64,
    test_distance Float64,
    detection_probability Float64,
    false_alarm_rate Float64,
    response_time_ms Float64,
    trigger_direction Int32,
    column_stiffness Float64,
    damping_coefficient Float64
) ENGINE = MergeTree()
PARTITION BY toYYYYMM(timestamp)
ORDER BY (device_id, timestamp, test_magnitude, test_distance)
TTL timestamp + INTERVAL 1 YEAR;

CREATE TABLE IF NOT EXISTS alerts (
    timestamp DateTime64(3) DEFAULT now64(3),
    device_id String,
    alert_type String,
    alert_level String,
    message String,
    details String
) ENGINE = MergeTree()
PARTITION BY toYYYYMM(timestamp)
ORDER BY (device_id, timestamp, alert_type)
TTL timestamp + INTERVAL 1 YEAR;

CREATE TABLE IF NOT EXISTS simulation_runs (
    timestamp DateTime64(3) DEFAULT now64(3),
    device_id String,
    simulation_id String,
    start_time DateTime64(3),
    end_time DateTime64(3),
    parameters String,
    result String
) ENGINE = MergeTree()
PARTITION BY toYYYYMM(timestamp)
ORDER BY (device_id, simulation_id)
TTL timestamp + INTERVAL 1 YEAR;

CREATE TABLE IF NOT EXISTS sensor_metrics (
    timestamp DateTime64(3) DEFAULT now64(3),
    device_id String,
    metric_name String,
    metric_value Float64,
    window_start DateTime64(3),
    window_end DateTime64(3)
) ENGINE = MergeTree()
PARTITION BY toYYYYMM(timestamp)
ORDER BY (device_id, metric_name, timestamp)
TTL timestamp + INTERVAL 1 YEAR;

CREATE MATERIALIZED VIEW IF NOT EXISTS sensor_data_hourly_mv
TO seismograph.sensor_metrics
AS SELECT
    timestamp,
    device_id,
    'avg_displacement' AS metric_name,
    sqrt(pow(avg(column_displacement_x), 2) + pow(avg(column_displacement_y), 2) + pow(avg(column_displacement_z), 2)) AS metric_value,
    date_trunc('hour', timestamp) AS window_start,
    date_add('hour', 1, date_trunc('hour', timestamp)) AS window_end
FROM seismograph.sensor_data
GROUP BY device_id, date_trunc('hour', timestamp), timestamp;
