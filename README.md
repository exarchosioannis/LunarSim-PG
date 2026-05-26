# Lunar Environment Simulator

## Overview

Lunar Environment Simulator is an Unreal Engine 5 project for rover perception and navigation dataset generation.

The project combines:

- a UE5 lunar rover simulation
- ROS 2 control and publishing through TempoROS
- synchronized stereo RGB capture
- UnrealGT ground-truth image outputs
- rover pose, path, TF, and IMU outputs
- occupancy and elevation map outputs

The main purpose is to generate ground-truth-rich autonomy datasets where online ROS topics and offline files can be matched by a shared frame index and timestamp.

## High-Level Architecture

At runtime, a rover exists in the UE level. `ARobotCamRig` can attach itself to the rover sensor mount, then creates and manages the left and right camera components.

Capture is coordinated by `UCaptureManager`. When capture starts, it creates a dataset run under `Saved/Datasets`, starts a numbered session, and creates a `manifest.csv`. For every capture frame, it creates one `FCaptureFrameInfo` containing:

- `frame_index`
- `timestamp_sec`
- `session_id`

`ARobotCamRig` uses that frame info to trigger the enabled outputs for the same frame:

- ROS RGB image, camera info, and frame index topics
- UnrealGT capture through the ground-truth camera actor
- rover ground-truth pose, path, and dynamic TF
- trajectory CSV rows for the rover and cameras

Map generation is handled separately by `UOccupancyMapPublisherComponent`. It raycasts the level to produce occupancy, elevation, and elevation point cloud outputs. The map files are written under the dataset run `Maps` directory.

The rover also has an independent IMU sensor through `UImuSensorPublisherComponent`. The IMU is mounted using `IMU_Mount`, publishes `/rover/imu` at its own configurable rate, and is synchronized with the rest of the simulator by ROS timestamp.

## Current Level Setup Notes

For now, the level expects `BP_GroundTruthMapPublisher` to be placed at the center of the map, currently world location `0, 0, 0`. The map publisher uses its owner actor location as the center of the generated map area.

`RobotCamRig` also needs these references set in the Details panel:

| Setting | Value |
| --- | --- |
| `RoverActor` | The rover actor in the level. |
| `Rover Sensor Mount Component Name` | `RoverSensorMount` |
| `Attach To Rover Sensor Mount On Begin Play` | Enabled |
| `Ground Truth Camera Class` | `BP_UnrealGT_Camera` |
| `IMU_Mount` | Scene component on the rover used by the IMU sensor. |

## Source/simulator Folder Structure

| Folder | Responsibility |
| --- | --- |
| `Capture/` | Capture configuration, frame metadata, session directories, manifest writing, and synchronized trajectory CSV output. |
| `Sensors/` | Camera rig, RGB render-target capture, ROS camera publishers, IMU sensor publisher, and the bridge actor used to trigger UnrealGT. |
| `Robots/` | ROS rover command input and synchronized rover ground-truth publishing. |
| `Maps/` | Ground-truth occupancy, elevation, point cloud publishing, and map file export. |
| `Utils/` | Shared Unreal-to-ROS coordinate conversion helpers. |

`Public/` contains the exported declarations used by UE and Blueprints. `Private/` contains the implementation.

## Important Classes and Files

