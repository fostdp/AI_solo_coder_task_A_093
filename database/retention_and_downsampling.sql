-- =====================================================
-- 地动仪系统 - ClickHouse 分层保留策略与降采样配置
-- 
-- 数据生命周期:
--   1. 原始数据 (sensor_data)            保留 30 天
--   2. 分钟级聚合 (sensor_data_1m)       保留 7 天
--   3. 5分钟级聚合 (sensor_data_5m)      保留 90 天
--   4. 小时级聚合 (sensor_data_1h)       保留 2 年
--   5. 日级聚合 (sensor_data_1d)         保留 5 年
--
--   灵敏度分析结果 (sensitivity_analysis) 保留 2 年
--   告警记录 (alerts)                    保留 5 年
--   仿真运行记录 (simulation_runs)       保留 5 年
-- =====================================================

USE seismograph;

-- =====================================================
-- 1. 修改主表 TTL (更短的原始数据保留期 + 分层 TTL)
-- =====================================================

ALTER TABLE sensor_data
    MODIFY TTL
        timestamp + INTERVAL 30 DAY,
        timestamp + INTERVAL 7 DAY
            TO VOLUME 'medium',
        timestamp + INTERVAL 30 DAY
            TO VOLUME 'archive'
    SETTINGS min_bytes_for_wide_part = '10M';

ALTER TABLE sensitivity_analysis
    MODIFY TTL timestamp + INTERVAL 2 YEAR;

ALTER TABLE alerts
    MODIFY TTL timestamp + INTERVAL 5 YEAR;

ALTER TABLE simulation_runs
    MODIFY TTL timestamp + INTERVAL 5 YEAR;

ALTER TABLE sensor_metrics
    MODIFY TTL timestamp + INTERVAL 1 YEAR;

-- =====================================================
-- 2. 分钟级降采样聚合表 (1 分钟粒度)
-- =====================================================

CREATE TABLE IF NOT EXISTS sensor_data_1m (
    window_start DateTime64(3),
    window_end   DateTime64(3),
    device_id    String,

    samples_count UInt64,

    avg_column_displacement_x Float64,
    avg_column_displacement_y Float64,
    avg_column_displacement_z Float64,
    max_column_displacement   Float64,

    avg_column_angle_x Float64,
    avg_column_angle_y Float64,
    max_column_angle   Float64,

    avg_seismic_accel_x Float64,
    avg_seismic_accel_y Float64,
    avg_seismic_accel_z Float64,
    rms_seismic_accel   Float64,
    peak_seismic_accel  Float64,

    trigger_count       UInt64,
    unique_directions   UInt64,

    avg_magnitude        Float64,
    min_magnitude        Float64,
    max_magnitude        Float64,
    avg_epicenter_distance Float64,

    dragon_trigger_count Array(UInt64)
)
ENGINE = SummingMergeTree()
PARTITION BY toYYYYMM(window_start)
ORDER BY (device_id, window_start)
TTL window_start + INTERVAL 7 DAY;

-- =====================================================
-- 3. 5 分钟级降采样聚合表
-- =====================================================

CREATE TABLE IF NOT EXISTS sensor_data_5m (
    window_start DateTime64(3),
    window_end   DateTime64(3),
    device_id    String,

    samples_count UInt64,

    avg_column_displacement_x Float64,
    avg_column_displacement_y Float64,
    avg_column_displacement_z Float64,
    max_column_displacement   Float64,

    avg_column_angle_x Float64,
    avg_column_angle_y Float64,
    max_column_angle   Float64,

    avg_seismic_accel_x Float64,
    avg_seismic_accel_y Float64,
    avg_seismic_accel_z Float64,
    rms_seismic_accel   Float64,
    peak_seismic_accel  Float64,

    trigger_count       UInt64,

    avg_magnitude        Float64,
    min_magnitude        Float64,
    max_magnitude        Float64,
    avg_epicenter_distance Float64,

    dragon_trigger_count Array(UInt64)
)
ENGINE = SummingMergeTree()
PARTITION BY toYYYYMM(window_start)
ORDER BY (device_id, window_start)
TTL window_start + INTERVAL 90 DAY;

-- =====================================================
-- 4. 小时级降采样聚合表
-- =====================================================

