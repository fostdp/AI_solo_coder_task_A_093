#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import socket
import json
import time
import random
import math
import threading
import argparse
from datetime import datetime
import struct

class SeismicWaveGenerator:
    def __init__(self, base_magnitude=0.0, base_distance=100.0, soil_type=1):
        self.base_magnitude = base_magnitude
        self.base_distance = base_distance
        self.time = 0.0
        self.earthquake_active = False
        self.earthquake_start_time = 0.0
        self.earthquake_magnitude = 0.0
        self.earthquake_distance = 0.0
        self.earthquake_duration = 0.0
        self.earthquake_direction = 0.0
        self.soil_type = soil_type

    SOIL_AMPLIFICATION = {
        0: 0.85,
        1: 1.00,
        2: 1.30,
        3: 1.80
    }
    
    def get_soil_amplification(self):
        return self.SOIL_AMPLIFICATION.get(self.soil_type, 1.0)
        
    def trigger_earthquake(self, magnitude, distance, duration=10.0, direction=None):
        self.earthquake_active = True
        self.earthquake_start_time = self.time
        self.earthquake_magnitude = magnitude
        self.earthquake_distance = distance
        self.earthquake_duration = duration
        if direction is None:
            self.earthquake_direction = random.uniform(-math.pi, math.pi)
        else:
            self.earthquake_direction = math.radians(direction) if abs(direction) > 2 * math.pi else direction
        soil_name = {0: "I类坚硬岩", 1: "II类中硬土", 2: "III类软弱土", 3: "IV类极软土"}.get(
            self.soil_type, f"未知({self.soil_type})")
        print(f"[模拟器] 触发地震: 震级={magnitude}, 距离={distance}km, "
              f"方向={math.degrees(self.earthquake_direction):.1f}°, 土壤={soil_name}, "
              f"持续时间={duration}s, 放大系数={self.get_soil_amplification():.2f}")
        
    def generate_p_wave(self, t, distance):
        vp = 6.0
        arrival_time = distance / vp
        if t < arrival_time:
            return 0.0
        t -= arrival_time
        freq = 5.0
        envelope = math.exp(-0.5 * t) * t
        return envelope * math.sin(2 * math.pi * freq * t)
    
    def generate_s_wave(self, t, distance):
        vs = 3.5
        arrival_time = distance / vs
        if t < arrival_time:
            return 0.0
        t -= arrival_time
        freq = 3.0
        envelope = math.exp(-0.3 * t) * math.sqrt(t)
        return envelope * math.sin(2 * math.pi * freq * t + math.pi / 4)
    
    def generate_rayleigh_wave(self, t, distance):
        vr = 3.0
        arrival_time = distance / vr
        if t < arrival_time:
            return 0.0
        t -= arrival_time
        freq = 1.5
        envelope = math.exp(-0.2 * t) * (t ** 0.25)
        return envelope * math.sin(2 * math.pi * freq * t)
    
    def calculate_wave_amplitude(self, magnitude, distance):
        a = 0.01 * (10 ** (0.5 * magnitude))
        attenuation = math.exp(-0.001 * distance)
        geometric_spreading = 1.0 / math.sqrt(distance + 1.0)
        soil_amp = self.get_soil_amplification()
        return a * attenuation * geometric_spreading * soil_amp
    
    def generate_seismic_acceleration(self, dt):
        self.time += dt
        
        if self.earthquake_active:
            elapsed = self.time - self.earthquake_start_time
            if elapsed > self.earthquake_duration:
                self.earthquake_active = False
                print(f"[模拟器] 地震结束，持续时间={elapsed:.1f}s")
        
        magnitude = self.base_magnitude
        distance = self.base_distance
        
        if self.earthquake_active:
            magnitude = self.earthquake_magnitude
            distance = self.earthquake_distance
        
        amplitude = self.calculate_wave_amplitude(magnitude, distance)
        
        t = self.time
        if self.earthquake_active:
            t = self.time - self.earthquake_start_time
        
        p_wave = self.generate_p_wave(t, distance)
        s_wave = self.generate_s_wave(t, distance)
        rayleigh = self.generate_rayleigh_wave(t, distance)
        noise = random.gauss(0, 0.1)
        
        base_wave = 0.3 * p_wave + 0.5 * s_wave + 0.2 * rayleigh + noise
        accel = amplitude * base_wave
        
        if self.earthquake_active:
            direction = self.earthquake_direction
            accel_x = accel * math.cos(direction)
            accel_y = accel * math.sin(direction)
            accel_z = accel * 0.3
        else:
            noise_dir = random.uniform(-math.pi, math.pi)
            accel_x = accel * math.cos(noise_dir) * 0.1
            accel_y = accel * math.sin(noise_dir) * 0.1
            accel_z = accel * 0.03
        
        return accel_x, accel_y, accel_z, magnitude, distance