| File | Main class/component | Purpose |
| --- | --- | --- |
| `Capture/CaptureManager.*` | `UCaptureManager` | Owns capture sessions, frame indices, timestamps, dataset paths, `manifest.csv`, and trajectory CSV output. |
| `Capture/CapturePoseSourceComponent.*` | `UCapturePoseSourceComponent` | Provides a named world pose source, normally used for the rover base pose. |
| `Capture/CaptureTypes.h` | `FCaptureConfig`, `FCaptureFrameInfo` | Defines capture modes, publish rate, enabled outputs, and per-frame synchronization metadata. |
| `Capture/CapturePoseTypes.h` | `FCapturePose`, `FCaptureFramePoseData` | Defines rover and camera pose data stored alongside synchronized frames. |
| `Sensors/RobotCamRig.*` | `ARobotCamRig` | Main camera rig actor. Attaches to the rover, manages stereo cameras, starts capture frames, triggers ROS RGB and UnrealGT capture, and publishes camera TFs. |
| `Sensors/RgbCameraCaptureComponent.*` | `URgbCameraCaptureComponent` | Captures RGB from a `USceneCaptureComponent2D` using asynchronous GPU readback. |
| `Sensors/CameraRosPublisherComponent.*` | `UCameraRosPublisherComponent` | Publishes ROS image, camera info, and frame index topics for one camera. The left camera also listens to `/control`. |
| `Sensors/GTCamera.*` | `AGTCamera` | Actor wrapper used by Blueprint ground-truth cameras to call UnrealGT generator triggers with frame metadata. |
| `Sensors/ImuSensorPublisherComponent.*` | `UImuSensorPublisherComponent` | Publishes the rover IMU topic from `IMU_Mount`, including static TF `base_link -> imu_link`, optional noise, and optional bias. |
| `Robots/RoverCmdVelVehicleControllerComponent.*` | `URoverCmdVelVehicleControllerComponent` | Subscribes to `/cmd_vel` and converts ROS twist commands into Chaos vehicle throttle, steering, and brake input. |
| `Robots/RoverGroundTruthPublisherComponent.*` | `URoverGroundTruthPublisherComponent` | Publishes synchronized rover pose, path, and dynamic TF. Also acts as a capture pose source. |
| `Maps/OccupancyMapPublisherComponent.*` | `UOccupancyMapPublisherComponent` | Generates and publishes occupancy and elevation point cloud map topics. Exports map files. |
| `Maps/GroundTruthMapFileExporter.*` | `FGroundTruthMapFileExporter` | Writes map files such as occupancy PGM/YAML and elevation CSV/YAML/preview. |
| `Maps/GroundTruthElevationPointCloudBuilder.*` | `FGroundTruthElevationPointCloudBuilder` | Converts the elevation grid into a ROS `PointCloud2`. |
| `Utils/UnrealToRosConversion.*` | `UnrealToRosConversion` | Converts Unreal positions, rotations, and transforms into ROS coordinates. |
| `simulator.Build.cs` | `simulator` module rules | Declares module dependencies including TempoROS, rclcpp, RHI, RenderCore, UnrealGT, and ChaosVehicles. |
| `simulator.cpp` | primary game module | Registers the `simulator` runtime module. |
| `simulator.h` | module header | Minimal shared module header. |

## Synchronization Model

The synchronization key is:

```text
session_id + frame_index + timestamp_sec
```

`UCaptureManager` creates this metadata once per capture frame. The same frame info is then used for ROS camera messages, `/frame_index`, UnrealGT captures, `manifest.csv`, rover trajectory CSV, and camera trajectory CSV files.

The intent is that outputs with the same `session_id` and `frame_index` refer to the same capture frame. `timestamp_sec` is the UE world time converted into ROS message stamps where applicable.

The IMU is different from the frame-based capture outputs. It runs continuously at its own rate and is synchronized by ROS timestamp, not by `frame_index`.

`URgbCameraCaptureComponent` uses asynchronous GPU readback. The code keeps the `FCaptureFrameInfo` with the pending readback and delays the GPU copy until a later tick so that the published ROS image corresponds to the intended capture frame rather than the previous render target contents.

## ROS Topics

Topics supported by the simulator code:

| Category | Topic | Message type / purpose |
| --- | --- | --- |
| Control | `/control` | `std_msgs/msg/Int32`; starts capture on `1`, stops capture on `0`. |
| Control | `/cmd_vel` | ROS twist command used by the rover vehicle controller. |
| Left camera | `/left_camera/rgb/image_raw` | `sensor_msgs/msg/Image`; BGRA8 RGB image. |
| Left camera | `/left_camera/camera_info` | `sensor_msgs/msg/CameraInfo`. |
| Left camera | `/left_camera/frame_index` | `std_msgs/msg/Int32`; synchronized frame index. |
| Right camera | `/right_camera/rgb/image_raw` | `sensor_msgs/msg/Image`; enabled in stereo capture modes. |
| Right camera | `/right_camera/camera_info` | `sensor_msgs/msg/CameraInfo`; includes stereo projection Tx for the right camera. |
| Right camera | `/right_camera/frame_index` | `std_msgs/msg/Int32`; synchronized frame index. |
| Rover ground truth | `/gt/rover/pose` | `geometry_msgs/msg/PoseStamped`. |
| Rover ground truth | `/gt/rover/path` | `nav_msgs/msg/Path`. |
| Rover IMU | `/rover/imu` | `sensor_msgs/msg/Imu`; independent simulated IMU stream from `imu_link`. |
| TF | `/tf` | Dynamic rover transform `map -> base_link`. |
| TF | `/tf_static` | Static sensor transforms published through TempoROS, including camera frames and `base_link -> imu_link`. |
| Maps | `/gt/map/occupancy` | `nav_msgs/msg/OccupancyGrid`. |
| Maps | `/gt/map/elevation_points` | `sensor_msgs/msg/PointCloud2`. |

## Dataset Outputs

Capture outputs are written under `Saved/Datasets`. A dataset run is named by timestamp. Capture sessions are numbered inside the run.

Example structure:

```text
Saved/Datasets/YYYY-MM-DD_HH-MM-SS/
├── Maps/
│   ├── occupancy_map.pgm
│   ├── occupancy_map.yaml
│   ├── elevation_map.csv
│   ├── elevation_map.yaml
│   └── elevation_map_preview.pgm
└── Session_001/
    ├── manifest.csv
    ├── Images/
    │   └── <UnrealGT generator/streamer output folders>
    └── Navigation/
        ├── rover_gt_trajectory_ros.csv
        ├── left_camera_gt_trajectory_ros.csv
        └── right_camera_gt_trajectory_ros.csv
```

`manifest.csv` is the per-frame synchronization index. It records `session_id`, `frame_index`, `timestamp_sec`, and flags indicating which outputs exist for that frame.

The `Navigation` CSV files store ROS-frame poses for the rover and cameras. Each row includes `timestamp_sec`, `frame_index`, `frame_id`, `child_frame_id`, position in meters, and quaternion orientation.

UnrealGT image outputs are redirected into the session `Images` directory when triggered through `GTCamera` and `CaptureManager`. The exact subfolder names depend on the configured UnrealGT generator and file streamer components, for example RGB, depth, segmentation, or bounding-box outputs.

## Coordinate Conversion

Coordinate conversion is centralized in `UnrealToRosConversion`.

Unreal Engine uses centimeters. ROS outputs use meters. The conversion also handles the coordinate-system difference used by this project: Unreal `Y` is converted to ROS `-Y`, while `X` remains forward and `Z` remains up.

This helper is used by rover ground-truth publishing and by trajectory CSV writing.

## Plugins

| Plugin | Used for |
| --- | --- |
| `TempoROS` | ROS 2 integration inside Unreal. The simulator uses it to create ROS nodes, publish camera/map/ground-truth topics, subscribe to control topics, and publish TF transforms. |
| `UnrealGT` | Ground-truth data generation. The simulator triggers UnrealGT from `GTCamera` so image outputs can share the same frame index, timestamp, session id, and dataset directory as the ROS capture pipeline. |
| `ChaosVehiclesPlugin` | Vehicle movement support for the rover. `/cmd_vel` commands are converted into Chaos vehicle inputs. |

The UnrealGT plugin in this repository is modified for this simulator. It supports frame-aware triggering, session validation, dataset directory overrides, and asynchronous GPU readback for image generation.

## Current Scope

The current code focuses on:

- synchronized mono or stereo ROS RGB capture
- ROS camera info and frame index topics
- UnrealGT image outputs such as RGB, depth, segmentation, and actor/bounding-box data when configured in the ground-truth camera Blueprint
- rover pose, path, and TF
- static camera and IMU TF tree
- simulated rover IMU with optional noise and bias
- occupancy and elevation map outputs
- elevation point cloud publishing

LiDAR is not implemented in `Source/simulator` at the moment and would be a future extension.