class Seismoscope3D {
    constructor() {
        this.scene = null;
        this.camera = null;
        this.renderer = null;
        this.controls = null;
        this.container = null;
        this.column = null;
        this.dragons = [];
        this.bodies = [];
        this.balls = [];
        this.autoRotate = false;
        this.animationId = null;
        this.columnState = {
            displacement: { x: 0, y: 0, z: 0 },
            angle: { x: 0, y: 0 },
            isTriggered: false,
        };
        this.clock = new THREE.Clock();
        this._triggerTimers = {};
    }

    init(containerId) {
        this.container = document.getElementById(containerId);
        if (!this.container) {
            console.error('[Seismoscope3D] Container not found:', containerId);
            return false;
        }

        this.scene = new THREE.Scene();
        this.scene.background = new THREE.Color(0x0a1929);
        this.scene.fog = new THREE.Fog(0x0a1929, 10, 50);

        const width = this.container.clientWidth;
        const height = this.container.clientHeight;

        this.camera = new THREE.PerspectiveCamera(60, width / height, 0.1, 1000);
        this.camera.position.set(8, 6, 8);

        this.renderer = new THREE.WebGLRenderer({ antialias: true });
        this.renderer.setSize(width, height);
        this.renderer.setPixelRatio(window.devicePixelRatio);
        this.renderer.shadowMap.enabled = true;
        this.renderer.shadowMap.type = THREE.PCFSoftShadowMap;
        this.container.appendChild(this.renderer.domElement);

        this.controls = new THREE.OrbitControls(this.camera, this.renderer.domElement);
        this.controls.enableDamping = true;
        this.controls.dampingFactor = 0.05;
        this.controls.minDistance = 5;
        this.controls.maxDistance = 20;
        this.controls.maxPolarAngle = Math.PI / 2;

        this._buildSharedGeometries();
        this.addLights();
        this.createGround();
        this.createBase();
        this.createMainBody();
        this.createDragons();
        this.createColumn();
        this.createToads();

        window.addEventListener('resize', () => this.onWindowResize());

        this.animate();
        return true;
    }

    _buildSharedGeometries() {
        this._sharedGeoms = {
            dragonBody: new THREE.CylinderGeometry(0.3, 0.4, 1.2, 8),
            dragonHead: new THREE.SphereGeometry(0.35, 16, 16),
            dragonHorn: new THREE.ConeGeometry(0.08, 0.3, 8),
            dragonEye: new THREE.SphereGeometry(0.05, 8, 8),
            dragonBall: new THREE.SphereGeometry(0.15, 16, 16),
            toadBody: new THREE.SphereGeometry(0.4, 16, 16),
            toadHead: new THREE.SphereGeometry(0.25, 16, 16),
            toadEye: new THREE.SphereGeometry(0.04, 8, 8),
            toadLeg: new THREE.SphereGeometry(0.12, 8, 8),
        };
        this._sharedGeoms.toadBody.scale(1.2, 0.8, 1);
    }

    addLights() {
        const ambientLight = new THREE.AmbientLight(0x404060, 0.5);
        this.scene.add(ambientLight);

        const mainLight = new THREE.DirectionalLight(0xffffff, 1);
        mainLight.position.set(10, 20, 10);
        mainLight.castShadow = true;
        mainLight.shadow.mapSize.width = 2048;
        mainLight.shadow.mapSize.height = 2048;
        mainLight.shadow.camera.near = 0.5;
        mainLight.shadow.camera.far = 50;
        mainLight.shadow.camera.left = -15;
        mainLight.shadow.camera.right = 15;
        mainLight.shadow.camera.top = 15;
        mainLight.shadow.camera.bottom = -15;
        this.scene.add(mainLight);

        const fillLight = new THREE.DirectionalLight(0xffd700, 0.3);
        fillLight.position.set(-10, 10, -10);
        this.scene.add(fillLight);

        const pointLight = new THREE.PointLight(0xff6b35, 0.8, 20);
        pointLight.position.set(0, 5, 0);
        this.scene.add(pointLight);
    }

