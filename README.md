# Capture Synchronization: ROS 2 + UnrealGT

This project captures synchronized simulator data for autonomy, perception, and navigation datasets.

The goal is simple:

```text
Every captured frame must use the same frame_index, timestamp, sensor outputs, ground-truth files, manifest row, and trajectory row.
```

This makes it possible to combine ROS 2 bags, UnrealGT images, rover poses, camera poses, and navigation ground truth without guessing or matching by approximate time.

---

## Main outputs

Each capture session can produce:

1. **ROS 2 bag data**
   - left camera RGB image
   - left camera camera info
   - left camera frame index
   - rover ground-truth pose

2. **UnrealGT image dataset**
   - RGB ground-truth image
   - depth image
   - segmentation image
   - bounding boxes
   - segmentation metadata

3. **Capture manifest**
   - session id
   - frame index
   - timestamp
   - rover pose in Unreal coordinates
   - left/reference camera pose in Unreal coordinates

4. **Navigation ground truth**
   - rover ROS-style trajectory CSV
   - timestamp
   - frame index
   - frame ids
   - position in meters
   - orientation quaternion

Note: `stamp_seconds` is Unreal world time in seconds, measured from the start of the Play session.

---

## Session folder structure

Each capture session is stored under:

```text
Saved/Datasets/<session_name>/
```

Example:

```text
Saved/Datasets/2026-05-06_16-46-16_session_1/
```

The folder layout is:

```text
Saved/Datasets/<session_name>/
├── manifest.csv
├── Images/
│   ├── RGB/
│   │   ├── 1.png
│   │   ├── 2.png
│   │   └── ...
│   ├── Depth/
│   │   ├── 1.png
│   │   ├── 2.png
│   │   └── ...
│   ├── Segmentation/
│   │   ├── 1.png
│   │   ├── 2.png
│   │   └── ...
│   ├── BoundingBoxes/
│   │   ├── 1.txt
│   │   ├── 2.txt
│   │   └── ...
│   └── SegmentationInfo/
│       └── segmentation_info.json
└── Navigation/
    └── rover_gt_trajectory_ros.csv
```

`CaptureManager` creates the session name, session directory, manifest path, images directory, and navigation directory. UnrealGT then writes into the session's `Images/` folder.

---

## Main synchronization idea

Every captured frame gets one shared frame number:

```text
frame_index = 1, 2, 3, ...
```

This frame index is created by `CaptureManager`.

The same `frame_index` is used by:

```text
ROS 2 messages
UnrealGT image files
manifest.csv
Navigation/rover_gt_trajectory_ros.csv
```

For example, for frame `25`:

```text
ROS 2:
  /left_camera/frame_index = 25
  /left_camera/rgb/image_raw
  /left_camera/camera_info
  /rover/gt/pose

UnrealGT:
  Images/RGB/25.png
  Images/Depth/25.png
  Images/Segmentation/25.png
  Images/BoundingBoxes/25.txt

Manifest:
  row with frame_index = 25

Navigation:
  row with frame_index = 25
```

So later, data can be matched directly by `frame_index`.

---

## Capture modes

The simulator configuration currently uses a pre-play capture mode dropdown.

Available modes:

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
  left ROS camera output
  manifest
  navigation trajectory
  optional ROS rover GT pose

Ground Truth:
  UnrealGT images
  manifest
  navigation trajectory
  optional ROS rover GT pose

Stereo ROS:
  reserved for future right camera support
  currently behaves like the left camera ROS path until right camera is implemented

Mono ROS + Ground Truth:
  left ROS camera output
  UnrealGT images
  manifest
  navigation trajectory
  optional ROS rover GT pose

Stereo ROS + Ground Truth:
  reserved for future right camera support
  currently behaves like Mono ROS + Ground Truth until right camera is implemented
