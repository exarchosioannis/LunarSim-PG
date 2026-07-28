# Quickstart

This is the shortest complete editor-and-ROS workflow. For prerequisites and
failure recovery, see [Installation](installation.md) and
[Troubleshooting](troubleshooting.md).

## 1. Clone, set up, build, and open

Working directory: the directory that will contain the repository.

```bash
git lfs install
git clone https://github.com/exarchosgiannis/LunarSim-PG.git
cd LunarSim-PG
git lfs pull
export UE_ROOT=/path/to/UnrealEngine-5.7.x
./setup.sh
./build.sh
./open_project.sh
```

Replace the `UE_ROOT` value with the installed Unreal Engine 5.7.x root. The
project is configured to open the `fresh_crater` level.

## 2. Prepare the level

In the Unreal Editor:

1. Open **Window > Simulator > Simulator Config**.
2. If the level does not already contain the required actors, click
   **Create/Update Rover + Ground Truth**.
3. Leave **Stereo ROS Images + CameraInfo** enabled.
4. Choose **cmd_vel** as the rover **Control Mode** if ROS control is desired,
   then click **Apply Settings**.
5. Start Play In Editor.

World-setup and configuration buttons are disabled during Play In Editor.
Apply changes before starting play. The configured rover actor is
`ESA_Rover`; the map actor is `BP_GroundTruthMapPublisher`.

## 3. Start ROS 2

In a host terminal at the repository root:

```bash
docker compose -f docker/docker-compose.yml build
docker compose -f docker/docker-compose.yml up -d
docker exec -it sim_ros bash
```

Inside the container:

```bash
source /opt/ros/humble/setup.bash
cd /ws/ros2_ws
colcon build --symlink-install
source install/setup.bash
ros2 topic list
ros2 topic echo /clock --once
```

With Play In Editor running, the topic list should include `/clock`; the echo
command should print one clock message. All project and container settings use
ROS domain ID `9` and Cyclone DDS.

## 4. Start capture and view an image

Images publish only while capture is active. Inside the sourced container:

```bash
bash /ws/scripts/startcapture
ros2 topic hz /stereo/left/image_raw
```

After observing the rate, stop `ros2 topic hz` with `Ctrl+C`, then run:

```bash
ros2 run rqt_image_view rqt_image_view
```

In rqt, select `/stereo/left/image_raw`. The image message encoding is `bgr8`.
The configured default capture rate is 6 Hz, subject to Unreal frame rate and
rendering performance.

You can also press `C` in the Play In Editor viewport to toggle capture.

## 5. Control the rover

For a Logitech F710 in XInput mode:

```bash
ros2 launch simulator_ros teleop.launch.py
```

Hold the right bumper while using the left stick. Full left-stick Y commands
up to 1 m/s; left-stick X commands up to 1 rad/s. The joystick is selected by
the default SDL name `Logitech Gamepad F710`.

For keyboard control instead, stop Play In Editor, change **Control Mode** to
**WASD**, apply, and restart play. Use `W`/`S` for forward/brake or reverse and
`A`/`D` for steering.

## 6. Stop cleanly

Inside the container:

```bash
bash /ws/scripts/stopcapture
```

Allow the three-second capture cooldown to finish, then:

1. Stop the teleop and rqt processes with `Ctrl+C`.
2. Stop Play In Editor.
3. Exit and stop the container:

   ```bash
   exit
   docker compose -f docker/docker-compose.yml down
   ```

Offline capture output is written below `Saved/Datasets/`. See
[Dataset generation](dataset-generation.md) for the workflow and
[Output format](output-format.md) for exact files.