    createGround() {
        const groundGeometry = new THREE.CircleGeometry(15, 64);
        const groundMaterial = new THREE.MeshStandardMaterial({
            color: 0x1a2a3a,
            roughness: 0.8,
            metalness: 0.2,
        });
        const ground = new THREE.Mesh(groundGeometry, groundMaterial);
        ground.rotation.x = -Math.PI / 2;
        ground.position.y = -0.01;
        ground.receiveShadow = true;
        this.scene.add(ground);

        const gridHelper = new THREE.GridHelper(20, 40, 0x2a3a4a, 0x1a2a3a);
        gridHelper.position.y = 0.01;
        this.scene.add(gridHelper);
    }

    createBase() {
        const baseGeometry = new THREE.CylinderGeometry(4, 4.5, 0.5, 32);
        const baseMaterial = new THREE.MeshStandardMaterial({
            color: 0x2d3748,
            roughness: 0.6,
            metalness: 0.4,
        });
        const base = new THREE.Mesh(baseGeometry, baseMaterial);
        base.position.y = 0.25;
        base.castShadow = true;
        base.receiveShadow = true;
        this.scene.add(base);

        const ringGeometry = new THREE.TorusGeometry(4, 0.1, 16, 64);
        const ringMaterial = new THREE.MeshStandardMaterial({
            color: 0xffd700,
            roughness: 0.3,
            metalness: 0.8,
        });
        const ring = new THREE.Mesh(ringGeometry, ringMaterial);
        ring.rotation.x = Math.PI / 2;
        ring.position.y = 0.5;
        this.scene.add(ring);
    }

    createMainBody() {
        const bodyGeometry = new THREE.CylinderGeometry(3, 3.5, 4, 32);
        const bodyMaterial = new THREE.MeshStandardMaterial({
            color: 0x4a5568,
            roughness: 0.5,
            metalness: 0.5,
        });
        const body = new THREE.Mesh(bodyGeometry, bodyMaterial);
        body.position.y = 2.5;
        body.castShadow = true;
        body.receiveShadow = true;
        this.scene.add(body);
        this.bodies.push(body);

        const topGeometry = new THREE.CylinderGeometry(1.5, 3, 0.8, 32);
        const topMaterial = new THREE.MeshStandardMaterial({
            color: 0x2d3748,
            roughness: 0.5,
            metalness: 0.5,
        });
        const top = new THREE.Mesh(topGeometry, topMaterial);
        top.position.y = 4.9;
        top.castShadow = true;
        top.receiveShadow = true;
        this.scene.add(top);

        const topRingGeometry = new THREE.TorusGeometry(1.5, 0.08, 16, 64);
        const topRingMaterial = new THREE.MeshStandardMaterial({
            color: 0xffd700,
            roughness: 0.3,
            metalness: 0.8,
        });
        const topRing = new THREE.Mesh(topRingGeometry, topRingMaterial);
        topRing.rotation.x = Math.PI / 2;
        topRing.position.y = 5.3;
        this.scene.add(topRing);
    }