```

Current user-facing config:

```text
PublishHz
Capture Mode dropdown
Enable ROS Rover GT Pose
```

`PublishHz` controls the capture/publish frequency. The current UI clamps it to the intended range.

---

## CaptureManager

`CaptureManager` is the source of truth.

It creates the synchronization data for each frame:

```text
SessionId
FrameIndex
StampSeconds
```

It also owns the current session paths:

```text
CurrentSessionName
CurrentSessionDirectory
CurrentImagesDirectory
ManifestFilePath
RoverGtTrajectoryFilePath
```

Every time a new frame is captured, `CaptureManager` increases the frame index:

```text
1, 2, 3, 4, ...
```

The important rule is:

```text
Other systems do not create their own frame index or timestamp.
They receive the frame information created by CaptureManager.
```

---

## Capture flow

For every captured frame, the flow is:

```text
RobotCamRig asks CaptureManager for one frame
CaptureManager returns FCaptureFrameInfo
CaptureManager writes the manifest row
CaptureManager writes the rover navigation trajectory row
RobotCamRig passes the same frame info to UnrealGT
RobotCamRig passes the same frame info to RGB capture
RobotCamRig asks RoverRobot to publish the synchronized ROS ground-truth pose
RobotCamRig asks the ROS publisher component to publish camera data
```

So one captured frame has one shared:

```text
SessionId
FrameIndex
StampSeconds
```

---

## Current code structure

The capture system is split into small responsibilities.

```text
RobotCamRig
  - high-level coordinator
  - controls capture timing
  - asks CaptureManager for the next frame
  - triggers UnrealGT capture
  - starts and polls RGB capture
  - asks RoverRobot to publish the synchronized ground-truth pose
  - asks the ROS publisher component to publish camera messages

CaptureManager
  - source of truth for SessionId, FrameIndex and StampSeconds
  - creates the dataset session folder
  - writes manifest.csv
  - writes Navigation/rover_gt_trajectory_ros.csv
  - records rover and left/reference camera pose for each frame

RgbCameraCaptureComponent
  - owns the RGB render target
  - owns the GPU readback logic
  - starts async RGB readback
  - stores the pending FCaptureFrameInfo
  - returns pixels together with the same FCaptureFrameInfo

RoverRobot
  - subscribes to /cmd_vel
  - moves the rover
  - publishes /rover/gt/pose using the same FCaptureFrameInfo timestamp

CameraRosPublisherComponent
  - owns the ROS node
  - owns ROS publishers and subscriber
  - builds and reuses ROS messages
  - publishes RGB image, camera_info and frame_index
  - uses FrameInfo.StampSeconds for ROS timestamps
  - uses FrameInfo.FrameIndex for /left_camera/frame_index

GTCamera
  - bridge to UnrealGT / Blueprint ground-truth generation
  - receives the same FrameIndex, StampSeconds, SessionId and CaptureManager

UnrealGT file utilities
  - normally keep UnrealGT fallback behavior
  - when CaptureManager provides a session Images directory, UnrealGT writes there
```

This keeps `RobotCamRig` simple. It coordinates the capture, but it does not directly own every ROS, UnrealGT, file-output, or GPU-readback detail.

---

## ROS 2 output

ROS uses these main topics:

```text
Subscribed:
  /cmd_vel
  /control

Published:
  /left_camera/rgb/image_raw
  /left_camera/camera_info
  /left_camera/frame_index
  /rover/gt/pose
```

The camera image may also appear with automatically generated image transport topics, for example:

```text
/left_camera/rgb/image_raw/compressed
/left_camera/rgb/image_raw/compressedDepth
/left_camera/rgb/image_raw/theora
```

The ROS image, camera info and rover ground-truth pose use the same timestamp from `CaptureManager`.

For each frame:

```text
frame_index = k
image.header.stamp = stamp_seconds for k
camera_info.header.stamp = stamp_seconds for k
rover_gt_pose.header.stamp = stamp_seconds for k
```

The `frame_index` topic is used to match ROS messages with UnrealGT files, the manifest and trajectory files.

The rover pose is published as:

```text
topic: /rover/gt/pose
type: geometry_msgs/msg/PoseStamped
frame_id: map
```

---

## UnrealGT output

UnrealGT saves files using the same `FrameIndex`.

Example:

```text
Images/RGB/1.png
Images/Depth/1.png
Images/Segmentation/1.png
Images/BoundingBoxes/1.txt

