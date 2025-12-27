#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import rospy
import paramiko
import os
from scp import SCPClient
import sys
from cloud_uploader.srv import GetFileList, GetFileListResponse, UploadFile, UploadFileResponse

class CloudUploader:
    def __init__(self):
        rospy.init_node('cloud_uploader_node', anonymous=False)
        
        # Get parameters
        self.hostname = rospy.get_param('~hostname', "47.100.206.61")
        self.username = rospy.get_param('~username', "root")
        self.password = rospy.get_param('~password', "Hj20040911!")
        self.port = rospy.get_param('~port', 22)
        self.local_folder = rospy.get_param('~local_folder', "/home/hu/CyberBot/PCD")
        self.remote_folder_base = rospy.get_param('~remote_folder', "upload")

        rospy.loginfo(f"Cloud Uploader Initialized.")
        rospy.loginfo(f"Target: {self.hostname}, User: {self.username}")
        rospy.loginfo(f"Watching folder: {self.local_folder}")

        # Create local folder if not exists
        if not os.path.exists(self.local_folder):
            rospy.logwarn(f"Local folder {self.local_folder} does not exist. Creating it.")
            os.makedirs(self.local_folder)

        # Services
        self.list_service = rospy.Service('get_pcd_files', GetFileList, self.handle_get_file_list)
        self.upload_service = rospy.Service('upload_pcd_file', UploadFile, self.handle_upload_file)
        
        rospy.spin()

    def handle_get_file_list(self, req):
        try:
            files = [f for f in os.listdir(self.local_folder) if os.path.isfile(os.path.join(self.local_folder, f))]
            files.sort()
            return GetFileListResponse(files)
        except Exception as e:
            rospy.logerr(f"Error listing files: {e}")
            return GetFileListResponse([])

    def handle_upload_file(self, req):
        filename = req.filename
        full_path = os.path.join(self.local_folder, filename)
        
        if not os.path.exists(full_path):
            return UploadFileResponse(False, f"File {filename} not found")
            
        success = self.transfer_file_to_upload(full_path)
        if success:
            return UploadFileResponse(True, "Upload successful")
        else:
            return UploadFileResponse(False, "Upload failed")

    def transfer_file_to_upload(self, local_file):
        """
        传输文件到服务器的upload文件夹
        """
        ssh = None
        try:
            # 创建SSH客户端
            ssh = paramiko.SSHClient()
            ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())

            # 连接到服务器
            rospy.loginfo(f"Connecting to {self.hostname}...")
            ssh.connect(self.hostname, port=self.port, username=self.username, password=self.password)

            # 根据用户选择正确的上传路径
            if self.username == "root":
                # root用户的上传路径
                remote_path = f"/home/admin/{self.remote_folder_base}/{os.path.basename(local_file)}"
            else:
                # 其他用户的上传路径
                remote_path = f"/home/{self.username}/{self.remote_folder_base}/{os.path.basename(local_file)}"

            rospy.loginfo(f"Target path: {remote_path}")

            # 使用SCP传输文件
            with SCPClient(ssh.get_transport()) as scp:
                rospy.loginfo(f"Transferring {local_file}...")
                scp.put(local_file, remote_path)

            rospy.loginfo(f"Successfully uploaded {os.path.basename(local_file)}")
            return True

        except Exception as e:
            rospy.logerr(f"Upload failed: {str(e)}")
            return False
        finally:
            if ssh:
                ssh.close()

if __name__ == "__main__":
    try:
        uploader = CloudUploader()
    except rospy.ROSInterruptException:
        pass