    createDragons() {
        const directions = [
            { angle: 0, name: '北' },
            { angle: Math.PI / 4, name: '东北' },
            { angle: Math.PI / 2, name: '东' },
            { angle: 3 * Math.PI / 4, name: '东南' },
            { angle: Math.PI, name: '南' },
            { angle: 5 * Math.PI / 4, name: '西南' },
            { angle: 3 * Math.PI / 2, name: '西' },
            { angle: 7 * Math.PI / 4, name: '西北' },
        ];

        const bodyMaterial = new THREE.MeshStandardMaterial({
            color: 0x718096, roughness: 0.5, metalness: 0.6,
        });
        const headMaterial = new THREE.MeshStandardMaterial({
            color: 0x4a5568, roughness: 0.4, metalness: 0.6,
        });
        const hornMaterial = new THREE.MeshStandardMaterial({
            color: 0xffd700, roughness: 0.3, metalness: 0.8,
        });
        const eyeMaterial = new THREE.MeshStandardMaterial({
            color: 0x000000, emissive: 0xff0000, emissiveIntensity: 0,
        });
        const ballMaterial = new THREE.MeshStandardMaterial({
            color: 0x1a1a1a, roughness: 0.3, metalness: 0.8,
        });

        directions.forEach((dir, index) => {
            const dragonGroup = new THREE.Group();

            const body = new THREE.Mesh(this._sharedGeoms.dragonBody, bodyMaterial);
            body.rotation.z = Math.PI / 2;
            body.castShadow = true;
            dragonGroup.add(body);

            const head = new THREE.Mesh(this._sharedGeoms.dragonHead, headMaterial);
            head.position.x = 0.8;
            head.castShadow = true;
            dragonGroup.add(head);

            const horn1 = new THREE.Mesh(this._sharedGeoms.dragonHorn, hornMaterial);
            horn1.position.set(0.85, 0.3, 0.15);
            horn1.rotation.z = -0.3;
            dragonGroup.add(horn1);

            const horn2 = new THREE.Mesh(this._sharedGeoms.dragonHorn, hornMaterial);
            horn2.position.set(0.85, 0.3, -0.15);
            horn2.rotation.z = 0.3;
            dragonGroup.add(horn2);

            const eye1 = new THREE.Mesh(this._sharedGeoms.dragonEye, eyeMaterial.clone());
            eye1.position.set(1, 0.1, 0.15);
            dragonGroup.add(eye1);

            const eye2 = new THREE.Mesh(this._sharedGeoms.dragonEye, eyeMaterial.clone());
            eye2.position.set(1, 0.1, -0.15);
            dragonGroup.add(eye2);

            const ball = new THREE.Mesh(this._sharedGeoms.dragonBall, ballMaterial);
            ball.position.set(1, -0.1, 0);
            ball.castShadow = true;
            ball.visible = true;
            dragonGroup.add(ball);
            this.balls.push(ball);

            const radius = 3.8;
            dragonGroup.position.set(
                Math.cos(dir.angle) * radius,
                3.5,
                -Math.sin(dir.angle) * radius
            );
            dragonGroup.rotation.y = -dir.angle;

            this.scene.add(dragonGroup);
            this.dragons.push({
                group: dragonGroup,
                head: head,
                eyes: [eye1, eye2],
                triggered: false,
                name: dir.name,
            });
        });
    }

    createColumn() {
        const columnGroup = new THREE.Group();

        const baseGeometry = new THREE.CylinderGeometry(0.8, 1, 0.5, 16);
        const baseMaterial = new THREE.MeshStandardMaterial({
            color: 0x2d3748, roughness: 0.5, metalness: 0.5,
        });
        const base = new THREE.Mesh(baseGeometry, baseMaterial);
        base.position.y = 0.75;
        base.castShadow = true;
        columnGroup.add(base);

        const columnGeometry = new THREE.CylinderGeometry(0.4, 0.5, 3.5, 16);
        const columnMaterial = new THREE.MeshStandardMaterial({
            color: 0xffd700, roughness: 0.3, metalness: 0.8,
        });
        const column = new THREE.Mesh(columnGeometry, columnMaterial);
        column.position.y = 2.75;
        column.castShadow = true;
        columnGroup.add(column);

        const topGeometry = new THREE.CylinderGeometry(0.5, 0.4, 0.3, 16);
        const topMaterial = new THREE.MeshStandardMaterial({
            color: 0xff6b35, roughness: 0.3, metalness: 0.8,
        });
        const top = new THREE.Mesh(topGeometry, topMaterial);
        top.position.y = 4.65;
        top.castShadow = true;
        columnGroup.add(top);

        const ringGeometry = new THREE.TorusGeometry(0.55, 0.05, 8, 32);
        const ringMaterial = new THREE.MeshStandardMaterial({
            color: 0xffd700, roughness: 0.3, metalness: 0.9,
            emissive: 0xffd700, emissiveIntensity: 0.2,
        });
        const ring1 = new THREE.Mesh(ringGeometry, ringMaterial);
        ring1.rotation.x = Math.PI / 2;
        ring1.position.y = 1.5;
        columnGroup.add(ring1);

        const ring2 = new THREE.Mesh(ringGeometry, ringMaterial.clone());
        ring2.rotation.x = Math.PI / 2;
        ring2.position.y = 3.5;
        columnGroup.add(ring2);

        columnGroup.position.y = 0.5;
        this.scene.add(columnGroup);
        this.column = columnGroup;
    }