class ColumnDynamicsSimulator:
    def __init__(self):
        self.mass = 1000.0
        self.height = 2.5
        self.base_radius = 0.3
        self.stiffness = 50000.0
        self.damping = 500.0
        self.static_friction = 0.6
        self.dynamic_friction = 0.4
        self.trigger_threshold = 0.05
        
        self.disp_x = 0.0
        self.disp_y = 0.0
        self.disp_z = 0.0
        self.angle_x = 0.0
        self.angle_y = 0.0
        self.vel_x = 0.0
        self.vel_y = 0.0
        self.vel_z = 0.0
        self.ang_vel_x = 0.0
        self.ang_vel_y = 0.0
        
        self.triggered = False
        self.trigger_direction = -1
        self.trigger_time = None
        
    def reset(self):
        self.disp_x = 0.0
        self.disp_y = 0.0
        self.disp_z = 0.0
        self.angle_x = 0.0
        self.angle_y = 0.0
        self.vel_x = 0.0
        self.vel_y = 0.0
        self.vel_z = 0.0
        self.ang_vel_x = 0.0
        self.ang_vel_y = 0.0
        self.triggered = False
        self.trigger_direction = -1
        self.trigger_time = None
        
    def calculate_restoring_force(self, displacement, stiffness):
        return stiffness * displacement + 0.01 * stiffness * displacement ** 3
    
    def calculate_damping_force(self, velocity, damping):
        return damping * velocity + 0.05 * damping * velocity * abs(velocity)
    
    def calculate_friction_force(self, velocity, normal_force):
        if abs(velocity) < 0.001:
            return self.static_friction * normal_force * math.tanh(velocity * 1000)
        else:
            return self.dynamic_friction * normal_force * math.tanh(velocity * 10)
    
    def update(self, accel_x, accel_y, accel_z, dt, current_time):
        m = self.mass
        k = self.stiffness
        c = self.damping
        h = self.height
        g = 9.81
        I = (1.0 / 3.0) * m * h * h
        
        inertia_force_x = m * accel_x
        inertia_force_y = m * accel_y
        inertia_force_z = m * accel_z
        
        restoring_force_x = self.calculate_restoring_force(self.disp_x, k)
        restoring_force_y = self.calculate_restoring_force(self.disp_y, k)
        restoring_force_z = self.calculate_restoring_force(self.disp_z, k * 0.5)
        
        damping_force_x = self.calculate_damping_force(self.vel_x, c)
        damping_force_y = self.calculate_damping_force(self.vel_y, c)
        damping_force_z = self.calculate_damping_force(self.vel_z, c * 0.5)
        
        normal_force = m * g + inertia_force_z
        friction_force_x = self.calculate_friction_force(self.vel_x, normal_force)
        friction_force_y = self.calculate_friction_force(self.vel_y, normal_force)
        
        net_force_x = inertia_force_x - restoring_force_x - damping_force_x - friction_force_x
        net_force_y = inertia_force_y - restoring_force_y - damping_force_y - friction_force_y
        net_force_z = inertia_force_z - restoring_force_z - damping_force_z
        
        acc_x = net_force_x / m
        acc_y = net_force_y / m
        acc_z = net_force_z / m
        
        gravity_torque_x = m * g * h * self.angle_x
        gravity_torque_y = m * g * h * self.angle_y
        
        inertia_torque_x = inertia_force_y * h * 0.5
        inertia_torque_y = -inertia_force_x * h * 0.5
        
        damping_torque_x = c * h * h * self.ang_vel_x
        damping_torque_y = c * h * h * self.ang_vel_y
        
        net_torque_x = inertia_torque_x - gravity_torque_x - damping_torque_x
        net_torque_y = inertia_torque_y - gravity_torque_y - damping_torque_y
        
        ang_acc_x = net_torque_x / I
        ang_acc_y = net_torque_y / I
        
        self.vel_x += acc_x * dt
        self.vel_y += acc_y * dt
        self.vel_z += acc_z * dt
        self.disp_x += self.vel_x * dt
        self.disp_y += self.vel_y * dt
        self.disp_z += self.vel_z * dt
        
        self.ang_vel_x += ang_acc_x * dt
        self.ang_vel_y += ang_acc_y * dt
        self.angle_x += self.ang_vel_x * dt
        self.angle_y += self.ang_vel_y * dt
        
        max_angle = math.atan2(self.base_radius, h)
        self.angle_x = max(-max_angle, min(max_angle, self.angle_x))
        self.angle_y = max(-max_angle, min(max_angle, self.angle_y))
        
        total_angle = math.sqrt(self.angle_x ** 2 + self.angle_y ** 2)
        if total_angle > self.trigger_threshold and not self.triggered:
            self.triggered = True
            self.trigger_time = current_time
            angle = math.atan2(self.angle_y, self.angle_x)
            if angle < 0:
                angle += 2 * math.pi
            self.trigger_direction = int(round(angle / (math.pi / 4))) % 8
            print(f"[模拟器] 都柱触发! 方向={self.trigger_direction}, 时间={current_time:.3f}s")
    
    def get_dragon_triggers(self):
        triggers = [0] * 8
        if not self.triggered:
            return triggers
        
        for i in range(8):
            dir_angle = i * (math.pi / 4)
            dir_x = math.cos(dir_angle)
            dir_y = math.sin(dir_angle)
            projection = self.angle_x * dir_x + self.angle_y * dir_y
            total_angle = math.sqrt(self.angle_x ** 2 + self.angle_y ** 2)
            if projection > self.trigger_threshold and total_angle > self.trigger_threshold * 0.8:
                triggers[i] = 1
        return triggers