CREATE TABLE IF NOT EXISTS sensor_data_1h (
    window_start DateTime64(3),
    window_end   DateTime64(3),
    device_id    String,

    samples_count UInt64,

    avg_column_displacement_x Float64,
    avg_column_displacement_y Float64,
    avg_column_displacement_z Float64,
    max_column_displacement   Float64,

    avg_column_angle_x Float64,
    avg_column_angle_y Float64,
    max_column_angle   Float64,

    avg_seismic_accel_x Float64,
    avg_seismic_accel_y Float64,
    avg_seismic_accel_z Float64,
    rms_seismic_accel   Float64,
    peak_seismic_accel  Float64,

    trigger_count       UInt64,

    avg_magnitude        Float64,
    min_magnitude        Float64,
    max_magnitude        Float64,
    avg_epicenter_distance Float64,

    dragon_trigger_count Array(UInt64)
)
ENGINE = SummingMergeTree()
PARTITION BY toYYYYMM(window_start)
ORDER BY (device_id, window_start)
TTL window_start + INTERVAL 2 YEAR;

-- =====================================================
-- 5. 日级降采样聚合表
-- =====================================================

CREATE TABLE IF NOT EXISTS sensor_data_1d (
    window_start DateTime64(3),
    window_end   DateTime64(3),
    device_id    String,

    samples_count UInt64,

    avg_column_displacement_x Float64,
    avg_column_displacement_y Float64,
    avg_column_displacement_z Float64,
    max_column_displacement   Float64,

    avg_column_angle_x Float64,
    avg_column_angle_y Float64,
    max_column_angle   Float64,

    avg_seismic_accel_x Float64,
    avg_seismic_accel_y Float64,
    avg_seismic_accel_z Float64,
    rms_seismic_accel   Float64,
    peak_seismic_accel  Float64,

    trigger_count       UInt64,

    avg_magnitude        Float64,
    min_magnitude        Float64,
    max_magnitude        Float64,
    avg_epicenter_distance Float64,

    dragon_trigger_count Array(UInt64)
)
ENGINE = SummingMergeTree()
PARTITION BY toYYYYMM(window_start)
ORDER BY (device_id, window_start)
TTL window_start + INTERVAL 5 YEAR;

-- =====================================================
-- 6. 告警聚合表 (小时级统计各类告警数量)
-- =====================================================

CREATE TABLE IF NOT EXISTS alerts_hourly (
    window_start DateTime64(3),
    window_end   DateTime64(3),
    device_id    String,
    alert_type   String,
    alert_level  String,
    count        UInt64
)
ENGINE = SummingMergeTree()
PARTITION BY toYYYYMM(window_start)
ORDER BY (device_id, window_start, alert_type, alert_level)
TTL window_start + INTERVAL 5 YEAR;

-- =====================================================
-- 7. 物化视图 - 原始数据 → 1分钟聚合
-- =====================================================

DROP VIEW IF EXISTS sensor_data_to_1m_mv;

CREATE MATERIALIZED VIEW sensor_data_to_1m_mv
TO sensor_data_1m
AS SELECT
    date_trunc('minute', timestamp)                           AS window_start,
    date_add('minute', 1, date_trunc('minute', timestamp))   AS window_end,
    device_id,

    count()                                                    AS samples_count,

    avg(column_displacement_x)                                 AS avg_column_displacement_x,
    avg(column_displacement_y)                                 AS avg_column_displacement_y,
    avg(column_displacement_z)                                 AS avg_column_displacement_z,
    max(sqrt(pow(column_displacement_x, 2)
          + pow(column_displacement_y, 2)
          + pow(column_displacement_z, 2)))                    AS max_column_displacement,

    avg(column_angle_x)                                        AS avg_column_angle_x,
    avg(column_angle_y)                                        AS avg_column_angle_y,
    max(sqrt(pow(column_angle_x, 2) + pow(column_angle_y, 2))) AS max_column_angle,

    avg(seismic_accel_x)                                       AS avg_seismic_accel_x,
    avg(seismic_accel_y)                                       AS avg_seismic_accel_y,
    avg(seismic_accel_z)                                       AS avg_seismic_accel_z,
    sqrt(avg(pow(seismic_accel_x, 2))
       + avg(pow(seismic_accel_y, 2))
       + avg(pow(seismic_accel_z, 2)))                         AS rms_seismic_accel,
    max(sqrt(pow(seismic_accel_x, 2)
          + pow(seismic_accel_y, 2)
          + pow(seismic_accel_z, 2)))                          AS peak_seismic_accel,

    sum(is_triggered)                                          AS trigger_count,
    uniqExact(if(is_triggered, trigger_direction, NULL))       AS unique_directions,

    avg(magnitude)                                             AS avg_magnitude,
    min(magnitude)                                             AS min_magnitude,
    max(magnitude)                                             AS max_magnitude,
    avg(epicenter_distance)                                    AS avg_epicenter_distance,

    [sum(dragon_triggers[1]),
     sum(dragon_triggers[2]),
     sum(dragon_triggers[3]),
     sum(dragon_triggers[4]),
     sum(dragon_triggers[5]),
     sum(dragon_triggers[6]),
     sum(dragon_triggers[7]),
     sum(dragon_triggers[8])]                                  AS dragon_trigger_count
