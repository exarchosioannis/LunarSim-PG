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

The simulator uses one unified capture session for both closed-loop ROS experiments and ground-truth-rich dataset generation. Online ROS topics and enabled offline files share a frame index and timestamp.

## High-Level Architecture

At runtime, a rover exists in the UE level. `ARobotCamRig` can attach itself to the rover sensor mount, then creates and manages the left and right camera components.

Capture is coordinated by `UCaptureManager`. Press `C` to start or stop the configured capture outputs. A ROS-only session is selected by enabling stereo ROS images and disabling ground-truth images and trajectory CSV; maps remain independently optional. Enabling the disk outputs uses the same synchronized session path. `rosbag` remains an external recorder and does not control simulator capture state.

When capture starts, the manager starts a numbered session under the current run in `Saved/Datasets` and creates a `manifest.csv`. For every capture frame, it creates one `FCaptureFrameInfo` containing:

- `frame_index`
- `timestamp_sec`
- `session_id`

`ARobotCamRig` uses that frame info to trigger the enabled capture outputs for the same frame:

- ROS stereo RGB images and CameraInfo
- UnrealGT capture through the ground-truth camera actor
- trajectory CSV rows for the rover and cameras

The IMU, ground-truth pose/odometry/path, dynamic and static TF, `/cmd_vel`, capture-control subscriber, and TempoROS clock have PIE-owned lifecycles independent of capture. Starting capture resets the accumulated ground-truth path but does not recreate or gate those endpoints.

Map generation is handled separately by `UOccupancyMapPublisherComponent`. It raycasts the level to produce occupancy, elevation, and elevation point cloud outputs. The map files are written under the dataset run `Maps` directory.

The rover also has an independent IMU sensor through `UImuSensorPublisherComponent`. The IMU is mounted using `IMU_Mount`, publishes `/imu/data` continuously for the PIE lifetime at its own configurable rate, and is synchronized with the rest of the simulator by ROS timestamp. Rover control—manual WASD/controller or `/cmd_vel`, according to the configured control mode—also remains available independently of `C`.

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
| `Capture/CaptureTypes.h` | `FCaptureConfig`, `FCaptureFrameInfo` | Defines publish rate, enabled outputs, and per-frame synchronization metadata. |
| `Capture/CapturePoseTypes.h` | `FCapturePose`, `FCaptureFramePoseData` | Defines rover and camera pose data stored alongside synchronized frames. |
| `Sensors/RobotCamRig.*` | `ARobotCamRig` | Main camera rig actor. Attaches to the rover, manages stereo cameras, starts capture frames, triggers ROS RGB and UnrealGT capture, and publishes camera TFs. |
| `Sensors/RgbCameraCaptureComponent.*` | `URgbCameraCaptureComponent` | Captures RGB from a `USceneCaptureComponent2D` using asynchronous GPU readback. |
| `Sensors/CameraRosPublisherComponent.*` | `UCameraRosPublisherComponent` | Publishes one canonical ROS image and CameraInfo pair. The left-camera node also listens to `/capture/control`. |
| `Sensors/GTCamera.*` | `AGTCamera` | Actor wrapper used by Blueprint ground-truth cameras to call UnrealGT generator triggers with frame metadata. |
| `Sensors/ImuSensorPublisherComponent.*` | `UImuSensorPublisherComponent` | Publishes the rover IMU topic from `IMU_Mount`, including static TF `base_link -> imu_link`, optional noise, and optional bias. |
| `Robots/RoverCmdVelVehicleControllerComponent.*` | `URoverCmdVelVehicleControllerComponent` | Subscribes to `/cmd_vel` and converts ROS twist commands into Chaos vehicle throttle, steering, and brake input. |
| `Robots/RoverGroundTruthPublisherComponent.*` | `URoverGroundTruthPublisherComponent` | Publishes PIE-continuous ground-truth pose, odometry, path, and dynamic TF. Also acts as a capture pose source. |
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

`UCaptureManager` creates this metadata once per capture frame. The same frame info is then used for ROS camera-message stamps, UnrealGT captures, `manifest.csv`, rover trajectory CSV, and camera trajectory CSV files. Frame index remains an internal manifest/trajectory key; it is not published as a separate ROS topic.

The intent is that outputs with the same `session_id` and `frame_index` refer to the same capture frame. `timestamp_sec` is the UE world time converted into ROS message stamps where applicable.

The IMU is different from the frame-based capture outputs. It runs continuously at its own rate and is synchronized by ROS timestamp, not by `frame_index`.

