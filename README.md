# LunarSim-PG

LunarSim-PG is a high-fidelity lunar rover simulation framework built on
Unreal Engine 5 and integrated with ROS 2. It combines realistic lunar
rendering, rover and stereo-camera simulation, science-parameterized procedural
terrain generation, and synchronized sensor and ground-truth data in a single
platform.

LunarSim-PG is designed to support the development and evaluation of rover
perception and navigation algorithms. It can be used both for closed-loop ROS 2
experiments and for offline synthetic dataset generation, including visual
odometry, SLAM, obstacle detection, semantic segmentation, and traversability
research.

## Core capabilities

- **High-fidelity lunar simulation** — Unreal Engine 5 rendering with
  lunar-specific terrain, materials, illumination, and shadows, together with
  a controllable wheeled rover and stereo-camera system.

- **ROS 2 integration** — Publish time-stamped stereo images, camera
  calibration, simulated IMU, ground-truth rover state, maps, transforms, and
  simulation time, while accepting rover commands through ROS 2.

- **Ground-truth data generation** — Capture visual, geometric, semantic, pose,
  trajectory, and terrain outputs organized by capture frame and simulation
  timestamp for perception, navigation, mapping, and dataset-generation
  workflows.

- **Procedural lunar terrain generation** — Generate reproducible terrain from
  regional profiles and random seeds, with configurable crater populations,
  degradation states, rock abundance, and crater-coupled rock placement.

## Requirements

| Component | Requirement |
| --- | --- |
| Operating system | Ubuntu 22.04 or 24.04 |
| Unreal Engine | 5.7.x ([click here](https://dev.epicgames.com/documentation/unreal-engine/linux-development-quickstart-for-unreal-engine?application_version=5.7)) |
| ROS 2 | Humble, through the included Docker environment |

## Quick start

Install Unreal Engine 5.7.x before starting. See the
[installation guide](docs/installation.md) for detailed prerequisites and
troubleshooting.

### 1. Clone the repository

```bash
git lfs install
git clone https://github.com/exarchosgiannis/LunarSim-PG.git
cd LunarSim-PG
git lfs pull
```

LunarSim-PG stores its large Unreal Engine assets with Git LFS. These commands
download the actual assets instead of leaving Git LFS pointer files.

### 2. Set the Unreal Engine path

```bash
export UE_ROOT=/path/to/UnrealEngine-5.7.x
```

Replace `/path/to/UnrealEngine-5.7.x` with the engine root containing
`Engine/Build/Build.version`.

### 3. Prepare, build, and open the project

```bash
./setup.sh
./build.sh
./open_project.sh
```

- `setup.sh` prepares dependencies and validates the Unreal Engine path.
- `build.sh` compiles the Unreal Editor project.
- `open_project.sh` opens `LunarSimPG.uproject` in Unreal Editor.


## Documentation

- [Documentation home](docs/README.md)
- [Installation](docs/installation.md)
- [Simulator configuration](docs/configuration.md)
- [Terrain generation](docs/terrain-generation.md)
- [ROS 2 interface](docs/ros2-interface.md)
- [Dataset generation](docs/dataset-generation.md)
- [Output formats](docs/output-format.md)
- [Coordinate frames](docs/coordinate-frames.md)

## Current scope

LunarSim-PG focuses on visual and geometric simulation for rover perception
and navigation, supporting both ROS 2-based experiments and synthetic dataset
generation.

It is not intended as a mission-analysis or terramechanics simulator.
Terrain–wheel interaction, dust dynamics, and thermal modelling are outside
the scope of the current framework.
