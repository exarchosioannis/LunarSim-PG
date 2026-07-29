# Unreal simulator

LunarSim-PG is an Unreal Engine 5.7.x editor project. The repository provides
an editor build and Play In Editor workflow; packaged and headless launch
targets are not documented by project scripts.

## Project modules and plugins

| Component | Type | Responsibility |
| --- | --- | --- |
| `simulator` | Runtime module | Rover control, cameras, IMU, ROS 2, capture, datasets, maps, and coordinate conversion |
| `simulatorEditor` | Editor module | Simulator Config, world setup, and rockfield baking |
| Chaos Vehicles | Engine plugin | Rover vehicle physics |
| TempoROS | Project plugin | Embedded ROS 2 Humble, clock, TF, publishers, and subscribers |
| UnrealGT | Project plugin | Ground-truth RGB, depth, segmentation, and bounding-box capture |

## Levels

The editor and game default are `/Game/Levels/fresh_crater`. Source-controlled
maps are:

| Content path | Level |
| --- | --- |
| `/Game/Levels/fresh_crater` | Fresh crater; default |
| `/Game/Levels/mare` | Mare |
| `/Game/Levels/apollo17` | Apollo 17 |
| `/Game/Levels/highland` | Highlands |
| `/Game/Levels/custom` | Custom |

The terrain-generator presets and these level assets are separate. Selecting a
profile in the Python GUI does not select or update an Unreal level.

## Build and open

Working directory: repository root, after setup has saved `.ue_root` (or with a
`UE_ROOT` override).

```bash
./build.sh
./open_project.sh
```

For the target-level clean build supported by the script:

```bash
./build.sh --clean
```

See [Installation](installation.md) for the complete setup.

## Simulator Config

Open **Window > Simulator > Simulator Config**. The window contains:

- world-setup actions;
- capture output switches;
- camera resolution, horizontal FOV, rate, and stereo baseline;
- IMU rate and rover control mode;
- Apply Settings;
- the terrain Rock Baker.

The exact settings and defaults are in [Configuration](configuration.md).

### World setup

Use the setup buttons only while Play In Editor is stopped:

- **Create/Update Sky, Earth and Sun** creates or updates the directional light,
  Earth, Sky, Sun, and `BP_SunGLowController`.
- **Create/Update Rover + Ground Truth** creates or updates `ESA_Rover` and
  `BP_GroundTruthMapPublisher`.

The complete rover pipeline includes vehicle control, the camera rig, capture
manager, live ground-truth publisher, `cmd_vel` controller, and IMU. The map
publisher samples the Landscape and tagged actors.

If automatic rover creation reports a partial result, see
[Rover setup is only partially created](troubleshooting.md#rover-setup-is-only-partially-created).

## Terrain import and rock baking

The terrain generator prepares:

- `Tools/Terrain_Generation/unreal_import/heightmaps/<run>.png`;
- `Tools/Terrain_Generation/unreal_import/rockfields/<run>.json`.

Import the heightmap through the Unreal Landscape editor using the X/Y/Z
scales from the matching generated `metadata.json`. This import step is manual.

In **Simulator Config > Terrain Generation**, select the rockfield JSON and
bake it against rock meshes below `/Game/Meshes/Rocks` unless a different
content folder is chosen. The baker:

- validates `MoonSimOfflineRockField` version 1, meter units, and
  `centered_map_meters`;
- chooses a mesh deterministically from sorted assets using `instance_id`;
- traces `WorldStatic` to place instances on the terrain;
- applies size, surface alignment, yaw, tilt, and burial;
- uses hierarchical instanced static meshes.

Bake and clear operations are disabled during Play In Editor.

## Start and stop simulation

1. Open the desired level.
2. Apply Simulator Config settings.
3. Save the level if prompted.
4. Start Play In Editor with the Unreal toolbar **Play** action.
5. Click **Stop** to end the run.

Starting Play In Editor creates a timestamped directory under
`Saved/Datasets/`, even before a capture session is started. Run-level maps are
generated when ground-truth maps are enabled and the map publisher is present.

## Rover control

Select a control mode before starting play:

| Mode | Input |
| --- | --- |
| WASD | `W` forward, `S` brake/reverse, `A`/`D` steering |
| `cmd_vel` | ROS 2 `/cmd_vel`, using `linear.x` and `angular.z` |
| Disabled | No control commands applied |

The ROS teleop launch file and command limits are documented in
[ROS 2 interface](ros2-interface.md).

## Capture

Capture can be toggled in either of two implemented ways:

- press `C` while the Play In Editor viewport has input focus;
- publish `1` or `0` to `/capture/control`, normally through the container
  scripts:

  ```bash
  bash /ws/scripts/startcapture
  bash /ws/scripts/stopcapture
  ```

When ground-truth outputs are enabled, capture waits for their warm-up state.
Stopping enters a three-second cooldown before the status returns to Ready.
Do not stop Play In Editor until this cooldown completes.

Stereo ROS images and all session image files are produced only during active
capture. Live `/clock`, IMU, rover ground truth, and TF use their independent
runtime schedules.

## Rendering settings

The project config selects Linux Vulkan SM6, desktop maximum feature level, and
ray tracing. Hardware ray-traced Lumen is disabled. Exact supported GPU models,
driver versions, and VRAM requirements are **Not yet verified**.

## Logs

Use **Window > Output Log** for setup, ROS, capture, map, and Rock Baker
messages. Persistent Unreal logs are written below:

```text
Saved/Logs/
```

Capture errors use structured messages containing subsystem, resource, stage,
cause, and effect. Dataset output is separate under `Saved/Datasets/`.
