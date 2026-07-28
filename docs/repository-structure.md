# Repository structure

The tree below lists source-controlled paths that matter to users and
developers. Unreal and colcon build products are intentionally omitted.

```text
LunarSim-PG/
├── Config/
├── Content/
│   ├── 3D_Models/
│   ├── Blueprints/
│   ├── Levels/
│   ├── Materials/
│   └── Meshes/
├── Instructions/
├── Plugins/
│   ├── TempoROS/
│   └── unrealgt/
├── Source/
│   ├── simulator/
│   └── simulatorEditor/
├── Tools/
│   ├── Terrain_Generation/
│   ├── convert_depth.py
│   └── draw_bounding_boxes.py
├── bags/
├── docker/
│   ├── scripts/
│   ├── Dockerfile
│   └── docker-compose.yml
├── ros2_ws/
│   └── src/simulator_ros/
├── docs/
├── LunarSimPG.uproject
├── setup.sh
├── build.sh
└── open_project.sh
```

## Project and configuration

| Path | Responsibility |
| --- | --- |
| `LunarSimPG.uproject` | Declares Unreal Engine 5.7, runtime/editor modules, Chaos Vehicles, TempoROS, and UnrealGT. |
| `Config/DefaultEngine.ini` | Selects the `fresh_crater` startup/default map and Linux renderer settings. |
| `Config/DefaultPlugins.ini` | Sets the TempoROS fixed frame, ROS domain ID, and RMW implementation. |
| `Config/DefaultInput.ini` | Defines rover keyboard input, including W/S throttle or brake and A/D steering. |
| `Instructions/INSTRUCTIONS.md` | Earlier project setup notes. The task-oriented pages under `docs/` are the canonical user guide. |

## Unreal source

| Path | Responsibility |
| --- | --- |
| `Source/simulator/Private/Capture/` | Capture state, frame scheduling, session metadata, manifests, and trajectory CSV output. |
| `Source/simulator/Private/Sensors/` | Stereo RGB, camera calibration, IMU, and ground-truth camera integration. |
| `Source/simulator/Private/Robots/` | Manual rover control, `cmd_vel` control, live pose, odometry, path, and TF. |
| `Source/simulator/Private/Maps/` | Occupancy/elevation sampling, ROS map publication, and map-file export. |
| `Source/simulator/Private/Utils/` | Dataset-run lifecycle and Unreal-to-ROS conversions. |
| `Source/simulator/Public/Utils/LunarSimRosInterface.h` | Canonical project-owned ROS topic names. |
| `Source/simulatorEditor/Private/simulatorEditor.cpp` | Simulator Config window, setting application, and world-setup actions. |
| `Source/simulatorEditor/Private/RockBaking/` | JSON validation and editor-time rock instance baking. |

See [Development](development.md) for extension points.

## Unreal content

`Content/` holds source-controlled Unreal assets. The configured maps are:

| Level asset | Name |
| --- | --- |
| `Content/Levels/fresh_crater.umap` | Default editor and game map |
| `Content/Levels/mare.umap` | Mare level |
| `Content/Levels/apollo17.umap` | Apollo 17 level |
| `Content/Levels/highland.umap` | Highland level |
| `Content/Levels/custom.umap` | Custom level |

The rover Blueprint is
`Content/3D_Models/LargeRover/Model/ESA_Rover.uasset`. Ground-truth and
environment Blueprints are under `Content/Blueprints/`; reusable rock meshes
are under `Content/Meshes/`.

## Plugins

- `Plugins/TempoROS/` provides embedded ROS 2 Humble, TempoROS node APIs,
  message conversion, TF, and simulation clock support. `setup.sh` prepares its
  third-party headers and Linux libraries.
- `Plugins/unrealgt/` provides the UnrealGT capture plugin used by the
  ground-truth camera Blueprint.

Both plugins are source-controlled project plugins; they are not fetched as Git
submodules in the current repository.

## Tools and generated data

- `Tools/Terrain_Generation/` contains the Tk launcher, heightmap and rockfield
  generators, analysis tools, generated run packages, and Unreal-import copies.
- `Tools/convert_depth.py` decodes UnrealGT RGB-packed depth into grayscale
  previews.
- `Tools/draw_bounding_boxes.py` overlays exported bounding boxes on RGB
  frames.
- `docker/` defines the ROS 2 Humble environment and capture-control scripts.
- `ros2_ws/src/simulator_ros/` is the project ROS 2 package for joystick
  teleoperation.
- `bags/` is mounted into the container for rosbag output.
- `Saved/Datasets/` is created by Unreal at runtime and contains dataset runs.

Generated directories are not part of the module layout. Do not edit
`Binaries/`, `Intermediate/`, `Saved/`, or the colcon `build/`, `install/`, and
`log/` directories as source.

