#!/usr/bin/env python3
import rclpy
from rclpy.node import Node
from sensor_msgs.msg import LaserScan
import serial
import math
import time

class ArduinoLidarBridge(Node):
    def __init__(self):
        super().__init__('arduino_lidar_bridge')
        
        #ROS 2 Parameters
        self.declare_parameter('port', '/dev/ttyACM0')
        self.declare_parameter('baudrate', 115200)
        self.declare_parameter('frame_id', 'laser')
        
        port = self.get_parameter('port').get_parameter_value().string_value
        baudrate = self.get_parameter('baudrate').get_parameter_value().integer_value
        self.frame_id = self.get_parameter('frame_id').get_parameter_value().string_value
        
        # --- Updated FOV & Resolution Settings ---
        self.num_samples = 400                      # 400 readings per sweep
        self.min_angle = -math.pi / 2.0             # -90 deg (-1.5708 rad)
        self.max_angle = math.pi / 2.0              # +90 deg (+1.5708 rad)
        self.fov = self.max_angle - self.min_angle  # 180 deg (pi rad)
        self.angle_increment = self.fov / self.num_samples      # 0.45 deg per step
        
        # Buffer to stare 400 points
        self.ranges = [float('inf')] * self.num_samples
        self.last_angle = -90.0
        
        # ROS 2 Publisher
        self.publisher_ = self.create_publisher(LaserScan, '/scan', 10)
        
        # Open Serial Port
        try:
            self.get_logger().info(f"Connecting to 180-deg LiDAR on {port}...")
            self.ser = serial.Serial(port, baudrate, timeout=1.0)
            time.sleep(2.0)
        except serial.SerialException as e:
            self.get_logger().error(f"Faile to connect to serail port: {e}")
            raise SystemExit
        # High-frequency processing timer
        self.timer = self.create_timer(0.002, self.read_serial_data)
        
    def read_serial_data(self):
        if self.ser.in_waiting > 0:
            try:
                line = self.ser.readline().decode('utf-8').strip()
                if not line or ',' not in line:
                    return
                
                # Expects "ANGLE, DISTANCE" from Arduino
                angle_deg, distance_m = map(float, line.split(','))
                
                # Convert input angle to range [-980, +90] if sent as [0, 180]
                if angle_deg > 90.0:
                    angle_deg -= 90.0
                    
                angle_rad = math.radians(angle_deg)
                
                # Map angle to an array index [0 to 399]
                index = int((angle_rad - self.min_angle) / self.angle_increment)
                if 0 <= index < self.num_samples:
                    if distance_m <= 0.05 or distance_m > 12.0:
                        self.ranges[index] = float('inf')
                    else:
                        self.ranges[index] = distance_m
                        
                # Detect end of sweep (angle drop or direction change)
                if angle_deg < self.last_angle and (self.last_angle - angle_deg) > 90.0:
                    self.publish_scan()
                    
                self.last_angle = angle_deg
                
            except (ValueError, UnicodeDecodeError):
                pass        # Skip malformed lines
            
    def publish_scan(self):
        msg = LaserScan()
        msg.header.stamp = self.get_clock().now().to_msg()
        msg.header.frame_id = self.frame_id
        
        msg.angle_min = self.min_angle
        msg.angle_max = self.max_angle
        msg.angle_increment = self.angle_increment
        msg.time_increment = 0.0
        msg.scan_time = 0.1
        msg.range_min = 0.05
        msg.range_max = 12.0
        
        msg.ranges = self.ranges
        self.publisher_.publish(msg)
        
        # Reset array buffer for next sweep
        self.ranges = [float('inf')] * self.num_samples
        
    def main(args=None):
        rclpy.init(args=args)
        node = ArduinoLidarBridge()
        try:
            rclpy.spin(node)
        except KeyboardInterrupt:
            pass
        finally:
            if hasattr(node, 'ser') and node.ser.is_open:
                node.ser.close()
            node.destroy_node()
            rclpy.shutdown()
            
    if __name__ == '__main__':
        main()