Images/RGB/2.png
Images/Depth/2.png
Images/Segmentation/2.png
Images/BoundingBoxes/2.txt
```

This means the UnrealGT files are aligned with ROS by filename.

The UnrealGT files do not need to store the timestamp inside each PNG or TXT file. Their timestamp is defined by the manifest row with the same `frame_index`.

For example:

```text
Images/RGB/25.png
Images/Depth/25.png
Images/Segmentation/25.png
Images/BoundingBoxes/25.txt
```

all correspond to:

```text
manifest row with frame_index = 25
Navigation row with frame_index = 25
ROS /left_camera/frame_index = 25
ROS image timestamp = manifest stamp_seconds for frame 25
ROS camera_info timestamp = manifest stamp_seconds for frame 25
ROS /rover/gt/pose timestamp = manifest stamp_seconds for frame 25
```

---

## Capture manifest

The manifest is created inside the dataset session folder:

```text
Saved/Datasets/<session_name>/manifest.csv
```

Example:

```text
Saved/Datasets/2026-05-06_16-46-16_session_1/manifest.csv
```

It contains synchronization information for every captured frame.

Basic columns:

```csv
session_id,frame_index,stamp_seconds,rover_valid,rover_x_ue_cm,rover_y_ue_cm,rover_z_ue_cm,rover_roll_ue_deg,rover_pitch_ue_deg,rover_yaw_ue_deg,left_camera_valid,left_camera_x_ue_cm,left_camera_y_ue_cm,left_camera_z_ue_cm,left_camera_roll_ue_deg,left_camera_pitch_ue_deg,left_camera_yaw_ue_deg
```

The manifest stores rover and left/reference camera pose in Unreal coordinates:

```text
position: Unreal centimeters
rotation: Unreal degrees
```

This is useful for debugging against the Unreal viewport and for keeping the original simulator transform values.

---

## Navigation trajectory output

A ROS-style rover ground-truth trajectory CSV is created here:

```text
Saved/Datasets/<session_name>/Navigation/rover_gt_trajectory_ros.csv
```

Columns:

```csv
timestamp_sec,frame_index,frame_id,child_frame_id,x_m,y_m,z_m,qx,qy,qz,qw
```

Example:

```csv
7.609157042,1,map,rover_base,-228.700000000,230.200000000,0.000000000,0.000000000,0.000000000,-0.000000000,1.000000000
```

This file is intended for robotics/navigation evaluation. It stores the rover ground-truth pose in ROS-style values:

```text
position: meters
orientation: quaternion
frame_id: map
child_frame_id: rover_base
```

This file should have the same number of rows as `manifest.csv` has captured frames.

---

## Rover pose system

The rover has a `UCapturePoseSourceComponent`.

This component does not store its own position. It reads the world transform of the actor that owns it.

For `RoverRobot`, it reads:

```text
RoverRobot actor location
RoverRobot actor rotation
```

`CaptureManager` finds the pose source named:

```text
rover_base
```

On every captured frame, `CaptureManager` writes the rover actor pose into:

```text
manifest.csv
Navigation/rover_gt_trajectory_ros.csv
```

The rover also publishes a synchronized ROS pose:

```text
/rover/gt/pose
```

This pose uses the same timestamp as the camera image, camera info, frame index, manifest row and trajectory row.

---

## Coordinate note

Unreal and ROS use different coordinate conventions.

The manifest stores rover and camera poses in Unreal coordinates:

```text
position: Unreal centimeters
rotation: Unreal degrees
```

The ROS topic `/rover/gt/pose` and the trajectory file `Navigation/rover_gt_trajectory_ros.csv` store rover pose in ROS-style values:

```text
position: meters
orientation: quaternion
```

The current rover position conversion is:

```text
ros_x = unreal_x / 100.0
ros_y = -unreal_y / 100.0
ros_z = unreal_z / 100.0
```

The current rover yaw conversion follows the same convention used by the ROS ground-truth pose publisher:

```text
ros_yaw = -unreal_yaw
```

So the manifest, ROS pose topic and navigation trajectory represent the same physical rover pose, but not always with the same numeric coordinate values.

---

## Recommended capture shutdown flow

UnrealGT writes image files asynchronously. To avoid losing the final image file when stopping the editor quickly, use this flow:

```text
Start capture
Stop capture with /control = 0
Wait 3-5 seconds
Then stop Play / PIE
```

This gives pending image saves time to finish.

A future improvement is to add an explicit flush/wait mechanism for pending UnrealGT save tasks.

---

## Important note about timestamp difference

A difference of `0.000000001` seconds is one nanosecond.

For example:

```text
manifest: 9.272525717
ROS:      9.272525716
```

This happens because the timestamp is converted between C++ `double`, CSV text and ROS `sec/nanosec`.

It is safe for evaluation. It is far smaller than one frame interval.

For example, at 10 Hz, one frame interval is about:

```text
0.1 seconds
```

So a `0.000000001` second difference is insignificant.

---

## Important note about segmentation metadata

`segmentation_info.json` is metadata, not one file per frame.

It is stored in:

```text
Images/SegmentationInfo/segmentation_info.json
```

The per-frame segmentation images are stored in:

```text
Images/Segmentation/
```

---

## Why this works

For each frame, all systems use the same information:

```text
CaptureManager creates:
  SessionId
  FrameIndex
  StampSeconds

