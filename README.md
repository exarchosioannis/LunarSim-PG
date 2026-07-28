# LunarSim-PG

LunarSim-PG is an Unreal Engine 5 lunar rover simulator for closed-loop ROS 2
operation and ground-truth-rich dataset capture. It combines reusable lunar
levels, a procedural heightmap and rockfield tool, rover and stereo-camera
simulation, ROS 2 interfaces, and run-organized dataset export.

## Key features

- Unreal Engine rover simulation with manual or ROS 2 `cmd_vel` control.
- Stereo RGB images, camera calibration, IMU, poses, odometry, paths, TF,
  simulation time, occupancy grids, and elevation point clouds over ROS 2.
- Offline RGB, encoded depth, color-coded segmentation, bounding-box, pose,
  calibration, occupancy, elevation, and slope outputs.
- Seeded terrain heightmap, crater-catalogue, and crater-aware rockfield
  generation.
- Editor tools for simulator configuration, world setup, and rockfield baking.

## Requirements

| Component | Verified requirement |
| --- | --- |
| Operating system | Ubuntu 24.04 |
| Unreal Engine | 5.7.x |
| ROS 2 | Humble, provided through the project Docker image |
| Host tools | Git, Git LFS, `curl`, `jq`, Docker Engine, Docker Compose v2 |
| Rendering | Linux Vulkan SM6-capable GPU and driver; exact hardware and VRAM requirements are not yet verified |

## Quick start

Run from a terminal:

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

Replace `/path/to/UnrealEngine-5.7.x` with the engine root containing
`Engine/Build/Build.version`.

See the [installation guide](docs/installation.md) for prerequisites and clean
builds, or follow the [complete quickstart](docs/quickstart.md) to start Play In
Editor, connect ROS 2, view an image, and control the rover.

## Verify the installation

After the editor opens:

1. Open **Window > Simulator > Simulator Config**.
2. Apply the desired settings while Play In Editor is stopped.
3. Start Play In Editor.
4. Start the ROS container as described in the
   [quickstart](docs/quickstart.md), then run:

   ```bash
   ros2 topic echo /clock --once
   ```

A successful check prints one `rosgraph_msgs/msg/Clock` message while Play In
Editor is running. Camera images require capture to be started separately.

## Documentation

- [Documentation home](docs/README.md)
- [Installation](docs/installation.md)
- [Quickstart](docs/quickstart.md)
- [Simulator configuration](docs/configuration.md)
- [Terrain generation](docs/terrain-generation.md)
- [ROS 2 interface](docs/ros2-interface.md)
- [Dataset generation](docs/dataset-generation.md)
- [Output formats](docs/output-format.md)
- [Coordinate frames](docs/coordinate-frames.md)

## Current scope

- The documented runtime is the Unreal Editor on Ubuntu; packaged, headless,
  and non-Linux workflows are not present in the repository.
- No terramechanics, dust, or thermal simulation subsystem is present in the
  current source.
- Stereo images and IMU samples are ROS 2 streams. The offline dataset writer
  saves one ground-truth camera view and trajectory CSV files; record a rosbag
  for ROS-only streams.
- Terrain profile, seed, and preview illumination metadata are not copied into
  Unreal session metadata. Preserve the terrain-generation package with a
  dataset when those values are required for reproducibility.
- No automated dataset completeness or synchronization validator is included.
