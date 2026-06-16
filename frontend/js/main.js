class SeismographApp {
    constructor() {
        this.seismograph3d = null;
        this.waveform = null;
        this.sensitivityChart = null;
        this.historyChart = null;
        this.deviceId = 'device_001';
        this.isSimulationRunning = false;
        this.simulationData = [];
        this.currentSimIndex = 0;
        this.simulationInterval = null;
        this.pollingInterval = null;
        this.alertPollingInterval = null;
    }

    init() {
        this.seismograph3d = new Seismograph3D();
        this.seismograph3d.init('seismograph-3d');

        this.waveform = new WaveformRenderer('waveform-canvas');
        this.waveform.start();

        this.initCharts();
        this.bindEvents();
        this.startPolling();
        this.updateSliderValues();
    }

    initCharts() {
        const sensitivityCtx = document.getElementById('sensitivity-chart').getContext('2d');
        this.sensitivityChart = new Chart(sensitivityCtx, {
            type: 'line',
            data: {
                labels: [],
                datasets: [
                    {
                        label: '检测概率 (%)',
                        data: [],
                        borderColor: '#ffd700',
                        backgroundColor: 'rgba(255, 215, 0, 0.1)',
                        fill: true,
                        tension: 0.4,
                    },
                    {
                        label: '误报率 (%)',
                        data: [],
                        borderColor: '#ef4444',
                        backgroundColor: 'rgba(239, 68, 68, 0.1)',
                        fill: true,
                        tension: 0.4,
                    },
                ],
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                plugins: {
                    legend: {
                        labels: {
                            color: '#e8e8e8',
                            font: { size: 11 },
                        },
                    },
                },
                scales: {
                    x: {
                        ticks: { color: '#888' },
                        grid: { color: 'rgba(255, 255, 255, 0.05)' },
                    },
                    y: {
                        min: 0,
                        max: 100,
                        ticks: { color: '#888' },
                        grid: { color: 'rgba(255, 255, 255, 0.05)' },
                    },
                },
            },
        });

        const historyCtx = document.getElementById('history-chart').getContext('2d');
        this.historyChart = new Chart(historyCtx, {
            type: 'line',
            data: {
                labels: [],
                datasets: [
                    {
                        label: 'X轴位移 (m)',
                        data: [],
                        borderColor: '#ef4444',
                        backgroundColor: 'rgba(239, 68, 68, 0.1)',
                        fill: true,
                        tension: 0.4,
                    },
                    {
                        label: 'Y轴位移 (m)',
                        data: [],
                        borderColor: '#22c55e',
                        backgroundColor: 'rgba(34, 197, 94, 0.1)',
                        fill: true,
                        tension: 0.4,
                    },
                    {
                        label: 'Z轴位移 (m)',
                        data: [],
                        borderColor: '#3b82f6',
                        backgroundColor: 'rgba(59, 130, 246, 0.1)',
                        fill: true,
                        tension: 0.4,
                    },
                ],
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                plugins: {
                    legend: {
                        labels: {
                            color: '#e8e8e8',
                            font: { size: 11 },
                        },
                    },
                },
                scales: {
                    x: {
                        ticks: { color: '#888', maxRotation: 0, autoSkip: true, maxTicksLimit: 6 },
                        grid: { color: 'rgba(255, 255, 255, 0.05)' },
                    },
                    y: {
                        ticks: { color: '#888' },
                        grid: { color: 'rgba(255, 255, 255, 0.05)' },
                    },
                },
            },
        });
    }

    bindEvents() {
        document.getElementById('reset-view').addEventListener('click', () => {
            this.seismograph3d.resetView();
        });

        document.getElementById('auto-rotate').addEventListener('click', (e) => {
            const enabled = this.seismograph3d.toggleAutoRotate();
            e.target.textContent = enabled ? '停止旋转' : '自动旋转';
        });

        document.getElementById('trigger-simulation').addEventListener('click', () => {
            this.runQuickSimulation();
        });

        document.getElementById('run-simulation').addEventListener('click', () => {
            this.runSimulation();
        });

        document.getElementById('run-analysis').addEventListener('click', () => {
            this.runSensitivityAnalysis();
        });

        document.getElementById('refresh-alerts').addEventListener('click', () => {
            this.refreshAlerts();
        });

        document.getElementById('load-history').addEventListener('click', () => {
            this.loadHistoryData();
        });

        document.getElementById('show-x').addEventListener('change', (e) => {
            this.waveform.setShowAxis('x', e.target.checked);
        });

        document.getElementById('show-y').addEventListener('change', (e) => {
            this.waveform.setShowAxis('y', e.target.checked);
        });

        document.getElementById('show-z').addEventListener('change', (e) => {
            this.waveform.setShowAxis('z', e.target.checked);
        });

        ['magnitude', 'distance', 'duration', 'stiffness', 'damping'].forEach((id) => {
            document.getElementById(id).addEventListener('input', (e) => {
                document.getElementById(`${id}-value`).textContent = e.target.value;
            });
        });
    }

    updateSliderValues() {
        ['magnitude', 'distance', 'duration', 'stiffness', 'damping'].forEach((id) => {
            const slider = document.getElementById(id);
            if (slider) {
                document.getElementById(`${id}-value`).textContent = slider.value;
            }
        });
    }

    startPolling() {
        this.pollingInterval = setInterval(() => {
            this.pollSensorData();
        }, 2000);

        this.alertPollingInterval = setInterval(() => {
            this.refreshAlerts();
        }, 5000);

        this.statsPollingInterval = setInterval(() => {
            this.refreshStats();
        }, 3000);
    }

    async pollSensorData() {
        try {
            const data = await API.getLatestSensorData(this.deviceId);
            if (data && data.success && data.data) {
                this.updateSensorDisplay(data.data);
            }
        } catch (error) {
            console.error('轮询传感器数据失败:', error);
            this.updateConnectionStatus(false);
        }
    }

    updateSensorDisplay(data) {
        this.updateConnectionStatus(true);

        document.getElementById('disp-x').textContent = data.column_displacement_x?.toFixed(3) || '0.000';
        document.getElementById('disp-y').textContent = data.column_displacement_y?.toFixed(3) || '0.000';
        document.getElementById('disp-z').textContent = data.column_displacement_z?.toFixed(3) || '0.000';
        document.getElementById('angle-x').textContent = data.column_angle_x?.toFixed(3) || '0.000';
        document.getElementById('angle-y').textContent = data.column_angle_y?.toFixed(3) || '0.000';

        const triggerStatus = document.getElementById('trigger-status');
        if (data.is_triggered) {
            triggerStatus.textContent = '已触发';
            triggerStatus.className = 'status-box-value status-triggered';
        } else {
            triggerStatus.textContent = '正常';
            triggerStatus.className = 'status-box-value status-normal';
        }

        this.waveform.update({
            x: data.seismic_accel_x || 0,
            y: data.seismic_accel_y || 0,
            z: data.seismic_accel_z || 0,
        });

        if (this.seismograph3d) {
            this.seismograph3d.updateColumnState({
                displacement: {
                    x: data.column_displacement_x || 0,
                    y: data.column_displacement_y || 0,
                    z: data.column_displacement_z || 0,
                },
                angle: {
                    x: data.column_angle_x || 0,
                    y: data.column_angle_y || 0,
                },
                isTriggered: data.is_triggered || false,
            });
        }

        if (data.dragon_triggers) {
            data.dragon_triggers.forEach((triggered, index) => {
                const dragonEl = document.getElementById(`dragon-${index}`);
                if (dragonEl) {
                    dragonEl.classList.toggle('triggered', triggered === 1);
                }
                if (this.seismograph3d) {
                    this.seismograph3d.setDragonTriggered(index, triggered === 1);
                }
            });
        }

        const columnIndicator = document.getElementById('column-indicator');
        if (columnIndicator) {
            const moveX = (data.column_displacement_x || 0) * 100;
            const moveZ = (data.column_displacement_z || 0) * 100;
            columnIndicator.style.transform = `translate(${moveX}px, ${moveZ}px)`;
        }

        document.getElementById('last-update').textContent = new Date().toLocaleTimeString();
    }

    updateConnectionStatus(connected) {
        const statusEl = document.getElementById('connection-status');
        if (connected) {
            statusEl.textContent = '● 已连接';
            statusEl.className = 'status-value connected';
        } else {
            statusEl.textContent = '● 已断开';
            statusEl.className = 'status-value disconnected';
        }
    }

    async runQuickSimulation() {
        if (this.isSimulationRunning) return;

        this.isSimulationRunning = true;
        this.seismograph3d.resetAllDragons();

        const magnitude = 5.5;
        const distance = 50;
        const duration = 10;

        try {
            const result = await API.runSimulation({
                magnitude,
                distance,
                duration,
                stiffness: 50000,
                damping: 500,
            });

            if (result && result.success && result.data && result.data.timeseries) {
                this.playSimulationTimeseries(result.data.timeseries);
            }
        } catch (error) {
            console.error('运行仿真失败:', error);
            this.isSimulationRunning = false;
        }
    }

    async runSimulation() {
        if (this.isSimulationRunning) return;

        this.isSimulationRunning = true;
        this.seismograph3d.resetAllDragons();

        const params = {
            magnitude: parseFloat(document.getElementById('magnitude').value),
            distance: parseFloat(document.getElementById('distance').value),
            duration: parseFloat(document.getElementById('duration').value),
            stiffness: parseFloat(document.getElementById('stiffness').value),
            damping: parseFloat(document.getElementById('damping').value),
        };

        try {
            const result = await API.runSimulation(params);
            if (result && result.success && result.data && result.data.timeseries) {
                this.playSimulationTimeseries(result.data.timeseries);
            } else {
                this.isSimulationRunning = false;
            }
        } catch (error) {
            console.error('运行仿真失败:', error);
            this.isSimulationRunning = false;
        }
    }

    catmullRom(p0, p1, p2, p3, t) {
        const t2 = t * t;
        const t3 = t2 * t;
        return 0.5 * ((2.0 * p1)
            + (-p0 + p2) * t
            + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2
            + (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3);
    }

    interpolateFrame(rawData, index, alpha) {
        const n = rawData.length;
        const get = (i) => {
            const idx = Math.max(0, Math.min(n - 1, i));
            const d = rawData[idx];
            return d[1] || d;
        };

        const f0 = get(index - 1);
        const f1 = get(index);
        const f2 = get(index + 1);
        const f3 = get(index + 2);

        const channel = (key) => this.catmullRom(
            f0[key] || 0, f1[key] || 0, f2[key] || 0, f3[key] || 0, alpha
        );

        const interp = {
            column_displacement_x: channel('displacement_x') || channel('column_displacement_x'),
            column_displacement_y: channel('displacement_y') || channel('column_displacement_y'),
            column_displacement_z: channel('displacement_z') || channel('column_displacement_z'),
            column_angle_x: channel('angle_x') || channel('column_angle_x'),
            column_angle_y: channel('angle_y') || channel('column_angle_y'),
            seismic_accel_x: f2.seismic_accel_x || f2.x || 0,
            seismic_accel_y: f2.seismic_accel_y || f2.y || 0,
            seismic_accel_z: f2.seismic_accel_z || f2.z || 0,
            is_triggered: f2.is_triggered || false,
            dragon_triggers: f2.dragon_triggers || [0, 0, 0, 0, 0, 0, 0, 0],
        };
        return interp;
    }

    playSimulationTimeseries(timeseries) {
        if (!timeseries || timeseries.length < 2) {
            this.isSimulationRunning = false;
            return;
        }

        this.simulationData = timeseries;
        this.waveform.clear();

        if (this._simRAF) {
            cancelAnimationFrame(this._simRAF);
            this._simRAF = null;
        }

        const rawData = timeseries;
        const totalFrames = rawData.length;
        const sampleDt = (rawData[1][0] || rawData[1].time || 0.001) - (rawData[0][0] || rawData[0].time || 0);
        const playbackSpeed = 1.0;

        let currentSimTime = 0;
        let lastFrameTime = performance.now();

        const render = (now) => {
            const realDt = (now - lastFrameTime) / 1000.0;
            lastFrameTime = now;
            currentSimTime += realDt * playbackSpeed;

            const simIndexFloat = currentSimTime / Math.max(sampleDt, 1e-6);

            if (simIndexFloat >= totalFrames - 2) {
                this.isSimulationRunning = false;
                const last = rawData[totalFrames - 1];
                const lastData = last[1] || last;
                this.waveform.update({
                    x: lastData.seismic_accel_x || 0,
                    y: lastData.seismic_accel_y || 0,
                    z: lastData.seismic_accel_z || 0,
                });
                this.updateSensorDisplay(lastData);
                this._simRAF = null;
                return;
            }

            const idx = Math.floor(simIndexFloat);
            const alpha = simIndexFloat - idx;

            const frame = this.interpolateFrame(rawData, idx, alpha);

            this.waveform.update({
                x: frame.seismic_accel_x || 0,
                y: frame.seismic_accel_y || 0,
                z: frame.seismic_accel_z || 0,
            });

            this.updateSensorDisplay(frame);

            this._simRAF = requestAnimationFrame(render);
        };

        this._simRAF = requestAnimationFrame(render);
    }

    async runSensitivityAnalysis() {
        const analysisType = document.getElementById('analysis-type').value;
        
        const params = {
            type: analysisType,
            min_magnitude: 3,
            max_magnitude: 8,
            min_distance: 10,
            max_distance: 500,
            num_samples: 20,
        };

        try {
            const result = await API.runSensitivityAnalysis(params);
            
            if (result && result.success && result.data) {
                this.updateSensitivityChart(result.data, analysisType);
                this.updateAnalysisResult(result.data, analysisType);
            }
        } catch (error) {
            console.error('运行灵敏度分析失败:', error);
        }
    }

    updateSensitivityChart(data, analysisType) {
        if (!this.sensitivityChart) return;

        let labels = [];
        let detectionData = [];
        let falseAlarmData = [];

        if (analysisType === 'magnitude' && data.magnitude_sensitivity) {
            labels = data.magnitude_sensitivity.map(d => d.magnitude.toFixed(1));
            detectionData = data.magnitude_sensitivity.map(d => d.detection_probability * 100);
            falseAlarmData = data.magnitude_sensitivity.map(d => d.false_alarm_rate * 100);
        } else if (analysisType === 'distance' && data.distance_sensitivity) {
            labels = data.distance_sensitivity.map(d => d.distance.toFixed(0));
            detectionData = data.distance_sensitivity.map(d => d.detection_probability * 100);
            falseAlarmData = data.distance_sensitivity.map(d => d.false_alarm_rate * 100);
        } else if (data.results && data.results.length > 0) {
            labels = data.results.map((_, i) => i.toString());
            detectionData = data.results.map(d => (d.detection_probability || 0) * 100);
            falseAlarmData = data.results.map(d => (d.false_alarm_rate || 0) * 100);
        }

        this.sensitivityChart.data.labels = labels;
        this.sensitivityChart.data.datasets[0].data = detectionData;
        this.sensitivityChart.data.datasets[1].data = falseAlarmData;
        this.sensitivityChart.update();
    }

    updateAnalysisResult(data, analysisType) {
        const resultEl = document.getElementById('analysis-result');
        let html = '';

        if (analysisType === 'detection_range') {
            html = `
                <strong>检测范围分析结果：</strong><br>
                最大检测距离: <span style="color: #ffd700;">${data.max_detection_distance?.toFixed(1) || '--'} km</span><br>
                最小检测震级: <span style="color: #ffd700;">${data.min_detection_magnitude?.toFixed(1) || '--'}</span><br>
                有效检测面积: <span style="color: #ffd700;">${data.detection_area?.toFixed(1) || '--'} km²</span>
            `;
        } else if (analysisType === 'optimize' && data.optimal_params) {
            html = `
                <strong>参数优化结果：</strong><br>
                最优刚度: <span style="color: #ffd700;">${data.optimal_params.stiffness?.toFixed(0) || '--'} N/m</span><br>
                最优阻尼: <span style="color: #ffd700;">${data.optimal_params.damping?.toFixed(0) || '--'}</span><br>
                综合评分: <span style="color: #4ade80;">${(data.optimal_score * 100)?.toFixed(1) || '--'}%</span>
            `;
        } else if (data.avg_detection_probability !== undefined) {
            html = `
                <strong>灵敏度分析结果：</strong><br>
                平均检测概率: <span style="color: #4ade80;">${(data.avg_detection_probability * 100).toFixed(1)}%</span><br>
                平均误报率: <span style="color: ${data.avg_false_alarm_rate > 0.1 ? '#ef4444' : '#4ade80'};">${(data.avg_false_alarm_rate * 100).toFixed(1)}%</span><br>
                平均响应时间: <span style="color: #ffd700;">${data.avg_response_time?.toFixed(3) || '--'} s</span>
            `;
        } else {
            html = '<strong>分析完成</strong><br>查看上方图表了解详细结果。';
        }

        resultEl.innerHTML = html;
    }

    async refreshAlerts() {
        try {
            const result = await API.getAlerts(20);
            if (result && result.success && result.data) {
                this.updateAlertsList(result.data);
            }
        } catch (error) {
            console.error('刷新告警失败:', error);
        }
    }

    updateAlertsList(alerts) {
        const listEl = document.getElementById('alerts-list');
        
        if (!alerts || alerts.length === 0) {
            listEl.innerHTML = '<div class="alert-empty">暂无告警信息</div>';
            return;
        }

        const levelMap = {
            0: { class: 'info', text: '信息' },
            1: { class: 'warning', text: '警告' },
            2: { class: 'error', text: '严重' },
        };

        const typeMap = {
            0: '误触发告警',
            1: '灵敏度下降告警',
            2: '系统告警',
        };

        listEl.innerHTML = alerts.map(alert => {
            const level = levelMap[alert.level] || levelMap[0];
            const type = typeMap[alert.type] || '未知告警';
            const time = new Date(alert.timestamp).toLocaleString();
            
            return `
                <div class="alert-item ${level.class}">
                    <div class="alert-time">${time} | ${level.text}</div>
                    <div class="alert-message">${type}: ${alert.message}</div>
                </div>
            `;
        }).join('');
    }

    async refreshStats() {
        try {
            const result = await API.getStats();
            if (result && result.success && result.data) {
                this.updateStatsDisplay(result.data);
            }
        } catch (error) {
            console.error('刷新统计信息失败:', error);
        }
    }

    updateStatsDisplay(stats) {
        document.getElementById('stat-requests').textContent = stats.total_requests || 0;
        document.getElementById('stat-response-time').textContent = `${stats.avg_response_time?.toFixed(0) || 0}ms`;
        
        if (stats.detection_probability !== undefined) {
            document.getElementById('stat-detection').textContent = `${(stats.detection_probability * 100).toFixed(1)}%`;
        }
        if (stats.false_alarm_rate !== undefined) {
            const color = stats.false_alarm_rate > 0.1 ? '#ef4444' : '#ffd700';
            document.getElementById('stat-false-alarm').textContent = `${(stats.false_alarm_rate * 100).toFixed(1)}%`;
            document.getElementById('stat-false-alarm').style.color = color;
        }
        if (stats.sensitivity_score !== undefined) {
            document.getElementById('stat-sensitivity').textContent = `${(stats.sensitivity_score * 100).toFixed(1)}%`;
        }
    }

    async loadHistoryData() {
        try {
            const result = await API.getSensorData(this.deviceId, 50);
            if (result && result.success && result.data) {
                this.updateHistoryChart(result.data);
                document.getElementById('history-count').textContent = `${result.data.length} 条记录`;
            }
        } catch (error) {
            console.error('加载历史数据失败:', error);
        }
    }

    updateHistoryChart(data) {
        if (!this.historyChart || !data || data.length === 0) return;

        const labels = data.map(d => {
            const date = new Date(d.timestamp);
            return date.toLocaleTimeString();
        }).reverse();

        const xData = data.map(d => d.column_displacement_x || 0).reverse();
        const yData = data.map(d => d.column_displacement_y || 0).reverse();
        const zData = data.map(d => d.column_displacement_z || 0).reverse();

        this.historyChart.data.labels = labels;
        this.historyChart.data.datasets[0].data = xData;
        this.historyChart.data.datasets[1].data = yData;
        this.historyChart.data.datasets[2].data = zData;
        this.historyChart.update();
    }

    dispose() {
        if (this.pollingInterval) clearInterval(this.pollingInterval);
        if (this.alertPollingInterval) clearInterval(this.alertPollingInterval);
        if (this.statsPollingInterval) clearInterval(this.statsPollingInterval);
        if (this.simulationInterval) clearInterval(this.simulationInterval);
        if (this._simRAF) cancelAnimationFrame(this._simRAF);
        
        if (this.waveform) this.waveform.stop();
        if (this.seismograph3d) this.seismograph3d.dispose();
    }
}

document.addEventListener('DOMContentLoaded', () => {
    window.app = new SeismographApp();
    window.app.init();
});

window.addEventListener('beforeunload', () => {
    if (window.app) {
        window.app.dispose();
    }
});