FROM sensor_data
GROUP BY device_id, date_trunc('minute', timestamp);

-- =====================================================
-- 8. 物化视图 - 1分钟 → 5分钟聚合
-- =====================================================

DROP VIEW IF EXISTS sensor_data_1m_to_5m_mv;

CREATE MATERIALIZED VIEW sensor_data_1m_to_5m_mv
TO sensor_data_5m
AS SELECT
    date_trunc('minute', window_start, 5)                           AS window_start,
    date_add('minute', 5, date_trunc('minute', window_start, 5))   AS window_end,
    device_id,

    sum(samples_count)                                               AS samples_count,

    sum(avg_column_displacement_x * samples_count) / sum(samples_count)  AS avg_column_displacement_x,
    sum(avg_column_displacement_y * samples_count) / sum(samples_count)  AS avg_column_displacement_y,
    sum(avg_column_displacement_z * samples_count) / sum(samples_count)  AS avg_column_displacement_z,
    max(max_column_displacement)                                     AS max_column_displacement,

    sum(avg_column_angle_x * samples_count) / sum(samples_count)     AS avg_column_angle_x,
    sum(avg_column_angle_y * samples_count) / sum(samples_count)     AS avg_column_angle_y,
    max(max_column_angle)                                            AS max_column_angle,

    sum(avg_seismic_accel_x * samples_count) / sum(samples_count)    AS avg_seismic_accel_x,
    sum(avg_seismic_accel_y * samples_count) / sum(samples_count)    AS avg_seismic_accel_y,
    sum(avg_seismic_accel_z * samples_count) / sum(samples_count)    AS avg_seismic_accel_z,
    sqrt(avg(pow(rms_seismic_accel, 2)))                             AS rms_seismic_accel,
    max(peak_seismic_accel)                                          AS peak_seismic_accel,

    sum(trigger_count)                                                AS trigger_count,

    sum(avg_magnitude * samples_count) / sum(samples_count)          AS avg_magnitude,
    min(min_magnitude)                                                AS min_magnitude,
    max(max_magnitude)                                                AS max_magnitude,
    sum(avg_epicenter_distance * samples_count) / sum(samples_count) AS avg_epicenter_distance,

    [sum(dragon_trigger_count[1]),
     sum(dragon_trigger_count[2]),
     sum(dragon_trigger_count[3]),
     sum(dragon_trigger_count[4]),
     sum(dragon_trigger_count[5]),
     sum(dragon_trigger_count[6]),
     sum(dragon_trigger_count[7]),
     sum(dragon_trigger_count[8])]                                    AS dragon_trigger_count
FROM sensor_data_1m
GROUP BY device_id, date_trunc('minute', window_start, 5);

-- =====================================================
-- 9. 物化视图 - 5分钟 → 1小时聚合
-- =====================================================

DROP VIEW IF EXISTS sensor_data_5m_to_1h_mv;

CREATE MATERIALIZED VIEW sensor_data_5m_to_1h_mv
TO sensor_data_1h
AS SELECT
    date_trunc('hour', window_start)                           AS window_start,
    date_add('hour', 1, date_trunc('hour', window_start))     AS window_end,
    device_id,

    sum(samples_count)                                          AS samples_count,

    sum(avg_column_displacement_x * samples_count) / sum(samples_count)  AS avg_column_displacement_x,
    sum(avg_column_displacement_y * samples_count) / sum(samples_count)  AS avg_column_displacement_y,
    sum(avg_column_displacement_z * samples_count) / sum(samples_count)  AS avg_column_displacement_z,
    max(max_column_displacement)                                AS max_column_displacement,

    sum(avg_column_angle_x * samples_count) / sum(samples_count) AS avg_column_angle_x,
    sum(avg_column_angle_y * samples_count) / sum(samples_count) AS avg_column_angle_y,
    max(max_column_angle)                                       AS max_column_angle,

    sum(avg_seismic_accel_x * samples_count) / sum(samples_count) AS avg_seismic_accel_x,
    sum(avg_seismic_accel_y * samples_count) / sum(samples_count) AS avg_seismic_accel_y,
    sum(avg_seismic_accel_z * samples_count) / sum(samples_count) AS avg_seismic_accel_z,
    sqrt(avg(pow(rms_seismic_accel, 2)))                        AS rms_seismic_accel,
    max(peak_seismic_accel)                                     AS peak_seismic_accel,

    sum(trigger_count)                                          AS trigger_count,

    sum(avg_magnitude * samples_count) / sum(samples_count)     AS avg_magnitude,
    min(min_magnitude)                                          AS min_magnitude,
    max(max_magnitude)                                          AS max_magnitude,
    sum(avg_epicenter_distance * samples_count) / sum(samples_count) AS avg_epicenter_distance,

    [sum(dragon_trigger_count[1]),
     sum(dragon_trigger_count[2]),
     sum(dragon_trigger_count[3]),
     sum(dragon_trigger_count[4]),
     sum(dragon_trigger_count[5]),
     sum(dragon_trigger_count[6]),
     sum(dragon_trigger_count[7]),
     sum(dragon_trigger_count[8])]                              AS dragon_trigger_count
