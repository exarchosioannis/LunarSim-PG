# Project Setup Instructions

These instructions explain how to set up the Unreal + TempoROS project on a new Linux machine.

## Requirements

This project needs:

- Linux
- Git
- Docker
- Docker Compose
- Unreal Engine 5.7.4 or compatible version
- `curl` and `jq` for TempoROS setup

The Unreal Engine installation is **not included** in this repository.

---

## 1. Clone the project

```bash
git clone <repo-url>
cd simulator57
```

Replace `<repo-url>` with the GitHub repository URL.

---

## 2. Set the Unreal Engine path

TempoROS needs the `UNREAL_ENGINE_PATH` environment variable on Linux.

This path must point to the Unreal Engine folder that contains the `Engine/` directory.

Example folder structure:

```text
/home/YOUR_USER/Downloads/Linux_Unreal_Engine_5.7.4/
└── Engine/
    ├── Binaries/
    ├── Build/
    ├── Source/
    └── ...
```

So the correct export command would be:

```bash
export UNREAL_ENGINE_PATH=/home/path_to_UE5_7/Linux_Unreal_Engine_5.7.4
```

Check that the path is correct:

```bash
ls "$UNREAL_ENGINE_PATH/Engine"
```

---

## 3. Install TempoROS setup dependencies

```bash
sudo apt update
sudo apt install curl jq
```

---

## 4. Setup TempoROS

From the project root:

```bash
cd Plugins/TempoROS
./Setup.sh -force
cd ../..
```

This downloads/creates the required TempoROS third-party dependencies, including `rclcpp`.

---

## 5. Build the Unreal project

From the project root:

```bash
./build.sh
```

If the build succeeds, the C++ side of the project is set up correctly.

---

## 6. Start the ROS 2 Docker container

From the project root:

```bash
cd docker
docker compose up --build -d
```

Enter the container:

```bash
docker exec -it sim_ros bash
```

Inside the container, build the ROS workspace:

```bash
cd /ws/ros2_ws
colcon build
source install/setup.bash
```

Check the ROS environment:

```bash
echo $ROS_DOMAIN_ID
echo $ROS_LOCALHOST_ONLY
echo $RMW_IMPLEMENTATION
```

Expected values:

```text
0
0
rmw_cyclonedds_cpp
```

Then leave the container if needed:

```bash
exit
```

---

## 7. Open the Unreal project

Open the project with Unreal Engine:

```bash
"$UNREAL_ENGINE_PATH/Engine/Binaries/Linux/UnrealEditor" simulator_test57.uproject
```

Then open the main level and press **Play**.

---

## 8. Check ROS topics

While Unreal is running, enter the ROS container:

```bash
docker exec -it sim_ros bash
```

Then run:

```bash
ros2 topic list
```

You should see the simulator ROS topics.

---

## Quick setup summary

For a new Linux machine, the main setup flow is:

```bash
git clone <repo-url>
cd simulator57

export UNREAL_ENGINE_PATH=/path/to/Linux_Unreal_Engine_5.7.4

sudo apt update
sudo apt install curl jq

cd Plugins/TempoROS
./Setup.sh -force
cd ../..

./build.sh
```

Then start Docker and open the Unreal project.

---

## Notes

The Docker container uses:

```text
ROS_DOMAIN_ID=0
ROS_LOCALHOST_ONLY=0
RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
```

The container must use host networking. This should already be set in:

```text
docker/docker-compose.yml
```

Do not commit generated folders such as:

```text
Binaries/
Intermediate/
DerivedDataCache/
Saved/
ros2_ws/build/
ros2_ws/install/
ros2_ws/log/
bags/
```

TempoROS generated dependency folders should also not be manually committed unless there is a specific reason. New users should run:

```bash
cd Plugins/TempoROS
./Setup.sh -force
```