    createToads() {
        const toadPositions = [
            { x: 4, z: 0 },
            { x: 2.83, z: 2.83 },
            { x: 0, z: 4 },
            { x: -2.83, z: 2.83 },
            { x: -4, z: 0 },
            { x: -2.83, z: -2.83 },
            { x: 0, z: -4 },
            { x: 2.83, z: -2.83 },
        ];

        const bodyMaterial = new THREE.MeshStandardMaterial({
            color: 0x4a5568, roughness: 0.6, metalness: 0.4,
        });
        const headMaterial = new THREE.MeshStandardMaterial({
            color: 0x2d3748, roughness: 0.5, metalness: 0.5,
        });
        const eyeMaterial = new THREE.MeshStandardMaterial({ color: 0x000000 });

        toadPositions.forEach((pos) => {
            const toadGroup = new THREE.Group();

            const body = new THREE.Mesh(this._sharedGeoms.toadBody, bodyMaterial);
            body.castShadow = true;
            toadGroup.add(body);

            const head = new THREE.Mesh(this._sharedGeoms.toadHead, headMaterial);
            head.position.set(0.4, 0.1, 0);
            head.castShadow = true;
            toadGroup.add(head);

            const eye1 = new THREE.Mesh(this._sharedGeoms.toadEye, eyeMaterial);
            eye1.position.set(0.55, 0.2, 0.1);
            toadGroup.add(eye1);

            const eye2 = new THREE.Mesh(this._sharedGeoms.toadEye, eyeMaterial);
            eye2.position.set(0.55, 0.2, -0.1);
            toadGroup.add(eye2);

            for (let i = 0; i < 4; i++) {
                const leg = new THREE.Mesh(this._sharedGeoms.toadLeg, bodyMaterial);
                const positions = [
                    [0.2, -0.3, 0.25],
                    [0.2, -0.3, -0.25],
                    [-0.3, -0.3, 0.25],
                    [-0.3, -0.3, -0.25],
                ];
                leg.position.set(...positions[i]);
                toadGroup.add(leg);
            }

            toadGroup.position.set(pos.x, 0.4, pos.z);
            toadGroup.rotation.y = Math.atan2(-pos.z, pos.x) - Math.PI / 2;

            this.scene.add(toadGroup);
        });
    }

    setColumnState(dispX, dispZ, angleX, angleY) {
        if (!this.column) return;

        this.columnState.displacement.x = dispX || 0;
        this.columnState.displacement.z = dispZ || 0;
        this.columnState.angle.x = angleX || 0;
        this.columnState.angle.y = angleY || 0;

        this.column.position.x = dispX || 0;
        this.column.position.z = dispZ || 0;
        this.column.rotation.x = angleX || 0;
        this.column.rotation.z = angleY || 0;
    }

