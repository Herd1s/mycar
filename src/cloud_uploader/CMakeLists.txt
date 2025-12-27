cmake_minimum_required(VERSION 3.0.2)
project(cloud_uploader)

find_package(catkin REQUIRED COMPONENTS
  rospy
  message_generation
)

add_service_files(
  FILES
  GetFileList.srv
  UploadFile.srv
)

generate_messages(
  DEPENDENCIES
)

catkin_package(
  CATKIN_DEPENDS message_runtime
)

include_directories(
  ${catkin_INCLUDE_DIRS}
)

catkin_install_python(PROGRAMS
  scripts/uploader_node.py
  DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
)
