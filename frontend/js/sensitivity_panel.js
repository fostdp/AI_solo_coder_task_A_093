class SensitivityPanel {
    constructor() {
        this.chart = null;
        this.currentAnalysisType = 'magnitude';
        this.lastResults = null;
    }

    init() {
        this.initChart();
        this.bindEvents();
    }

    initChart() {
        const ctx = document.getElementById('sensitivity-chart');
        if (!ctx) return;

        this.chart = new Chart(ctx, {
            type: 'line',
            data: {
                labels: [],
                datasets: [
                    {
                        label: '检测概率 (%)',
                        data: [],
                        borderColor: '#4ade80',
                        backgroundColor: 'rgba(74, 222, 128, 0.1)',
                        tension: 0.3,
                        fill: true,
                    },
                    {
                        label: '误报率 (%)',
                        data: [],
                        borderColor: '#ef4444',
                        backgroundColor: 'rgba(239, 68, 68, 0.1)',
                        tension: 0.3,
                        fill: true,
                    },
                ],
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                plugins: {
                    legend: {
                        labels: { color: '#e0e0e0' },
                    },
                },
                scales: {
                    x: {
                        ticks: { color: '#94a3b8' },
                        grid: { color: 'rgba(148, 163, 184, 0.1)' },
                    },
                    y: {
                        min: 0,
                        max: 100,
                        ticks: { color: '#94a3b8' },
                        grid: { color: 'rgba(148, 163, 184, 0.1)' },
                    },
                },
            },
        });
    }

    bindEvents() {
        const runBtn = document.getElementById('run-analysis');
        if (runBtn) {
            runBtn.addEventListener('click', () => this.runAnalysis());
        }
    }

    getAnalysisType() {
        const select = document.getElementById('analysis-type');
        return select ? select.value : 'magnitude';
    }

    async runAnalysis() {
        const analysisType = this.getAnalysisType();
        this.currentAnalysisType = analysisType;

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
                this.lastResults = result.data;
                this.updateChart(result.data, analysisType);
                this.updateResultDisplay(result.data, analysisType);
            }
        } catch (error) {
            console.error('[SensitivityPanel] 运行灵敏度分析失败:', error);
        }
    }

    updateChart(data, analysisType) {
        if (!this.chart) return;

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

        this.chart.data.labels = labels;
        this.chart.data.datasets[0].data = detectionData;
        this.chart.data.datasets[1].data = falseAlarmData;
        this.chart.update();
    }

    updateResultDisplay(data, analysisType) {
        const resultEl = document.getElementById('analysis-result');
        if (!resultEl) return;

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

    setResults(results, type) {
        this.lastResults = results;
        this.currentAnalysisType = type;
        this.updateChart(results, type);
        this.updateResultDisplay(results, type);
    }

    getLastResults() {
        return this.lastResults;
    }

    dispose() {
        if (this.chart) {
            this.chart.destroy();
            this.chart = null;
        }
    }
}