ROS 2 uses:
  FrameIndex
  StampSeconds

UnrealGT uses:
  FrameIndex

Manifest stores:
  SessionId
  FrameIndex
  StampSeconds

Navigation trajectory stores:
  FrameIndex
  StampSeconds
```

So frame `k` always refers to the same capture moment.

The synchronization rule is:

```text
UnrealGT file name k
  <-> manifest row k
  <-> Navigation trajectory row k
  <-> ROS frame_index k
  <-> ROS rover gt pose timestamp for k
```

---

## Verified full-pipeline result

A full pipeline test produced:

```text
ROS rover gt pose:       192
ROS compressed RGB:      192
ROS frame_index:         192
ROS camera_info:         192

Manifest frames:         192
UnrealGT RGB:            192
UnrealGT Depth:          192
UnrealGT Segmentation:   192
UnrealGT BoundingBoxes:  192
```

The timestamps were also checked:

```text
Max |RGB - CameraInfo|: 0.000000000000 sec
Max |RGB - RoverPose|:  0.000000000000 sec
Max |RGB - Manifest|:   0.000000001000 sec
```

The rover pose was checked against the manifest conversion:

```text
Max rover position error: 0.000000006641 m
```

So the ROS image, ROS camera_info, ROS frame_index, ROS rover ground-truth pose, UnrealGT files, manifest rows and navigation trajectory rows are synchronized.

---

## Summary

The synchronization works like this:

```text
FrameIndex 1
  ROS image
  ROS camera info
  ROS frame index
  ROS rover ground-truth pose
  Images/RGB/1.png
  Images/Depth/1.png
  Images/Segmentation/1.png
  Images/BoundingBoxes/1.txt
  manifest row for frame 1
  Navigation trajectory row for frame 1

FrameIndex 2
  ROS image
  ROS camera info
  ROS frame index
  ROS rover ground-truth pose
  Images/RGB/2.png
  Images/Depth/2.png
  Images/Segmentation/2.png
  Images/BoundingBoxes/2.txt
  manifest row for frame 2
  Navigation trajectory row for frame 2
```

So later we can safely combine ROS 2, UnrealGT, navigation ground truth and offline dataset files by using `frame_index`.

---

## Suggested future improvements

These are useful next steps, but they are not required for the current working pipeline:

```text
- Add right ROS camera support for stereo modes
- Add right camera pose columns to the manifest
- Add ROS-style left camera trajectory file
- Add TUM trajectory export for SLAM evaluation
- Add /gt/odom as nav_msgs/Odometry
- Add /tf transform map -> rover_base
- Add /gt/path as nav_msgs/Path
- Add explicit flush/wait for pending UnrealGT save tasks on capture stop
- Add object metadata and instance-mask outputs
```

NOTE IN THE END: (DONT REMOVE IT)
Turn your current manifest pose rows into proper ROS navigation ground truth:
manifest + /gt/odom + /tf + /gt/path + trajectory files.