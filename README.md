# Capture Synchronization: ROS + UnrealGT

This project captures synchronized simulator data for autonomy and perception datasets.

The main outputs are:

1. **ROS bag**
   - RGB image
   - camera info
   - frame index
   - rover ground-truth pose

2. **UnrealGT dataset**
   - RGB ground truth image
   - Depth image
   - Segmentation image
   - Bounding boxes

3. **Capture manifest**
   - session id
   - frame index
   - timestamp
   - rover pose
   - camera pose

The goal is simple: every ROS frame must match the correct UnrealGT files, the correct rover ground-truth pose, and the correct manifest row.

---

## Main idea

Every captured frame gets one shared frame number:

```text
frame_index = 1, 2, 3, ...
```

This frame index is created by `CaptureManager`.

The same frame index is then used by ROS, UnrealGT and the manifest.


For example, for frame `25`:

```text
ROS:
  /left_camera/frame_index = 25
  /left_camera/rgb/image_raw
  /left_camera/camera_info
  /rover/gt/pose

UnrealGT:
  RGB/25.png
  Depth/25.png
  Segmentation/25.png
  BoundingBoxes/25.txt

Manifest:
  row with frame_index = 25
```

So later, if we want to combine ROS and UnrealGT, we match by frame number.

---

## CaptureManager

`CaptureManager` is the source of truth.

It creates the synchronization data for each frame:

```text
SessionId
FrameIndex
StampSeconds
```

Every time a new frame is captured, `CaptureManager` increases the frame index:

```text
1, 2, 3, 4, ...
```

This same `FrameIndex` is used by:

```text
ROS
UnrealGT
Capture manifest
```

The important rule is that other systems do not create their own frame index or timestamp.
They receive the frame information that was created by `CaptureManager`.

---

## Capture flow

For every captured frame, the flow is:

```text
RobotCamRig asks CaptureManager for one frame
CaptureManager returns FCaptureFrameInfo
RobotCamRig passes the same frame info to UnrealGT
RobotCamRig passes the same frame info to RGB capture
RobotCamRig passes the same frame info to rover ground-truth pose publishing
RobotCamRig passes the same frame info to ROS publishing
Manifest stores the same frame info
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
  - asks the ROS publisher component to publish

CaptureManager
  - source of truth for SessionId, FrameIndex and StampSeconds
  - writes the capture manifest
  - records rover and camera pose for each frame

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
  - receives the same FrameIndex, StampSeconds and SessionId
```

This keeps `RobotCamRig` simple. It coordinates the capture, but it does not directly handle all ROS and GPU readback details.

---

## ROS output

ROS uses these main topics:

```text
Subscribed:
  /cmd_vel
  /left_camera/control

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

So for each frame:

```text
frame_index = k
image.header.stamp = stamp_seconds for k
camera_info.header.stamp = stamp_seconds for k
rover_gt_pose.header.stamp = stamp_seconds for k
```

The `frame_index` topic is used to match ROS messages with UnrealGT files and the manifest.

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
RGB/1.png
Depth/1.png
Segmentation/1.png
BoundingBoxes/1.txt

RGB/2.png
Depth/2.png
Segmentation/2.png
BoundingBoxes/2.txt
```

This means the UnrealGT files are aligned with ROS by filename.

The UnrealGT files do not need to store the timestamp inside each PNG or TXT file.
Their timestamp is defined by the manifest row with the same `frame_index`.

For example:

```text
RGB/25.png
Depth/25.png
Segmentation/25.png
BoundingBoxes/25.txt
```

all correspond to:

```text
manifest row with frame_index = 25
ROS /left_camera/frame_index = 25
ROS image timestamp = manifest stamp_seconds for frame 25
ROS camera_info timestamp = manifest stamp_seconds for frame 25
ROS /rover/gt/pose timestamp = manifest stamp_seconds for frame 25
```

---

## Capture manifest

A CSV file is created in:

```text
Saved/CaptureManifests/
```

Example file:

```text
2026-05-04_18-24-45_session_1.csv
```

It contains the synchronization information for each frame.

Basic columns:

```csv
session_id,frame_index,stamp_seconds
1,1,8.969255818
1,2,9.196653258
1,3,9.272525717
```

This manifest tells us:

```text
Frame 1 happened at timestamp 8.969255818
Frame 2 happened at timestamp 9.196653258
Frame 3 happened at timestamp 9.272525717
```

The manifest can also contain rover and camera pose data for each frame.

Later, a Python script can use this manifest to combine UnrealGT files with the ROS bag.

---

## Rover pose system

The rover has a `UCapturePoseSourceComponent`.

This component does not store its own position.
It reads the world transform of the actor that owns it.

For `RoverRobot`, it reads:

```text
RoverRobot actor location
RoverRobot actor rotation
```

`CaptureManager` finds the pose source named `rover_base`.
On every captured frame, `CaptureManager` writes the rover actor pose into the manifest CSV.

The CSV gives one rover pose per frame:

```text
frame_index
timestamp
rover position in Unreal centimeters
rover rotation in Unreal degrees
```

The rover also publishes a synchronized ROS pose:

```text
/rover/gt/pose
```

This pose uses the same timestamp as the camera image and camera info.

The rover trajectory is the sequence of these rows ordered by `frame_index`.

---

## Coordinate note

Unreal and ROS use different coordinate conventions.

The manifest currently stores the rover and camera pose in Unreal coordinates:

```text
position: Unreal centimeters
rotation: Unreal degrees
```

The `/rover/gt/pose` ROS topic stores the rover pose in ROS-style values:

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

So the manifest and ROS pose represent the same physical rover pose, but not with the same numeric values.

---

## Why this works

For each frame, all systems use the same information:

```text
CaptureManager creates:
  SessionId
  FrameIndex
  StampSeconds

ROS uses:
  FrameIndex
  StampSeconds

UnrealGT uses:
  FrameIndex

Manifest stores:
  SessionId
  FrameIndex
  StampSeconds
```

So frame `k` always refers to the same capture moment.

The synchronization rule is:

```text
UnrealGT file name k  <->  manifest row k  <->  ROS frame_index k  <->  ROS rover gt pose timestamp for k
```

---

## Verified result

We tested one capture and got:

```text
ROS camera_info:        136
ROS frame_index:        136
ROS RGB compressed:     136
ROS rover gt pose:      136

UnrealGT RGB:           136
UnrealGT Depth:         136
UnrealGT Segmentation:  136
UnrealGT BoundingBoxes: 136

Manifest lines:         137
Manifest frames:        136
```

The frame counts matched across ROS, UnrealGT and the manifest.

We also checked the ROS timestamps:

```text
Max |RGB compressed - CameraInfo|: 0 ns
Max |RGB compressed - RoverPose|:  0 ns
Max |CameraInfo - RoverPose|:      0 ns
```

So the ROS RGB image, ROS camera_info and ROS rover ground-truth pose messages use the same timestamp for each frame.

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

## Important note about GTSegmentationGenerator

`GTSegmentationGenerator` may contain only one file:

```text
segmentation_info.json
```

That is normal. It is metadata, not one file per frame.

The per-frame segmentation images are inside:

```text
Segmentation/
```

---

## Summary

The synchronization works like this:

```text
FrameIndex 1
  ROS image
  ROS camera info
  ROS frame index
  ROS rover ground-truth pose
  UnrealGT RGB/1.png
  UnrealGT Depth/1.png
  UnrealGT Segmentation/1.png
  UnrealGT BoundingBoxes/1.txt
  Manifest row for frame 1

FrameIndex 2
  ROS image
  ROS camera info
  ROS frame index
  ROS rover ground-truth pose
  UnrealGT RGB/2.png
  UnrealGT Depth/2.png
  UnrealGT Segmentation/2.png
  UnrealGT BoundingBoxes/2.txt
  Manifest row for frame 2
```

So later we can safely combine ROS and UnrealGT data by using `frame_index`.


NOTE IN THE END: (DONT REMOVE IT)
Turn your current manifest pose rows into proper ROS navigation ground truth:
manifest + /gt/odom + /tf + /gt/path + trajectory files.