class SeismographSimulator:
    def __init__(self, host='127.0.0.1', port=12345, device_id='device_001', 
                 send_interval=60.0, protocol='json',
                 soil_type=1, random_seed=None,
                 noise_level=0.1):
        self.host = host
        self.port = port
        self.device_id = device_id
        self.send_interval = send_interval
        self.protocol = protocol
        self.soil_type = soil_type
        self.noise_level = noise_level
        
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.wave_generator = SeismicWaveGenerator(soil_type=soil_type)
        self.column_simulator = ColumnDynamicsSimulator()
        
        if random_seed is not None:
            random.seed(random_seed)
        
        self.running = False
        self.simulation_time = 0.0
        self.simulation_dt = 0.01
        
        self.stats = {
            'packets_sent': 0,
            'earthquakes_triggered': 0,
            'triggers_detected': 0,
            'start_time': None
        }
        
    def generate_sensor_data(self):
        accel_x, accel_y, accel_z, magnitude, distance = self.wave_generator.generate_seismic_acceleration(self.simulation_dt)
        self.simulation_time += self.simulation_dt
        
        self.column_simulator.update(accel_x, accel_y, accel_z, self.simulation_dt, self.simulation_time)
        
        triggers = self.column_simulator.get_dragon_triggers()
        
        if self.column_simulator.triggered and self.column_simulator.trigger_time == self.simulation_time:
            self.stats['triggers_detected'] += 1
        
        data = {
            'timestamp': int(time.time() * 1000),
            'device_id': self.device_id,
            'column_displacement_x': self.column_simulator.disp_x,
            'column_displacement_y': self.column_simulator.disp_y,
            'column_displacement_z': self.column_simulator.disp_z,
            'column_angle_x': self.column_simulator.angle_x,
            'column_angle_y': self.column_simulator.angle_y,
            'seismic_accel_x': accel_x,
            'seismic_accel_y': accel_y,
            'seismic_accel_z': accel_z,
            'dragon_triggers': triggers,
            'magnitude': magnitude,
            'epicenter_distance': distance,
            'is_triggered': 1 if self.column_simulator.triggered else 0,
            'trigger_direction': self.column_simulator.trigger_direction
        }
        
        return data
    
    def serialize_json(self, data):
        return json.dumps(data).encode('utf-8')
    
    def serialize_csv(self, data):
        parts = [
            str(data['timestamp']),
            data['device_id'],
            f"{data['column_displacement_x']:.6f}",
            f"{data['column_displacement_y']:.6f}",
            f"{data['column_displacement_z']:.6f}",
            f"{data['column_angle_x']:.6f}",
            f"{data['column_angle_y']:.6f}",
            f"{data['seismic_accel_x']:.6f}",
            f"{data['seismic_accel_y']:.6f}",
            f"{data['seismic_accel_z']:.6f}",
            *[str(t) for t in data['dragon_triggers']],
            f"{data['magnitude']:.2f}",
            f"{data['epicenter_distance']:.1f}",
            str(data['is_triggered']),
            str(data['trigger_direction'])
        ]
        return ','.join(parts).encode('utf-8')
    
    def serialize_binary(self, data):
        buffer = bytearray()
        buffer.extend(struct.pack('<Q', data['timestamp']))
        
        device_id_bytes = data['device_id'].encode('utf-8')
        buffer.extend(device_id_bytes.ljust(32, b'\x00'))
        
        buffer.extend(struct.pack('<d', data['column_displacement_x']))
        buffer.extend(struct.pack('<d', data['column_displacement_y']))
        buffer.extend(struct.pack('<d', data['column_displacement_z']))
        buffer.extend(struct.pack('<d', data['column_angle_x']))
        buffer.extend(struct.pack('<d', data['column_angle_y']))
        buffer.extend(struct.pack('<d', data['seismic_accel_x']))
        buffer.extend(struct.pack('<d', data['seismic_accel_y']))
        buffer.extend(struct.pack('<d', data['seismic_accel_z']))
        
        buffer.extend(bytes(data['dragon_triggers']))
        
        buffer.extend(struct.pack('<d', data['magnitude']))
        buffer.extend(struct.pack('<d', data['epicenter_distance']))
        buffer.append(data['is_triggered'])
        buffer.extend(struct.pack('<i', data['trigger_direction']))
        
        return bytes(buffer)
    
    def send_data(self, data):
        if self.protocol == 'json':
            payload = self.serialize_json(data)
        elif self.protocol == 'csv':
            payload = self.serialize_csv(data)
        elif self.protocol == 'binary':
            payload = self.serialize_binary(data)
        else:
            payload = self.serialize_json(data)
        
        try:
            self.sock.sendto(payload, (self.host, self.port))
            self.stats['packets_sent'] += 1
            return True
        except Exception as e:
            print(f"[模拟器] 发送失败: {e}")
            return False
    
    def trigger_random_earthquake(self, magnitude_range=None, distance_range=None, duration_range=None):
        if magnitude_range is None:
            magnitude_range = (2.0, 7.0)
        if distance_range is None:
            distance_range = (10.0, 500.0)
        if duration_range is None:
            duration_range = (8.0, 15.0)

        magnitude = random.uniform(*magnitude_range)
        distance = random.uniform(*distance_range)
        duration = random.uniform(*duration_range)
        
        self.wave_generator.trigger_earthquake(magnitude, distance, duration)
        self.stats['earthquakes_triggered'] += 1
        self.column_simulator.reset()
        
        return magnitude, distance, duration
    
    def run(self, auto_earthquake=True, earthquake_interval=300.0,
            magnitude_range=None, distance_range=None, duration_range=None,
            batch_count=None, stop_after_seconds=None):
        self.running = True
        self.stats['start_time'] = time.time()
        
        soil_name = {0: "I类坚硬岩", 1: "II类中硬土", 2: "III类软弱土", 3: "IV类极软土"}.get(
            self.soil_type, f"未知({self.soil_type})")
        
        print(f"[模拟器] 启动: 目标={self.host}:{self.port}, 设备={self.device_id}")
        print(f"[模拟器] 上报间隔={self.send_interval}s, 协议={self.protocol}")
        print(f"[模拟器] 土壤类型={soil_name}, 放大系数={self.wave_generator.get_soil_amplification():.2f}")
        if magnitude_range:
            print(f"[模拟器] 震级范围={magnitude_range}, 距离范围={distance_range}km")
        if batch_count:
            print(f"[模拟器] 批处理模式: 将触发 {batch_count} 次地震后退出")
        if stop_after_seconds:
            print(f"[模拟器] 将在 {stop_after_seconds} 秒后自动退出")
        
        last_send_time = time.time()
        last_earthquake_time = time.time() - earthquake_interval
        triggered_count = 0
        
        try:
            while self.running:
                current_time = time.time()
                
                if auto_earthquake and current_time - last_earthquake_time > earthquake_interval:
                    if batch_count is not None and triggered_count >= batch_count:
                        print(f"[模拟器] 已完成 {batch_count} 次批处理地震触发")
                        break
                    
                    magnitude, distance, duration = self.trigger_random_earthquake(
                        magnitude_range, distance_range, duration_range)
                    triggered_count += 1
                    
                    mag_str = f"{int(magnitude * 10) / 10}"
                    dist_str = f"{int(distance)}"
                    print(f"[模拟器] 自动触发地震 #{triggered_count}: M{mag_str}, {dist_str}km, 持续{duration:.1f}s")
                    last_earthquake_time = current_time
                
                data = self.generate_sensor_data()
                
                if current_time - last_send_time >= self.send_interval:
                    self.send_data(data)
                    
                    magnitude_str = f"M{int(data['magnitude'] * 10) / 10}"
                    status = "触发" if data['is_triggered'] else "正常"
                    direction = data['trigger_direction'] if data['is_triggered'] else "-"
                    
                    print(f"[{datetime.now().strftime('%H:%M:%S')}] "
                          f"发送数据 | {magnitude_str} | {int(data['epicenter_distance'])}km | "
                          f"{status} | 方向={direction}")
                    
                    last_send_time = current_time
                
                time.sleep(self.simulation_dt)
                
                if stop_after_seconds and (time.time() - self.stats['start_time']) > stop_after_seconds:
                    print(f"[模拟器] 已运行 {stop_after_seconds}s，按设定退出")
                    break
                    
        except KeyboardInterrupt:
            print("\n[模拟器] 接收到停止信号")
        finally:
            self.stop()
    
    def stop(self):
        self.running = False
        self.sock.close()
        
        elapsed = time.time() - self.stats['start_time'] if self.stats['start_time'] else 0
        print("\n[模拟器] 统计:")
        print(f"  运行时间: {int(elapsed)}秒")
        print(f"  发送数据包: {self.stats['packets_sent']}")
        print(f"  触发地震: {self.stats['earthquakes_triggered']}次")
        print(f"  都柱触发: {self.stats['triggers_detected']}次")
        print("[模拟器] 已停止")

