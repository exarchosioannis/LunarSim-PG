# Project Setup Instructions

## Requirements

This project needs:

- Linux
- Docker
- Docker Compose
- Unreal Engine 5.7.4 or compatible version
- Git

The Unreal Engine installation is not included in the repository.
If Unreal is installed somewhere else, set `UE_ROOT` before building.

---

## 1. Clone the project

```bash
git clone <repo-url>
cd simulator_test5.7
```

---

## 2. Start the ROS 2 Docker container

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

```bash
0
0
rmw_cyclonedds_cpp
```

---

## 3. Build the Unreal project

Go back to the project root:

```bash
cd ..
```

If Unreal Engine is installed in the default path:

```bash
./build.sh
```

If Unreal Engine is installed somewhere else:

```bash
UE_ROOT=/path/to/UnrealEngine ./build.sh
```

Example:

```bash
UE_ROOT=$HOME/Downloads/Linux_Unreal_Engine_5.7.4 ./build.sh
```

---

## 4. Open the project

Open this file with Unreal Engine:

```bash
simulator_test57.uproject
```

Then press Play.

---

## 5. Check ROS topics

While Unreal is running, enter the Docker container:

```bash
docker exec -it sim_ros bash
```

Then run:

```bash
ros2 topic list
```

You should see the simulator ROS topics.

---

## Notes

The Docker container uses:

```bash
ROS_DOMAIN_ID=0
ROS_LOCALHOST_ONLY=0
RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
```

The container must use host networking. This is already set in:

```bash
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