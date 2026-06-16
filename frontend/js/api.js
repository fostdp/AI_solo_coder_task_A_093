const API_BASE_URL = 'http://localhost:8080/api';

class ApiClient {
    constructor(baseUrl = API_BASE_URL) {
        this.baseUrl = baseUrl;
    }

    async request(endpoint, options = {}) {
        const url = `${this.baseUrl}${endpoint}`;
        const defaultOptions = {
            headers: {
                'Content-Type': 'application/json',
            },
        };

        try {
            const response = await fetch(url, { ...defaultOptions, ...options });
            if (!response.ok) {
                throw new Error(`HTTP ${response.status}: ${response.statusText}`);
            }
            return await response.json();
        } catch (error) {
            console.error(`API请求失败 [${endpoint}]:`, error);
            throw error;
        }
    }

    async getSensorData(deviceId, limit = 100) {
        const params = new URLSearchParams({
            device_id: deviceId,
            limit: limit.toString(),
        });
        return this.request(`/sensor/data?${params}`);
    }

    async getSensorDataRange(deviceId, startTime, endTime) {
        const params = new URLSearchParams({
            device_id: deviceId,
            start_time: startTime,
            end_time: endTime,
        });
        return this.request(`/sensor/data/range?${params}`);
    }

    async getLatestSensorData(deviceId) {
        const params = new URLSearchParams({ device_id: deviceId });
        return this.request(`/sensor/data/latest?${params}`);
    }

    async getAlerts(limit = 50) {
        const params = new URLSearchParams({ limit: limit.toString() });
        return this.request(`/alerts?${params}`);
    }

    async getAlertsByLevel(level, limit = 50) {
        const params = new URLSearchParams({
            level: level,
            limit: limit.toString(),
        });
        return this.request(`/alerts/level?${params}`);
    }

    async getSensitivityAnalysis(analysisType, params = {}) {
        const queryParams = new URLSearchParams({
            type: analysisType,
            ...params,
        });
        return this.request(`/sensitivity?${queryParams}`);
    }

    async getSensitivityById(id) {
        return this.request(`/sensitivity/${id}`);
    }

    async runSimulation(params) {
        return this.request('/simulation/run', {
            method: 'POST',
            body: JSON.stringify(params),
        });
    }

    async runSensitivityAnalysis(params) {
        return this.request('/sensitivity/run', {
            method: 'POST',
            body: JSON.stringify(params),
        });
    }

    async getStats() {
        return this.request('/stats');
    }

    async getHealth() {
        return this.request('/health');
    }
}

const api = new ApiClient();

window.API = {
    api,
    ApiClient,
    getSensorData: (deviceId, limit) => api.getSensorData(deviceId, limit),
    getSensorDataRange: (deviceId, startTime, endTime) => api.getSensorDataRange(deviceId, startTime, endTime),
    getLatestSensorData: (deviceId) => api.getLatestSensorData(deviceId),
    getAlerts: (limit) => api.getAlerts(limit),
    getAlertsByLevel: (level, limit) => api.getAlertsByLevel(level, limit),
    getSensitivityAnalysis: (analysisType, params) => api.getSensitivityAnalysis(analysisType, params),
    getSensitivityById: (id) => api.getSensitivityById(id),
    runSimulation: (params) => api.runSimulation(params),
    runSensitivityAnalysis: (params) => api.runSensitivityAnalysis(params),
    getStats: () => api.getStats(),
    getHealth: () => api.getHealth(),
};
