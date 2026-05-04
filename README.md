# Capture Synchronization: ROS + UnrealGT

This project captures simulator data in two places:

1. **ROS bag**
   - RGB image
   - camera info
   - frame index

2. **UnrealGT**
   - RGB ground truth image
   - Depth image
   - Segmentation image
   - Bounding boxes

The goal is simple: every ROS frame must match the correct UnrealGT files.

---

## Main idea

Every capture frame gets one shared frame number:

```text
frame_index = 1, 2, 3, ...
```

This frame index is created by `CaptureManager`.

The same frame index is then used by both ROS and UnrealGT.

For example, for frame `25`:

```text
ROS:
  /sim_camera/frame_index = 25
  /sim_camera/rgb/image_raw/compressed
  /sim_camera/camera_info

UnrealGT:
  RGB/25.png
  Depth/25.png
  Segmentation/25.png
  BoundingBoxes/25.txt
```

So later, if we want to combine ROS and UnrealGT, we match by frame number.

---

## CaptureManager

`CaptureManager` is the source of truth.

It creates:

```text
SessionId
FrameIndex
StampSeconds
```

Every time a new frame is captured, `CaptureManager::NextFrame()` increases the frame index:

```text
1, 2, 3, 4, ...
```

This same `FrameIndex` is used by:

```text
ROS
UnrealGT
Capture manifest
```

---

## ROS output

ROS publishes:

```text
/sim_camera/frame_index
/sim_camera/rgb/image_raw/compressed
/sim_camera/camera_info
```

The ROS image and camera info use the same timestamp from `CaptureManager`.

So for each frame:

```text
frame_index = k
image.header.stamp = timestamp for k
camera_info.header.stamp = timestamp for k
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

---

## Capture manifest

A CSV file is created in:

```text
Saved/CaptureManifests/
```

Example file:

```text
2026-04-28_10-59-17_session_1.csv
```

It contains:

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

Later, a Python script can use this manifest to add the UnrealGT data into a ROS bag.

---

## Why this works

For each frame, all systems use the same information:

```text
CaptureManager creates:
  FrameIndex
  Timestamp

ROS uses:
  FrameIndex
  Timestamp

UnrealGT uses:
  FrameIndex

Manifest stores:
  FrameIndex
  Timestamp
```

So frame `k` always refers to the same capture moment.

---

## Verified result

We tested one capture and got:

```text
ROS frame_index:        1–103
UnrealGT RGB:           1–103
UnrealGT Depth:         1–103
UnrealGT Segmentation:  1–103
UnrealGT BoundingBoxes: 1–103
Manifest:               1–103
```

The manifest also matched the ROS timestamps.

The maximum timestamp difference was around:

```text
0.000000001 seconds
```

This is only floating-point / decimal precision. It is not a real timing mismatch.

---

## Important note about timestamp difference

A difference of `0.000000001` seconds is one nanosecond.

For example:

```text
manifest: 9.272525717
ROS:      9.272525716
```

This happens because the timestamp is converted between C++ `double`, CSV text, and ROS `sec/nanosec`.

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
  UnrealGT RGB/1.png
  UnrealGT Depth/1.png
  UnrealGT Segmentation/1.png
  UnrealGT BoundingBoxes/1.txt
  Manifest row for frame 1

FrameIndex 2
  ROS image
  ROS camera info
  UnrealGT RGB/2.png
  UnrealGT Depth/2.png
  UnrealGT Segmentation/2.png
  UnrealGT BoundingBoxes/2.txt
  Manifest row for frame 2
```

So later we can safely combine ROS and UnrealGT data by using `frame_index`.

## Rover pose system

The rover has a UCapturePoseSourceComponent.
This component does not store its own position.
It reads the world transform of the actor that owns it.

For RoverRobot, it reads:
- RoverRobot actor location
- RoverRobot actor rotation

CaptureManager finds the pose source named "rover_base".
On every captured frame, CaptureManager writes the rover actor pose into the manifest CSV.

The CSV gives one rover pose per frame:
- frame_index
- timestamp
- rover position in Unreal centimeters
- rover rotation in Unreal degrees

The rover trajectory is the sequence of these rows ordered by frame_index.