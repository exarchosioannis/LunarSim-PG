# Capture Synchronization: ROS 2 + UnrealGT

This project captures synchronized simulator data from Unreal Engine for robotics, perception, stereo vision, and navigation experiments.

The main rule is simple:

```text
One captured frame = one shared frame_index + one shared timestamp.
```

For every capture step, `CaptureManager` creates the shared frame information:

```text
SessionId
FrameIndex
StampSeconds
```

The same frame information is used by ROS 2 messages, UnrealGT files, `manifest.csv`, and navigation ground-truth files. This allows the dataset to be matched directly by `frame_index`, without approximate timestamp matching.

---

## Outputs

A capture session can produce:

```text
ROS 2 data:
  /left_camera/rgb/image_raw
  /left_camera/camera_info
  /left_camera/frame_index
  /right_camera/rgb/image_raw
  /right_camera/camera_info
  /right_camera/frame_index
  /rover/gt/pose

UnrealGT data:
  RGB images
  depth images
  segmentation images
  bounding boxes
  segmentation metadata

CSV files:
  manifest.csv
  Navigation/rover_gt_trajectory_ros.csv
```

`stamp_seconds` is Unreal world time in seconds, measured from the start of the Play session.

---

## Session folder structure

Each session is stored under:

```text
Saved/Datasets/<session_name>/
```

Folder layout:

```text
Saved/Datasets/<session_name>/
├── manifest.csv
├── Images/
│   ├── RGB/
│   ├── Depth/
│   ├── Segmentation/
│   ├── BoundingBoxes/
│   └── SegmentationInfo/
│       └── segmentation_info.json
└── Navigation/
    └── rover_gt_trajectory_ros.csv
```

`CaptureManager` creates the session folder, `manifest.csv`, `Images/`, and `Navigation/`. UnrealGT writes its files inside the session `Images/` directory.

---

## Capture modes

The simulator config supports:

```text
Mono ROS
Ground Truth
Stereo ROS
Mono ROS + Ground Truth
Stereo ROS + Ground Truth
```

Current behavior:

```text
Mono ROS:
  left ROS camera + manifest + navigation trajectory + optional rover GT pose

Ground Truth:
  UnrealGT output + manifest + navigation trajectory + optional rover GT pose

Stereo ROS:
  left ROS camera + right ROS camera + manifest + navigation trajectory + optional rover GT pose

Mono ROS + Ground Truth:
  left ROS camera + UnrealGT output + manifest + navigation trajectory + optional rover GT pose

Stereo ROS + Ground Truth:
  left ROS camera + right ROS camera + UnrealGT output + manifest + navigation trajectory + optional rover GT pose
```

User-facing config:

```text
PublishHz
Capture Mode
Enable ROS Rover GT Pose
Stereo Baseline Cm
```

`/control = 1` starts the whole capture pipeline. `/control = 0` stops it.

---

## Main architecture

```text
RobotCamRig
  high-level coordinator
  controls capture timing
  owns left/right ROS camera components
  owns the internal UnrealGT child camera
  asks CaptureManager for the next frame
  passes the same frame info to all outputs

CaptureManager
  source of truth for SessionId, FrameIndex, StampSeconds
  creates the session directory
  writes manifest.csv
  writes Navigation/rover_gt_trajectory_ros.csv
  records rover, left camera, and right camera poses

RgbCameraCaptureComponent
  owns RGB render target and GPU readback
  stores the pending FCaptureFrameInfo
  returns pixels together with the same frame info

CameraRosPublisherComponent
  owns ROS camera publishers
  publishes image, camera_info, and frame_index
  uses FrameInfo.StampSeconds for ROS timestamps
  uses FrameInfo.FrameIndex for frame matching

RoverRobot
  subscribes to /cmd_vel
  moves the rover
  publishes /rover/gt/pose with the synchronized frame timestamp

GTCamera
  C++ bridge to the UnrealGT Blueprint camera
  receives FrameIndex, StampSeconds, SessionId, and CaptureManager
```

`RobotCamRig` is the only camera rig actor that needs to be placed in the level. The UnrealGT Blueprint camera is spawned inside `RobotCamRig` as a child actor, so it does not need to be placed separately in the level.

---

## Stereo camera setup

The stereo setup uses the left camera as the reference camera:

```text
RobotCamRig origin
├── left ROS camera at local (0, 0, 0)
├── UnrealGT camera aligned with the left camera
└── right ROS camera at local (0, StereoBaselineCm, 0)
```

The left and right cameras are triggered from the same `FCaptureFrameInfo`, so the left and right images for frame `k` share the same timestamp and frame index.

Stereo ROS topics:

```text
/left_camera/rgb/image_raw
/left_camera/camera_info
/left_camera/frame_index

/right_camera/rgb/image_raw
/right_camera/camera_info
/right_camera/frame_index
```

The right camera `camera_info` includes the stereo baseline in the projection matrix:

```text
P_left[3]  = 0
P_right[3] = -fx * baseline_m
```

For example, with:

```text
image width = 1280
horizontal FOV = 90 degrees
StereoBaselineCm = 20
```

then:

```text
fx = width / (2 * tan(FOV / 2))
fx = 1280 / (2 * tan(45 degrees)) = 640 px
baseline_m = 20 / 100 = 0.2 m
P_right[3] = -640 * 0.2 = -128
```

This makes the stereo `camera_info` usable by ROS stereo tools.

---

## Frame synchronization

For frame `25`, the synchronized outputs are:

```text
ROS 2:
  /left_camera/frame_index = 25
  /right_camera/frame_index = 25
  /left_camera/rgb/image_raw       timestamp = T25
  /right_camera/rgb/image_raw      timestamp = T25
  /left_camera/camera_info         timestamp = T25
  /right_camera/camera_info        timestamp = T25
  /rover/gt/pose                   timestamp = T25

UnrealGT:
  Images/RGB/25.png
  Images/Depth/25.png
  Images/Segmentation/25.png
  Images/BoundingBoxes/25.txt

CSV:
  manifest.csv row with frame_index = 25
  Navigation/rover_gt_trajectory_ros.csv row with frame_index = 25
```

The GPU readback and UnrealGT file saving may finish slightly later than the frame trigger, but the stored `FrameIndex` and `StampSeconds` still identify the correct capture moment.

---

## Manifest

`manifest.csv` stores synchronization and Unreal-coordinate poses for every frame.

It includes:

```text
session_id
frame_index
stamp_seconds
rover pose in Unreal coordinates
left camera pose in Unreal coordinates
right camera pose in Unreal coordinates
```

Positions are stored in Unreal centimeters. Rotations are stored in Unreal degrees.

This file is mainly for debugging, dataset validation, and checking the exact left/right camera poses used for stereo capture.

---

## Navigation ground truth

The rover navigation trajectory is written to:

```text
Saved/Datasets/<session_name>/Navigation/rover_gt_trajectory_ros.csv
```

Columns:

```csv
timestamp_sec,frame_index,frame_id,child_frame_id,x_m,y_m,z_m,qx,qy,qz,qw
```

This file stores the rover pose in ROS-style values:

```text
position: meters
orientation: quaternion
frame_id: map
child_frame_id: rover_base
```

The trajectory rows use the same `frame_index` and timestamp as the rest of the capture pipeline.

---

## Coordinate note

The manifest stores poses in Unreal coordinates:

```text
position: centimeters
rotation: degrees
```

The ROS rover pose and navigation trajectory use ROS-style coordinates:

```text
position: meters
orientation: quaternion
```

Current rover position conversion:

```text
ros_x = unreal_x / 100.0
ros_y = -unreal_y / 100.0
ros_z = unreal_z / 100.0
```

Current rover yaw conversion:

```text
ros_yaw = -unreal_yaw
```

So the manifest and ROS/navigation outputs describe the same physical pose, but not always with the same numeric coordinate values.

---

## Shutdown flow

UnrealGT writes image files asynchronously. To avoid losing final files:

```text
1. Start capture
2. Stop capture with /control = 0
3. Wait 3-5 seconds
4. Stop Play / PIE
```

A future improvement is to add an explicit flush/wait mechanism for pending UnrealGT save tasks.

---

## Notes

A timestamp difference of `0.000000001` seconds is one nanosecond. This can happen when converting between C++ `double`, CSV text, and ROS `sec/nanosec`. It is safe for evaluation and far smaller than a normal frame interval.

`segmentation_info.json` is metadata, not one file per frame. Per-frame segmentation images are stored in `Images/Segmentation/`.

---

## Suggested future improvements

```text
- Add ROS-style left/right camera trajectory files
- Add TUM trajectory export for SLAM evaluation
- Add /gt/odom as nav_msgs/Odometry
- Add /tf transform map -> rover_base
- Add /gt/path as nav_msgs/Path
- Add explicit flush/wait for pending UnrealGT save tasks on capture stop
    fileNameAndExt = os.path.splitext(filename)
- Add object metadata and instance-mask outputs
```

NOTE IN THE END: (DONT REMOVE IT)
Turn your current manifest pose rows into proper ROS navigation ground truth:
manifest + /gt/odom + /tf + /gt/path + trajectory files.