FROM sensor_data_5m
GROUP BY device_id, date_trunc('hour', window_start);

-- =====================================================
-- 10. 物化视图 - 1小时 → 1天聚合
-- =====================================================

DROP VIEW IF EXISTS sensor_data_1h_to_1d_mv;

CREATE MATERIALIZED VIEW sensor_data_1h_to_1d_mv
TO sensor_data_1d
AS SELECT
    date_trunc('day', window_start)                           AS window_start,
    date_add('day', 1, date_trunc('day', window_start))      AS window_end,
    device_id,

    sum(samples_count)                                        AS samples_count,

    sum(avg_column_displacement_x * samples_count) / sum(samples_count)  AS avg_column_displacement_x,
    sum(avg_column_displacement_y * samples_count) / sum(samples_count)  AS avg_column_displacement_y,
    sum(avg_column_displacement_z * samples_count) / sum(samples_count)  AS avg_column_displacement_z,
    max(max_column_displacement)                              AS max_column_displacement,

    sum(avg_column_angle_x * samples_count) / sum(samples_count) AS avg_column_angle_x,
    sum(avg_column_angle_y * samples_count) / sum(samples_count) AS avg_column_angle_y,
    max(max_column_angle)                                     AS max_column_angle,

    sum(avg_seismic_accel_x * samples_count) / sum(samples_count) AS avg_seismic_accel_x,
    sum(avg_seismic_accel_y * samples_count) / sum(samples_count) AS avg_seismic_accel_y,
    sum(avg_seismic_accel_z * samples_count) / sum(samples_count) AS avg_seismic_accel_z,
    sqrt(avg(pow(rms_seismic_accel, 2)))                      AS rms_seismic_accel,
    max(peak_seismic_accel)                                   AS peak_seismic_accel,

    sum(trigger_count)                                        AS trigger_count,

    sum(avg_magnitude * samples_count) / sum(samples_count)   AS avg_magnitude,
    min(min_magnitude)                                        AS min_magnitude,
    max(max_magnitude)                                        AS max_magnitude,
    sum(avg_epicenter_distance * samples_count) / sum(samples_count) AS avg_epicenter_distance,

    [sum(dragon_trigger_count[1]),
     sum(dragon_trigger_count[2]),
     sum(dragon_trigger_count[3]),
     sum(dragon_trigger_count[4]),
     sum(dragon_trigger_count[5]),
     sum(dragon_trigger_count[6]),
     sum(dragon_trigger_count[7]),
     sum(dragon_trigger_count[8])]                            AS dragon_trigger_count
FROM sensor_data_1h
GROUP BY device_id, date_trunc('day', window_start);

-- =====================================================
-- 11. 物化视图 - 告警小时级聚合
-- =====================================================

DROP VIEW IF EXISTS alerts_to_hourly_mv;

CREATE MATERIALIZED VIEW alerts_to_hourly_mv
TO alerts_hourly
AS SELECT
    date_trunc('hour', timestamp)                         AS window_start,
    date_add('hour', 1, date_trunc('hour', timestamp))   AS window_end,
    device_id,
    alert_type,
    alert_level,
    count()                                                AS count
FROM alerts
GROUP BY device_id, date_trunc('hour', timestamp), alert_type, alert_level;

-- =====================================================
-- 12. 历史数据回填 SQL (首次部署后，对已存在的原始数据执行一次)
-- =====================================================
-- INSERT INTO sensor_data_1m
-- SELECT ... (同 1m MV)
-- FROM sensor_data WHERE timestamp >= now() - INTERVAL 30 DAY
-- GROUP BY device_id, date_trunc('minute', timestamp);

-- =====================================================
-- 13. 查询示例
-- =====================================================
-- 最近 1 小时的都柱位移峰值:
--   SELECT window_start, max_column_displacement, device_id
--   FROM sensor_data_1m
--   WHERE window_start >= now() - INTERVAL 1 HOUR
--   ORDER BY window_start;
--
-- 近 7 天地震动峰值趋势:
--   SELECT window_start, peak_seismic_accel, trigger_count
--   FROM sensor_data_5m
--   WHERE window_start >= now() - INTERVAL 7 DAY
--   ORDER BY window_start;
