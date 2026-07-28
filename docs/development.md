# Development

This page describes extension points present in the current source. The
repository has no automated C++/ROS integration test suite, so changes require
an Unreal build and manual Play In Editor verification.

## Build system

The Unreal project declares:

| Target/module | Type | Source |
| --- | --- | --- |
| `simulator` | Game target and runtime module | `Source/simulator*` |
| `simulatorEditor` | Editor target/module | `Source/simulatorEditor*` |

Both targets use Unreal 5.7 include ordering and build settings V6. The runtime
module depends on TempoROS, `rclcpp`, UnrealGT, Chaos Vehicles, RHI,
RenderCore, ImageWrapper, Landscape, and the Unreal UI modules used at runtime.
The editor module adds UnrealEd, LevelEditor, ToolMenus, AssetRegistry, JSON,
DesktopPlatform, and the runtime module.

Build from the repository root:

```bash
export UE_ROOT=/path/to/UnrealEngine-5.7.x
./setup.sh
./build.sh
```

The supported target clean is:

```bash
./build.sh --clean
```

There is no repository script for deleting Unreal build directories or colcon
build products. Additional clean procedures are **To be documented**.

## Runtime entry points

| Area | Important classes/files |
| --- | --- |
| Capture lifecycle | `UCaptureManager`, `ARobotCamRig`, `UDatasetRunSubsystem`, `FCaptureConfig` |
| Pose capture | `UCapturePoseSourceComponent`, `FCapturePose`, `FCaptureFrameInfo` |
| Stereo ROS | `URgbCameraCaptureComponent`, `UCameraRosPublisherComponent`, `ARobotCamRig` |
| Ground-truth camera | `AGTCamera` plus the configured UnrealGT Blueprint child |
| IMU | `UImuSensorPublisherComponent` |
| Rover control | `URoverVehicleControllerComponent`, `URoverCmdVelVehicleControllerComponent` |
| Live rover truth | `URoverGroundTruthPublisherComponent` |
| Maps | `UOccupancyMapPublisherComponent`, `FGroundTruthMapFileExporter`, `FGroundTruthElevationPointCloudBuilder` |
| ROS names | `Source/simulator/Public/Utils/LunarSimRosInterface.h` |
| Coordinate conversion | `UnrealToRosConversion` |

`ARobotCamRig` coordinates camera configuration, capture state, GPU readback,
ROS publication, ground-truth warm-up, TF, keyboard capture, and three-second
finalization. `UCaptureManager` owns session directories, metadata, manifest
rows, frame indices, and trajectories.

## Editor entry points

`FsimulatorEditorModule` in
`Source/simulatorEditor/Private/simulatorEditor.cpp` registers the Simulator
Config window, world setup, setting application, and control-mode UI.

`Source/simulatorEditor/Private/RockBaking/` contains:

- `SRockBakingPanel` for the embedded editor UI;
- `FSimulatorRockBaker` for validation, asset discovery, terrain tracing, and
  HISM creation;
- `USimulatorRockSettings` for JSON-file and mesh-folder selection.

Editor operations must remain guarded against Play In Editor mutations.

## Add a ROS 2 topic

Follow the existing publisher/subscriber components:

1. Add one canonical topic constant to
   `Public/Utils/LunarSimRosInterface.h`.
2. Place the component in the runtime area that owns the data.
3. Create a TempoROS node during `BeginPlay` or component initialization.
4. Register one typed publisher or subscription with an explicit
   `FROSQOSProfile`.
5. Reuse message storage when publishing at frame rate.
6. Set header time and frame IDs through the existing world-time and
   Unreal-to-ROS conversion paths.
7. Tear down references in `EndPlay`.
8. Wire the component into `ESA_Rover`, the camera rig, or the map actor as
   appropriate.
9. Update [ROS 2 interface](ros2-interface.md) and test discovery, type, QoS,
   rate, frame, and restart behavior.

TempoROS and `rclcpp` are already runtime module dependencies. If a new message
package needs another build dependency, the exact TempoROS integration process
is **To be documented**; do not assume standard system ROS linking.

## Add a sensor

Use `UImuSensorPublisherComponent` as the independent-rate component pattern,
or the RGB path when the sensor needs capture-frame scheduling:

1. Define the sensor component and explicit mounting frame.
2. Decide whether it publishes continuously or participates in active capture.
3. Convert Unreal axes/units with `UnrealToRosConversion` or a documented
   sensor-frame conversion.
4. Publish its static transform from `base_link`.
5. Add it to the rover Blueprint and editor pipeline resolution.
6. If it is file-backed, extend capture config, session metadata, manifest
   paths, and output documentation together.

There is no generic sensor registry or automatic Blueprint wiring. The exact
asset-editing steps for a new sensor are **To be documented** per sensor.

## Add a ground-truth modality

The current file-backed modalities are selected in `FCaptureConfig`, applied by
`AGTCamera`, and implemented by the UnrealGT camera Blueprint.

To preserve frame association:

1. Add an output switch and resolved helper to `FCaptureConfig`.
2. Expose and apply the switch in Simulator Config.
3. Add the UnrealGT generator/component to the ground-truth camera.
4. Pass `FrameIndex`, `StampSeconds`, and `SessionId` through the existing
   `CaptureGroundTruthNow` event.
5. Extend session directory setup, `manifest.csv`, and
   `session_metadata.json`.
6. Write files with the capture frame index as the filename stem.
7. Add a completeness check to the manual validation procedure.

The manifest is currently written before asynchronous image completion.
Introducing a modality is not complete until missing-write behavior is
detectable.

## Add a regional profile

Profiles are embedded in `Tools/Terrain_Generation/moonsim.sh`:

1. Add user-visible defaults to `PRESET_DEFAULTS`.
2. Add the heightmap and rock profile names to the corresponding GUI choice
   lists.
3. Add crater segment definitions to `CRATER_SEGMENT_TEMPLATES`.
4. Confirm every rock field is populated.
5. Generate a heightmap package and a rockfield package with a fixed seed.
6. Validate the JSON headers and import scales, then import/bake in a test
   level.

If adding a heightmap-only preset, also update the choices accepted by
`heightmap_generator.py`. Profile files are not dynamically discovered.

## Add a terrain output

The heightmap package writer is in `heightmap_generator.py`; GUI packaging and
Unreal-import copies are finalized in `moonsim.sh`. Rockfield output is owned
by `rockfield_generator.py`.

Add an output only after defining:

- its filename and JSON/schema version;
- coordinate system and units;
- seed/source provenance;
- GUI packaging and analysis behavior;
- Unreal consumer, if any;
- documentation in [Terrain generation](terrain-generation.md) and
  [Output format](output-format.md).

There is no general terrain-product plugin interface. Changes to a producer
and its Unreal consumer must be coordinated manually.

## Test a change

From the repository root, non-runtime checks supported by current files are:

```bash
bash -n setup.sh build.sh open_project.sh
./build.sh --help
./open_project.sh --help
python3 Tools/Terrain_Generation/heightmap_generator.py --help
python3 Tools/Terrain_Generation/rockfield_generator.py --help
python3 Tools/convert_depth.py --help
python3 Tools/draw_bounding_boxes.py --help
docker compose -f docker/docker-compose.yml config --quiet
```

Then:

1. Run `./build.sh`.
2. Open the affected level and inspect the Output Log.
3. Start Play In Editor.
4. Verify `/clock`, changed topics, QoS, TF, and rates from the sourced
   container.
5. Start/stop one short capture and wait for finalization.
6. Check every enabled manifest path and corresponding trajectory row.
7. If terrain changed, generate twice with the same seed and compare the
   produced packages using the project's intended tolerance. That comparison
   tolerance is **To be documented**.

No automated unit, functional, dataset-validation, or frame-synchronization
test suite is present.

## Validate frame synchronization

For one short capture:

1. Treat `manifest.csv` as the expected frame set.
2. Verify every enabled file uses the manifest `frame_index` as its stem.
3. Verify rover/left/right trajectory rows use matching frame indices and
   `timestamp_sec`.
4. Compare ROS image and CameraInfo header stamps for equality.
5. Use timestamps—not row position—to correlate independently scheduled IMU,
   odometry, TF, and rosbag messages.
6. Report missing asynchronous image files separately from intentionally
   disabled modalities or invalid pose rows.

This is a manual procedure. A machine-readable validator and acceptance
tolerances remain **To be documented**.
