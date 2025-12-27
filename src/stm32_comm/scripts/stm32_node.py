#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import sys
import os
# Add the directory containing this script to the Python path
sys.path.append(os.path.dirname(os.path.abspath(__file__)))

import rospy
from geometry_msgs.msg import Twist, Point
from host_protocol import USBProtocol

class STM32Node:
    def __init__(self):
        rospy.init_node('stm32_node', anonymous=False)

        # Get parameters
        self.port = rospy.get_param('~port', None) # None means auto-detect
        self.baudrate = rospy.get_param('~baudrate', 115200)

        # Initialize protocol
        self.protocol = USBProtocol(port=self.port, baudrate=self.baudrate)
        
        # Publishers
        self.feedback_pub = rospy.Publisher('stm32_feedback', Point, queue_size=10)

        # Subscribers
        self.cmd_sub = rospy.Subscriber('cmd_vel', Twist, self.cmd_callback)

        # Register callback
        self.protocol.register_callback(self.data_callback)

    def start(self):
        if self.protocol.connect():
            rospy.loginfo(f"Connected to STM32 on port {self.protocol.port}")
            rospy.spin()
        else:
            rospy.logerr("Failed to connect to STM32")

    def stop(self):
        self.protocol.disconnect()

    def cmd_callback(self, msg):
        # Map Twist to cmd_vel, cmd_wel
        # Assuming linear.x is velocity and angular.z is angular velocity
        v = msg.linear.x
        w = msg.angular.z
        
        # You might need to scale these values or map flags if needed
        # For now, passing 0 for flags
        self.protocol.send_command(v, w, 0, 0, 0, 0)

    def data_callback(self, x, y):
        # Publish received data
        p = Point()
        p.x = x
        p.y = y
        p.z = 0.0
        self.feedback_pub.publish(p)

if __name__ == '__main__':
    node = STM32Node()
    try:
        node.start()
    except rospy.ROSInterruptException:
        pass
    finally:
        node.stop()