def main():
    parser = argparse.ArgumentParser(description='地动仪传感器模拟器')
    parser.add_argument('--host', default='127.0.0.1', help='后端服务器地址')
    parser.add_argument('--port', type=int, default=12345, help='后端UDP端口')
    parser.add_argument('--device-id', default='device_001', help='设备ID')
    parser.add_argument('--interval', type=float, default=60.0, help='上报间隔(秒)')
    parser.add_argument('--protocol', default='json', choices=['json', 'csv', 'binary'], help='数据协议')
    
    parser.add_argument('--soil', type=int, default=1, choices=[0, 1, 2, 3],
                        help='场地土类型: 0=I类坚硬岩, 1=II类中硬土(默认), 2=III类软弱土, 3=IV类极软土')
    parser.add_argument('--seed', type=int, default=None, help='随机数种子(复现实验)')
    
    parser.add_argument('--no-auto-earthquake', action='store_true', help='禁用自动地震')
    parser.add_argument('--earthquake-interval', type=float, default=300.0, help='自动地震间隔(秒)')
    parser.add_argument('--magnitude', type=float, default=5.0, help='手动触发地震震级')
    parser.add_argument('--distance', type=float, default=50.0, help='手动触发地震距离(km)')
    parser.add_argument('--duration', type=float, default=10.0, help='手动触发地震持续时间(秒)')
    parser.add_argument('--direction', type=float, default=None, help='手动触发地震方向(角度, 0=正东)')
    
    parser.add_argument('--magnitude-min', type=float, default=2.0, help='随机震级下限')
    parser.add_argument('--magnitude-max', type=float, default=7.0, help='随机震级上限')
    parser.add_argument('--distance-min', type=float, default=10.0, help='随机震中距下限(km)')
    parser.add_argument('--distance-max', type=float, default=500.0, help='随机震中距上限(km)')
    
    parser.add_argument('--trigger-once', action='store_true', help='触发一次地震后退出')
    parser.add_argument('--batch', type=int, default=None, help='批处理模式: 触发 N 次地震后退出')
    parser.add_argument('--stop-after', type=float, default=None, help='运行 N 秒后自动退出')
    
    args = parser.parse_args()
    
    simulator = SeismographSimulator(
        host=args.host,
        port=args.port,
        device_id=args.device_id,
        send_interval=args.interval,
        protocol=args.protocol,
        soil_type=args.soil,
        random_seed=args.seed
    )
    
    if args.trigger_once:
        simulator.wave_generator.trigger_earthquake(
            args.magnitude, args.distance, args.duration, args.direction)
        simulator.column_simulator.reset()
        simulator.run(auto_earthquake=False, stop_after_seconds=args.stop_after)
    elif args.batch is not None:
        simulator.run(
            auto_earthquake=True,
            earthquake_interval=max(5.0, args.earthquake_interval),
            magnitude_range=(args.magnitude_min, args.magnitude_max),
            distance_range=(args.distance_min, args.distance_max),
            batch_count=args.batch,
            stop_after_seconds=args.stop_after
        )
    else:
        simulator.run(
            auto_earthquake=not args.no_auto_earthquake,
            earthquake_interval=args.earthquake_interval,
            magnitude_range=(args.magnitude_min, args.magnitude_max),
            distance_range=(args.distance_min, args.distance_max),
            stop_after_seconds=args.stop_after
        )

if __name__ == '__main__':
    main()
