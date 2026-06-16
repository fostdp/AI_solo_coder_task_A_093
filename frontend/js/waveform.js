class WaveformRenderer {
    constructor(canvasId) {
        this.canvas = document.getElementById(canvasId);
        this.ctx = this.canvas.getContext('2d');
        this.maxDataPoints = 500;
        this.data = {
            x: [],
            y: [],
            z: [],
        };
        this.colors = {
            x: '#ef4444',
            y: '#22c55e',
            z: '#3b82f6',
        };
        this.show = {
            x: true,
            y: true,
            z: true,
        };
        this.animationId = null;
        this.isRunning = false;
        this.scale = 50;
        this.offset = 0;
        this.resize();
        window.addEventListener('resize', () => this.resize());
    }

    resize() {
        const rect = this.canvas.getBoundingClientRect();
        this.canvas.width = rect.width * window.devicePixelRatio;
        this.canvas.height = rect.height * window.devicePixelRatio;
        this.ctx.scale(window.devicePixelRatio, window.devicePixelRatio);
        this.width = rect.width;
        this.height = rect.height;
    }

    addData(x, y, z) {
        const now = Date.now();
        this.data.x.push({ time: now, value: x });
        this.data.y.push({ time: now, value: y });
        this.data.z.push({ time: now, value: z });

        if (this.data.x.length > this.maxDataPoints) {
            this.data.x.shift();
            this.data.y.shift();
            this.data.z.shift();
        }

        const maxVal = Math.max(
            ...this.data.x.map(d => Math.abs(d.value)),
            ...this.data.y.map(d => Math.abs(d.value)),
            ...this.data.z.map(d => Math.abs(d.value))
        );
        if (maxVal > 0) {
            this.scale = Math.max(10, (this.height * 0.4) / maxVal;
        }
    }

    addDataPoint(axis, value) {
        const now = Date.now();
        this.data[axis].push({ time: now, value });
        if (this.data[axis].length > this.maxDataPoints) {
            this.data[axis].shift();
        }
        const maxVal = Math.max(...this.data[axis].map(d => Math.abs(d.value)));
        if (maxVal > 0) {
            this.scale = Math.max(10, (this.height * 0.4) / maxVal;
        }
    }

    clear() {
        this.data.x = [];
        this.data.y = [];
        this.data.z = [];
    }

    setShowAxis(axis, show) {
        this.show[axis] = show;
    }

    drawGrid() {
        this.ctx.strokeStyle = 'rgba(255, 255, 255, 0.05)';
        this.ctx.lineWidth = 1;

        const rows = 5;
        for (let i = 0; i <= rows; i++) {
            const y = (this.height / rows) * i;
            this.ctx.beginPath();
            this.ctx.moveTo(0, y);
            this.ctx.lineTo(this.width, y);
            this.ctx.stroke();
        }

        const cols = 10;
        for (let i = 0; i <= cols; i++) {
            const x = (this.width / cols) * i;
            this.ctx.beginPath();
            this.ctx.moveTo(x, 0);
            this.ctx.lineTo(x, this.height);
            this.ctx.stroke();
        }

        this.ctx.strokeStyle = 'rgba(255, 215, 0, 0.3)';
        this.ctx.lineWidth = 1;
        this.ctx.beginPath();
        this.ctx.moveTo(0, this.height / 2);
        this.ctx.lineTo(this.width, this.height / 2);
        this.ctx.stroke();
    }

    drawAxis(axis, data, color) {
        if (!this.show[axis] || data.length < 2) return;

        this.ctx.strokeStyle = color;
        this.ctx.lineWidth = 2;
        this.ctx.beginPath();

        const centerY = this.height / 2;
        const stepX = this.width / (this.maxDataPoints - 1);

        for (let i = 0; i < data.length; i++) {
            const x = i * stepX;
            const y = centerY - data[i].value * this.scale;

            if (i === 0) {
                this.ctx.moveTo(x, y);
            } else {
                this.ctx.lineTo(x, y);
            }
        }

        this.ctx.stroke();

        this.ctx.fillStyle = color + '30';
        this.ctx.lineTo(this.width, centerY);
        this.ctx.lineTo(0, centerY);
        this.ctx.closePath();
        this.ctx.fill();
    }

    drawLabels() {
        const labels = [];
        if (this.show.x) labels.push({ axis: 'X', color: this.colors.x });
        if (this.show.y) labels.push({ axis: 'Y', color: this.colors.y });
        if (this.show.z) labels.push({ axis: 'Z', color: this.colors.z });

        this.ctx.font = '12px monospace';
        labels.forEach((label, index) => {
            const x = 10 + index * 50;
            const y = 20;

            this.ctx.fillStyle = label.color;
            this.ctx.fillRect(x, y - 10, 12, 12);
            this.ctx.fillStyle = '#ffffff';
            this.ctx.fillText(label.axis, x + 18, y);
        });
    }

    render() {
        this.ctx.clearRect(0, 0, this.width, this.height);

        this.ctx.fillStyle = '#0a1929';
        this.ctx.fillRect(0, 0, this.width, this.height);

        this.drawGrid();
        this.drawAxis('x', this.data.x, this.colors.x);
        this.drawAxis('y', this.data.y, this.colors.y);
        this.drawAxis('z', this.data.z, this.colors.z);
        this.drawLabels();
    }

    start() {
        if (this.isRunning) return;
        this.isRunning = true;

        const animate = () => {
            if (!this.isRunning) return;
            this.render();
            this.animationId = requestAnimationFrame(animate);
        };

        animate();
    }

    stop() {
        this.isRunning = false;
        if (this.animationId) {
            cancelAnimationFrame(this.animationId);
            this.animationId = null;
        }
    }

    update(data) {
        if (data.x !== undefined && data.y !== undefined && data.z !== undefined) {
            this.addData(data.x, data.y, data.z);
        }
    }
}

window.WaveformRenderer = WaveformRenderer;
