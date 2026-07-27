# LunarSim-PG setup

## Prerequisites

- Ubuntu 24.04
- Unreal Engine 5.7.x already installed
- Git
- Git LFS
- Docker

## First-Time Setup

### Git LFS

```bash
git lfs install
```

### Clone

```bash
git clone https://github.com/exarchosgiannis/LunarSim-PG.git
cd LunarSim-PG
```

### Unreal Engine

```bash
export UE_ROOT=/absolute/path/to/UnrealEngine-5.7.x
```

### Setup

```bash
./setup.sh
```

### Build

```bash
./build.sh
```

### Open

```bash
./open_project.sh
```

### ROS 2

```bash
docker compose -f docker/docker-compose.yml build
docker compose -f docker/docker-compose.yml up -d
```

### Enter

```bash
docker exec -it sim_ros bash
source /opt/ros/humble/setup.bash
cd /ws/ros2_ws
colcon build --symlink-install
source install/setup.bash
```

### Stop

```bash
docker compose -f docker/docker-compose.yml down
```

### Notes

- Docker and Docker Compose must already be installed.
- The default Linux configuration uses Vulkan SM6, so opening the Unreal Editor requires a compatible GPU and driver.
