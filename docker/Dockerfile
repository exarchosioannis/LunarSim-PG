FROM ros:humble-ros-base

SHELL ["/bin/bash", "-c"]
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    nano \
    htop \
    python3-pip \
    python3-colcon-common-extensions \
    python3-numpy \
    python3-pil \
    ros-humble-rmw-cyclonedds-cpp \
    ros-humble-rviz2 \
    ros-humble-rqt \
    ros-humble-rqt-image-view \
    ros-humble-tf2-tools \
    ros-humble-ros2bag \
    ros-humble-rosbag2-storage-default-plugins \
    ros-humble-joy \
    ros-humble-teleop-twist-joy \
    mesa-utils \
 && rm -rf /var/lib/apt/lists/*

RUN echo "source /opt/ros/humble/setup.bash" >> /root/.bashrc && \
    echo "if [ -f /ws/ros2_ws/install/setup.bash ]; then source /ws/ros2_ws/install/setup.bash; fi" >> /root/.bashrc

WORKDIR /ws

COPY ./scripts /ws/scripts

RUN chmod +x /ws/scripts/*.py

CMD ["bash"]