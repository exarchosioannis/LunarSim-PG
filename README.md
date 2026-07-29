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
git clone https://github.com/exarchosioannis/LunarSim-PG.git
cd LunarSim-PG
git lfs pull
```

LunarSim-PG stores its large Unreal Engine assets with Git LFS. These commands
download the actual assets instead of leaving Git LFS pointer files.

### 2. Prepare the project and save the Unreal Engine path

```bash
./setup.sh --ue-root /path/to/UnrealEngine-5.7.x
```

Replace `/path/to/UnrealEngine-5.7.x` with the engine root containing
`Engine/Build/Build.version`.

### 3. Build and open the project

```bash
./build.sh
./open_project.sh
```

- `setup.sh` prepares dependencies, validates the Unreal Engine path, and saves
  it for later terminal sessions.
- `build.sh` compiles the Unreal Editor project.
- `open_project.sh` opens `LunarSimPG.uproject` in Unreal Editor.

## How to use LunarSim-PG

### 1. Choose a lunar environment

Once the project opens, choose one of the included levels from
`Content/Levels/`:

| Level | Description |
| --- | --- |
| `apollo17` | _[Add a short description of the terrain and its main characteristics.]_ |
| `highland` | _[Add a short description of the terrain and its main characteristics.]_ |
| `mare` | _[Add a short description of the terrain and its main characteristics.]_ |
| `fresh_crater` | _[Add a short description of the terrain and its main characteristics.]_ |
| `custom` | _[Add a short description, for example a configurable terrain with a dense rock field.]_ |

You can also create your own lunar terrain by following the
[terrain-generation guide](docs/terrain-generation.md).

### 2. Configure your simulation

Open **Window > Simulator > Simulator Config** and choose the settings you need
for your experiment.

For example, you can select:

- which ground-truth outputs to generate;
- whether to publish stereo images through ROS 2;
- pose, trajectory, and timing outputs;
- rover control through WASD or ROS 2 `cmd_vel`.

When you are ready, press **Apply Settings**.

For a full explanation of every option, see the
[configuration guide](docs/configuration.md).

### 3. Start the simulation

Start **Play In Editor** to begin the simulation.

LunarSim-PG creates a new run directory under `Saved/Datasets/`, where your
selected configuration and generated outputs are organized.

### 4. Start and stop capture

With the Play In Editor viewport active, press `C` whenever you want to start
capturing data. Press `C` again to stop.

Depending on your settings, LunarSim-PG can save ground-truth data and publish
stereo images through ROS 2. Each capture is stored in its own
`Session_NNN` directory.

## Data generation and ROS 2

LunarSim-PG supports two complementary ways to work with simulation data. Save
reference data directly to disk for offline use, work with live ROS 2 streams,
or use both during the same run.

| Workflow | What you get |
| --- | --- |
| Dataset generation | Ground-truth images, trajectories, metadata, manifests, and terrain maps saved under `Saved/Datasets/` |
| ROS 2 operation | Stereo cameras, IMU, rover ground truth, TF, clock, maps, capture control, and `cmd_vel`, with optional rosbag recording |

### Save data directly to disk

A capture session can contain:

- ground-truth RGB aligned with the left/reference camera, packed depth,
  segmentation images with JSON legends, and per-frame bounding boxes.
- rover, left-camera, and right-camera trajectories, when their pose sources
  are enabled and available.
- a manifest that connects output files to their offline frame index and
  simulation timestamp.
- session metadata describing the level, enabled outputs, camera calibration,
  frame names, capture rate, and map paths;
- run-level occupancy maps for free, occupied, and unknown cells, elevation
  maps for terrain height, and slope maps for local terrain slope.

The **frame index** and **timestamp** make the trajectories useful as reference data
for visual odometry, SLAM, localization, and trajectory evaluation. Use the
ground-truth maps as references for mapping, elevation reconstruction,
obstacle-map, or traversability evaluation.

### Work live through ROS 2

The live interface groups stereo images and calibration, IMU data, rover
ground-truth pose, odometry and path, occupancy and elevation maps, TF, and
simulation time. It also accepts rover commands through `/cmd_vel` and capture
commands through `/capture/control`.

Live ROS streams are not all duplicated in the offline session folder. Record
a rosbag when you need the stereo pair, IMU, TF, clock, or other selected
topics after the simulation ends.

### Built with

- [UnrealGT](https://github.com/unrealgt/unrealgt) provides ground-truth RGB,
  depth, segmentation, and bounding-box generation. LunarSim-PG uses a modified
  version with asynchronous GPU readback, camera warm-up, and frame/timestamp
  association for synchronized capture.
- [TempoROS](https://github.com/tempo-sim/TempoROS) provides the bridge between
  Unreal Engine and ROS 2.

[Explore the available data and ROS 2 interface](docs/data-and-ros2.md).

## Documentation

- [Documentation home](docs/README.md)
- [Installation](docs/installation.md)
- [Simulator configuration](docs/configuration.md)
- [Terrain generation](docs/terrain-generation.md)
- [Data outputs and ROS 2](docs/data-and-ros2.md)
- [Coordinate frames](docs/coordinate-frames.md)

## Current scope

LunarSim-PG focuses on visual and geometric simulation for rover perception
and navigation, supporting both ROS 2-based experiments and synthetic dataset
generation.

It is not intended as a mission-analysis or terramechanics simulator.
Terrain–wheel interaction, dust dynamics, and thermal modelling are outside
the scope of the current framework.
