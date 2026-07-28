# Installation

This page documents the repository-supported Ubuntu editor workflow. Packaged,
headless, Windows, and macOS installation procedures are not present in the
repository.

## Requirements

| Requirement | Verified value | Notes |
| --- | --- | --- |
| Host OS | Ubuntu 24.04 | This is the platform named by the project instructions. |
| Unreal Engine | 5.7.x | `setup.sh` reads `Engine/Build/Build.version` and rejects a different major/minor version. |
| Rendering | Vulkan SM6 | The project selects the Linux Vulkan SM6 shader format and enables ray tracing. Exact GPU, driver, CPU, RAM, and VRAM minimums are not yet verified. |
| Git tooling | Git and Git LFS | Two material assets are stored through Git LFS. |
| Setup tooling | `curl` and `jq` | Both are checked by `setup.sh`. |
| ROS 2 | Humble | The supported user environment is the supplied Docker image. |
| Containers | Docker Engine and Docker Compose v2 | Exact minimum versions are not pinned. Host networking and access to `/dev/dri` and `/dev/input` are configured. |
| Terrain Python modules | `tkinter`, NumPy, Pillow; Matplotlib for analysis | These are not installed by `setup.sh`. Distribution package commands are **To be documented**. |

An NVIDIA-specific requirement is not declared. Use a Linux driver that can
run the project's Vulkan SM6 rendering configuration.

## Obtain Unreal Engine

Install or build Unreal Engine 5.7.x, then identify its root directory. It must
contain all three of these paths:

```text
Engine/Build/Build.version
Engine/Build/BatchFiles/Linux/Build.sh
Engine/Binaries/Linux/UnrealEditor
```

Set `UE_ROOT` in every terminal used for setup, build, or launch:

```bash
export UE_ROOT=/path/to/UnrealEngine-5.7.x
```

The path is necessarily machine-specific. Replace it with the real engine root;
do not point it at the `Engine/` subdirectory.

## Clone and retrieve large files

Working directory: the parent directory in which the repository should be
created.

```bash
git lfs install
git clone https://github.com/exarchosgiannis/LunarSim-PG.git
cd LunarSim-PG
git lfs pull
```

`git lfs pull` retrieves the tracked lunar-regolith material assets. The
repository currently has no `.gitmodules`; `setup.sh` nevertheless initializes
submodules if one is added later.

## Set up bundled plugins

Working directory: repository root.

```bash
export UE_ROOT=/path/to/UnrealEngine-5.7.x
./setup.sh
```

The script verifies Git, Git LFS, `curl`, `jq`, the engine version, project and
plugin descriptors, and then runs `Plugins/TempoROS/Setup.sh`. TempoROS prepares
its bundled ROS 2 Humble headers and Linux libraries.

Successful script completion prints:

```text
Setup complete.
Next: ./build.sh
```

## Build the editor target

Working directory: repository root.

```bash
./build.sh
```

This invokes the Unreal build tool for `simulatorEditor`, `Linux`,
`Development`, and `LunarSimPG.uproject`.

For the script-supported clean target build:

```bash
./build.sh --clean
```

The clean option asks Unreal Build Tool to clean the same target before
compiling it. It does not delete project directories manually.

## Open the project

Working directory: repository root.

```bash
./open_project.sh
```

The script opens `LunarSimPG.uproject` with the editor below `UE_ROOT`. Extra
Unreal Editor arguments can be appended to the command. The configured editor
startup map is `/Game/Levels/fresh_crater`.

## Build the ROS 2 environment

Working directory: repository root.

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
```

The image provides ROS 2 Humble, Cyclone DDS, RViz, rqt image view, rosbag2,
`joy`, `teleop_twist_joy`, NumPy, and Pillow. The Compose file uses host
networking and mounts:

| Host path | Container path | Purpose |
| --- | --- | --- |
| `bags/` | `/ws/bags` | Persistent rosbags |
| `ros2_ws/` | `/ws/ros2_ws` | Project ROS 2 workspace |
| `docker/scripts/` | `/ws/scripts` | Capture-control helpers |
| `/dev/dri` | `/dev/dri` | Rendering access for GUI ROS tools |
| `/dev/input` | `/dev/input` | Joystick access |

## Verify the installation

1. Open the project and start Play In Editor.
2. Start and enter the ROS container, then build and source the workspace as
   shown above.
3. Inside the container, run:

   ```bash
   ros2 topic echo /clock --once
   ```

The implemented success condition is one clock message while Play In Editor is
running. The source and configuration for this check have been verified; an
end-to-end run still requires manual testing on a graphical Unreal host.

To stop the container:

```bash
exit
docker compose -f docker/docker-compose.yml down
```

Continue with the [quickstart](quickstart.md).