    updateColumnState(state) {
        if (!this.column) return;

        this.columnState = { ...this.columnState, ...state };

        const { displacement, angle } = this.columnState;

        this.column.position.x = displacement.x || 0;
        this.column.position.z = displacement.z || 0;
        this.column.position.y = 0.5 + (displacement.y || 0);

        this.column.rotation.x = angle.x || 0;
        this.column.rotation.z = angle.y || 0;
    }

    triggerDragon(direction) {
        if (direction < 0 || direction >= this.dragons.length) return;

        const dragon = this.dragons[direction];
        if (dragon.triggered) return;

        dragon.triggered = true;

        dragon.head.material.emissive = new THREE.Color(0xff0000);
        dragon.head.material.emissiveIntensity = 0.5;
        dragon.eyes.forEach(eye => {
            eye.material.emissiveIntensity = 1;
        });

        if (this.balls[direction]) {
            this.animateBallDrop(direction);
        }

        dragon.group.position.y = 3.3;
        setTimeout(() => {
            if (dragon.group) {
                dragon.group.position.y = 3.5;
            }
        }, 200);
    }

    setDragonTriggered(direction, triggered) {
        if (triggered) {
            this.triggerDragon(direction);
        } else {
            this.resetDragon(direction);
        }
    }

    resetDragon(direction) {
        if (direction < 0 || direction >= this.dragons.length) return;

        const dragon = this.dragons[direction];
        dragon.triggered = false;

        dragon.head.material.emissive = new THREE.Color(0x000000);
        dragon.head.material.emissiveIntensity = 0;
        dragon.eyes.forEach(eye => {
            eye.material.emissiveIntensity = 0;
        });

        if (this.balls[direction]) {
            this.balls[direction].visible = true;
            this.balls[direction].position.set(1, -0.1, 0);
        }
    }

    animateBallDrop(direction) {
        const ball = this.balls[direction];
        if (!ball) return;

        const startY = -0.1;
        const endY = -3;
        const duration = 500;
        const startTime = Date.now();

        const animate = () => {
            const elapsed = Date.now() - startTime;
            const progress = Math.min(elapsed / duration, 1);

            if (progress < 1) {
                const t = progress;
                ball.position.y = startY + (endY - startY) * t * t;
                ball.rotation.x += 0.2;
                ball.rotation.z += 0.15;
                requestAnimationFrame(animate);
            } else {
                ball.visible = false;
            }
        };

        animate();
    }

    resetAllDragons() {
        for (let i = 0; i < this.dragons.length; i++) {
            this.resetDragon(i);
        }
    }

    resetView() {
        if (this.controls) {
            this.camera.position.set(8, 6, 8);
            this.controls.target.set(0, 2.5, 0);
            this.controls.update();
        }
    }

    toggleAutoRotate() {
        this.autoRotate = !this.autoRotate;
        return this.autoRotate;
    }

    onWindowResize() {
        if (!this.container || !this.camera || !this.renderer) return;

        const width = this.container.clientWidth;
        const height = this.container.clientHeight;

        this.camera.aspect = width / height;
        this.camera.updateProjectionMatrix();

        this.renderer.setSize(width, height);
    }

    animate() {
        this.animationId = requestAnimationFrame(() => this.animate());

        const delta = this.clock.getDelta();

        if (this.autoRotate && this.column) {
            this.column.rotation.y += delta * 0.5;
        }

        if (this.controls) {
            this.controls.update();
        }

        if (this.renderer && this.scene && this.camera) {
            this.renderer.render(this.scene, this.camera);
        }
    }

    dispose() {
        if (this.animationId) {
            cancelAnimationFrame(this.animationId);
        }

        if (this.renderer) {
            this.renderer.dispose();
            if (this.container && this.renderer.domElement) {
                this.container.removeChild(this.renderer.domElement);
            }
        }

        Object.values(this._sharedGeoms || {}).forEach(geom => {
            if (geom && geom.dispose) geom.dispose();
        });
    }
}

window.Seismoscope3D = Seismoscope3D;
window.Seismograph3D = Seismoscope3D;
