#!/usr/bin/env python3
import http.server
import socketserver
import os
import rospkg
import rospy

PORT = 8000

def start_server():
    rospy.init_node('web_ui_server', anonymous=True)
    
    # Find the path to the vehicle_launch package
    rospack = rospkg.RosPack()
    pkg_path = rospack.get_path('vehicle_launch')
    web_dir = os.path.join(pkg_path, 'web_ui')
    
    os.chdir(web_dir)
    
    Handler = http.server.SimpleHTTPRequestHandler
    
    with socketserver.TCPServer(("", PORT), Handler) as httpd:
        rospy.loginfo(f"Web UI Server serving at port {PORT}")
        rospy.loginfo(f"Open http://localhost:{PORT} in your browser")
        httpd.serve_forever()

if __name__ == "__main__":
    try:
        start_server()
    except rospy.ROSInterruptException:
        pass