`URgbCameraCaptureComponent` uses asynchronous GPU readback. The code keeps the `FCaptureFrameInfo` with the pending readback and delays the GPU copy until a later tick so that the published ROS image corresponds to the intended capture frame rather than the previous render target contents.

## ROS Topics

The locked project interface is below. All project-owned endpoints are reliable. `KEEP_LAST(N)` is shown as `KL(N)`.

| Lifecycle | Topic | Type | QoS |
| --- | --- | --- | --- |
| Capture-gated | `/stereo/left/image_raw` | `sensor_msgs/msg/Image` (`bgr8`) | KL(20), volatile |
| Capture-gated | `/stereo/left/camera_info` | `sensor_msgs/msg/CameraInfo` | KL(20), volatile |
| Capture-gated | `/stereo/right/image_raw` | `sensor_msgs/msg/Image` (`bgr8`) | KL(20), volatile |
| Capture-gated | `/stereo/right/camera_info` | `sensor_msgs/msg/CameraInfo` | KL(20), volatile |
| PIE-continuous | `/imu/data` | `sensor_msgs/msg/Imu`, frame `imu_link` | KL(50), volatile |
| PIE-continuous | `/ground_truth/pose` | `geometry_msgs/msg/PoseStamped`, frame `map` | KL(10), volatile |
| PIE-continuous | `/ground_truth/odom` | `nav_msgs/msg/Odometry`, `map -> base_link` semantics | KL(10), volatile |
| PIE-continuous; reset on capture start | `/ground_truth/path` | `nav_msgs/msg/Path`, frame `map` | KL(1), transient local |
| Generated once when maps are enabled | `/ground_truth/map/occupancy` | `nav_msgs/msg/OccupancyGrid`, frame `map` | KL(1), transient local |
| Generated once when maps are enabled | `/ground_truth/map/elevation_points` | `sensor_msgs/msg/PointCloud2`, frame `map` | KL(1), transient local |
| PIE-continuous subscriber | `/capture/control` | `std_msgs/msg/Int32`; `1` starts, `0` stops | KL(1), volatile |
| PIE-continuous subscriber | `/cmd_vel` | `geometry_msgs/msg/Twist`; m/s and rad/s | KL(1), volatile |
| PIE-continuous | `/tf` | `tf2_msgs/msg/TFMessage`; dynamic `map -> base_link` | official tf2 dynamic broadcaster |
| PIE-latched | `/tf_static` | `tf2_msgs/msg/TFMessage`; camera and IMU mounts | official tf2 static broadcaster |
| PIE-continuous | `/clock` | `rosgraph_msgs/msg/Clock` | TempoROS clock QoS |

The camera image and CameraInfo messages share an exact stamp within each side and across the stereo pair. Static frames form `base_link -> camera_link -> camera_optical_frame` for each camera plus `base_link -> imu_link`. Ground-truth pose, odometry, and dynamic TF describe the same `map -> base_link` state. External compatibility with previous topic names should use ROS remapping; the simulator does not create duplicate legacy endpoints.

For the validated 640x640, 90-degree FOV, 0.20 m baseline, 6 Hz run, record the interface with:

```bash
ros2 bag record \
  /stereo/left/image_raw /stereo/left/camera_info \
  /stereo/right/image_raw /stereo/right/camera_info \
  /imu/data /ground_truth/pose /ground_truth/odom /ground_truth/path \
  /ground_truth/map/occupancy /ground_truth/map/elevation_points \
  /tf /tf_static /clock /cmd_vel
```

While PIE is running, the live validator is read-only unless an active-test flag is supplied:

```bash
python3 Tools/validate_lunarsimpg_ros_interface.py --graph-only
python3 Tools/validate_lunarsimpg_ros_interface.py --window 10 \
  --expected-capture-hz 6 --expected-baseline-m 0.20
```

The following commands explicitly change simulator state; the second moves the rover:

```bash
python3 Tools/validate_lunarsimpg_ros_interface.py --test-capture-control
python3 Tools/validate_lunarsimpg_ros_interface.py --test-cmd-vel
```

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
│   ├── elevation_map_preview.pgm
│   ├── slope_map.csv
│   ├── slope_map.yaml
│   └── slope_map_preview.pgm
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
- ROS CameraInfo paired exactly with stereo images
- UnrealGT image outputs such as RGB, depth, segmentation, and actor/bounding-box data when configured in the ground-truth camera Blueprint
- rover pose, path, and TF
- static camera and IMU TF tree
- simulated rover IMU with optional noise and bias
- occupancy and elevation map outputs
- elevation point cloud publishing






BP_UnrealGT_Camera
RGB COMPONENT 
TARGET GAMMA 2.2
