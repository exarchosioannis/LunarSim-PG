# Installation

## Requirements

- Ubuntu 22.04 or 24.04.
- Unreal Engine 5.7.x for Linux. `setup.sh` validates the engine version and
  rejects other releases.
- Git with Git LFS installed before cloning or pulling, plus `curl`, `jq`, and
  Python 3.
- Docker Engine.
- Docker Compose v2.
- ROS 2 Humble is provided through the included Docker environment. Host ROS 2
  installation is not required.

Docker Engine and Docker Compose v2 are required for the intended full
LunarSim-PG workflow.

## 1. Clone the repository

```bash
git lfs install
git clone https://github.com/exarchosioannis/LunarSim-PG.git
cd LunarSim-PG
git lfs pull
```

## 2. Prepare the project

Run setup once and provide the path to your Unreal Engine installation:

```bash
./setup.sh --ue-root /path/to/UnrealEngine-5.7.x
```

The path must point to the Unreal Engine root directory, not its `Engine/`
subdirectory. Setup validates the installation, prepares the bundled plugins,
and saves the path locally so you do not need to export it again in future
terminal sessions. It also pulls Git LFS assets, prepares TempoROS's bundled
ROS libraries, and installs missing terrain-tool Python modules with `apt` on
Ubuntu (requesting `sudo` when needed).

## 3. Build and open

```bash
./build.sh
./open_project.sh
```

To clean the Unreal target before rebuilding:

```bash
./build.sh --clean
```

## 4. Start the ROS 2 environment

Build and start the included ROS 2 Humble container:

```bash
docker compose -f docker/docker-compose.yml build
docker compose -f docker/docker-compose.yml up -d
docker exec -it sim_ros bash
```

The Compose configuration expects a graphical X11 session and access to
`/dev/dri` and `/dev/input` for its included GUI and joystick tools. It does
not require NVIDIA-specific container support.

Inside the container:

```bash
source /opt/ros/humble/setup.bash
cd /ws/ros2_ws
colcon build --symlink-install
source install/setup.bash
```

You can now start Play In Editor and use the simulator's ROS 2 topics.

To stop the container:

```bash
exit
docker compose -f docker/docker-compose.yml down
```
