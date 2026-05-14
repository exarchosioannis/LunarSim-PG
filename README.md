# Capture Synchronization: ROS 2 + UnrealGT

This project captures synchronized simulator data from Unreal Engine for robotics, perception, stereo vision, rover navigation, mapping, and machine-learning dataset generation.

The main rule is:

```text
One captured frame = one shared frame_index + one shared timestamp.
```

For every capture step, `CaptureManager` creates one shared frame description:

```text
SessionId
FrameIndex
StampSeconds
```

The same `FCaptureFrameInfo` is passed to ROS 2 camera messages, rover ground-truth messages, TF, path, UnrealGT file generation, `manifest.csv`, and navigation trajectory files. This makes the dataset matchable by `frame_index` without approximate timestamp matching.

`stamp_seconds` is Unreal world time in seconds, measured from the start of the Play session.

---

## Current project goal

The project is a ground-truth-rich autonomy dataset generator for simulated rover experiments.

It is designed to generate synchronized outputs for:

```text
navigation
perception
machine learning datasets
stereo vision
SLAM / odometry evaluation
semantic ground truth evaluation
mapping / occupancy-grid evaluation
```

The current system supports:

```text
ROS 2 RGB camera streams
stereo ROS camera streams
camera_info calibration
frame_index topics
UnrealGT RGB/depth/segmentation/bounding-box files
rover ground-truth pose
rover ground-truth odometry-style message
synchronized dynamic TF
static camera TF
rover path
ground-truth occupancy map
manifest.csv
ROS-style trajectory CSV files
```

---

## Core synchronization rule

For frame `k`, all synchronized outputs should use:

```text
FrameIndex = k
StampSeconds = Tk
SessionId = current capture session
```

Example:

```text
ROS 2:
  /left_camera/frame_index        = k
  /right_camera/frame_index       = k
  /left_camera/rgb/image_raw      timestamp = Tk
  /right_camera/rgb/image_raw     timestamp = Tk
  /left_camera/camera_info        timestamp = Tk
  /right_camera/camera_info       timestamp = Tk
  /rover/gt/pose                  timestamp = Tk
  /gt/odom                        timestamp = Tk
  /tf                             timestamp = Tk
  /gt/path                        timestamp = Tk

UnrealGT:
  Images/RGB/k.png
  Images/Depth/k.png
  Images/Segmentation/k.png
  Images/BoundingBoxes/k.txt

CSV:
  manifest.csv row with frame_index = k
  Navigation/rover_gt_trajectory_ros.csv row with frame_index = k
  Navigation/left_camera_gt_trajectory_ros.csv row with frame_index = k
  Navigation/right_camera_gt_trajectory_ros.csv row with frame_index = k
```

The GPU readback and UnrealGT file saving may finish later than the frame trigger, but the stored frame index and timestamp identify the correct capture moment.

Important:

```text
/rover/gt/pose, /gt/odom, /tf, /gt/path, camera_info, and frame_index are triggered from the synchronized FCaptureFrameInfo.
/left_camera/rgb/image_raw and /right_camera/rgb/image_raw may be published slightly later because RGB uses async GPU readback.
The RGB messages still carry the original synchronized timestamp and frame_index.
```

---

## Current ROS TF tree

The current validated TF tree is:

```text
map
└── base_link
    ├── left_camera_link
    │   └── left_camera_optical_frame
    └── right_camera_link
        └── right_camera_optical_frame
```

Meaning:

```text
map
  global simulator/world frame converted to ROS coordinates

base_link
  rover body/root frame
  currently corresponds to the BP_RoverVehicle actor transform

left_camera_link / right_camera_link
  physical camera mounting frames on the rover

left_camera_optical_frame / right_camera_optical_frame
  ROS optical camera frames used by image and camera_info messages
```

Camera optical frames use the ROS camera convention:

```text
z forward
x right
y down
```

The dynamic rover transform is:

```text
map -> base_link
```

The static camera transforms are:

```text
base_link -> left_camera_link
base_link -> right_camera_link
left_camera_link -> left_camera_optical_frame
right_camera_link -> right_camera_optical_frame
```

Current note:

```text
The simulator does not currently add an odom frame.
The current tree is intentionally kept simple:
map -> base_link -> cameras.
```

A future Nav2/SLAM mode may introduce:

```text
map -> odom -> base_link
```

but that is not part of the current validated setup.

---

## ROS frame IDs

Current frame names:

```text
map
base_link
left_camera_link
right_camera_link
left_camera_optical_frame
right_camera_optical_frame
```

Important:

```text
Image and camera_info messages use optical frame IDs.
Physical camera TF uses camera_link frame IDs.
```

So:

```text
/left_camera/rgb/image_raw.header.frame_id       = left_camera_optical_frame
/left_camera/camera_info.header.frame_id         = left_camera_optical_frame

/right_camera/rgb/image_raw.header.frame_id      = right_camera_optical_frame
/right_camera/camera_info.header.frame_id        = right_camera_optical_frame
```

---

## Outputs

A capture session can produce:

```text
ROS 2 camera data:
  /left_camera/rgb/image_raw
  /left_camera/camera_info
  /left_camera/frame_index
  /right_camera/rgb/image_raw
  /right_camera/camera_info
  /right_camera/frame_index

ROS 2 rover ground truth:
  /rover/gt/pose
  /gt/odom
  /tf
  /gt/path

ROS 2 static TF:
  /tf_static

ROS 2 map:
  /gt/map/occupancy

ROS 2 control:
  /control
  /cmd_vel

UnrealGT data:
  RGB images
  depth images
  segmentation images
  bounding boxes
  segmentation metadata

CSV files:
  manifest.csv
  Navigation/rover_gt_trajectory_ros.csv
  Navigation/left_camera_gt_trajectory_ros.csv
  Navigation/right_camera_gt_trajectory_ros.csv
```

---

## Session folder structure

Each capture session is stored under:

```text
Saved/Datasets/<session_name>/
```

A normal session has this layout:

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
    ├── rover_gt_trajectory_ros.csv
    ├── left_camera_gt_trajectory_ros.csv
    └── right_camera_gt_trajectory_ros.csv
```

`CaptureManager` creates the session folder, `manifest.csv`, `Images/`, and `Navigation/`. UnrealGT writes its output inside the session `Images/` directory.

---

## Capture modes

The simulator supports these capture modes:

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
  left ROS camera
  manifest
  rover trajectory CSV if rover pose source exists
  optional ROS rover GT topics

Ground Truth:
  UnrealGT output
  manifest
  rover trajectory CSV if rover pose source exists
  optional ROS rover GT topics

Stereo ROS:
  left ROS camera
  right ROS camera
  stereo camera_info
  manifest
  rover trajectory CSV if rover pose source exists
  left/right camera trajectory CSVs
  optional ROS rover GT topics

Mono ROS + Ground Truth:
  left ROS camera
  UnrealGT output aligned with the left camera
  manifest
  rover trajectory CSV if rover pose source exists
  optional ROS rover GT topics

Stereo ROS + Ground Truth:
  left ROS camera
  right ROS camera
  UnrealGT output aligned with the left camera
  stereo camera_info
  manifest
  rover trajectory CSV if rover pose source exists
  left/right camera trajectory CSVs
  optional ROS rover GT topics
```

---

## Simulator Config editor window

The project includes an editor module that adds a **Simulator Config** window under the Unreal Editor Window menu.

The config window controls the placed `RobotCamRig` actor.

User-facing settings:

```text
Publish Hz
Capture Mode
/rover/gt/pose checkbox
Stereo Baseline Cm
```

Current default values from the editor module:

```text
PublishHz = 6
CaptureMode = Mono ROS + Ground Truth
Enable ROS Rover GT Pose = true
StereoBaselineCm = 20.0
```

The config window applies settings to the first `RobotCamRig` found in the editor world.

Important:

```text
The config window changes capture configuration only.
The actual capture starts and stops with the /control ROS topic.
```

---

## Start/stop capture

The capture pipeline is controlled through ROS 2:

```bash
ros2 topic pub /control std_msgs/msg/Int32 "{data: 1}" --once
```

starts capture.

```bash
ros2 topic pub /control std_msgs/msg/Int32 "{data: 0}" --once
```

stops capture.

The left camera ROS publisher owns the `/control` subscription and forwards start/stop commands to `RobotCamRig`.

Useful helper script:

```bash
./startcapture
```

Expected content:

```bash
#!/bin/bash
ros2 topic pub /control std_msgs/msg/Int32 "{data: 1}" --once && echo "[OK] Capture started"
```

---

## Main architecture

```text
RobotCamRig
  high-level coordinator
  owns the left/reference camera side
  owns the right/stereo camera side
  owns the internal UnrealGT child camera
  owns CaptureManager
  controls capture timing
  asks CaptureManager for the next synchronized frame
  passes the same FCaptureFrameInfo to ROS cameras, UnrealGT, and rover GT publishing
  attaches to the rover's RoverSensorMount at BeginPlay
  publishes static camera TFs using TempoROS PublishStaticTransform

CaptureManager
  source of truth for SessionId, FrameIndex, StampSeconds
  creates session directories
  writes manifest.csv
  writes rover/camera trajectory CSV files
  collects rover, left camera, and right camera poses
  uses UnrealToRosConversion for trajectory CSV pose conversion

RgbCameraCaptureComponent
  owns RGB render target and GPU readback
  starts async capture with a pending FCaptureFrameInfo
  returns pixels together with the same FCaptureFrameInfo

CameraRosPublisherComponent
  owns ROS image, camera_info, and frame_index publishers
  left instance also owns /control subscription
  uses FrameInfo.StampSeconds for ROS timestamps
  uses FrameInfo.FrameIndex for frame matching
  publishes image and camera_info with optical frame IDs

GTCamera
  C++ bridge to the UnrealGT Blueprint camera
  exposes CaptureGroundTruthNow as a BlueprintImplementableEvent
  receives FrameIndex, StampSeconds, SessionId, and CaptureManager

RoverCmdVelVehicleControllerComponent
  reusable movement component for Chaos vehicle rovers
  subscribes to /cmd_vel
  converts geometry_msgs/Twist into throttle, steering, and brake/reverse input

CapturePoseSourceComponent
  generic pose-source component
  exposes GetWorldCapturePose()
  used by CaptureManager to write synchronized trajectory CSV rows

RoverGroundTruthPublisherComponent
  reusable rover GT ROS publisher
  attachable to the current rover actor or future final rover
  derives from CapturePoseSourceComponent
  publishes /rover/gt/pose, /gt/odom, /tf, and /gt/path
  publishes only when RobotCamRig gives it a synchronized FCaptureFrameInfo
  publishes synchronized dynamic /tf manually with VOLATILE QoS

OccupancyMapPublisherComponent
  independent map publisher component
  publishes /gt/map/occupancy as nav_msgs/msg/OccupancyGrid
  uses frame_id = map
  can be placed on a separate BP_GroundTruthMapPublisher actor
  does not touch or depend on the capture synchronization pipeline

UnrealToRosConversion
  shared conversion helper
  converts Unreal positions, rotations, and transforms to ROS coordinate convention
  used by CaptureManager and rover ground-truth outputs

BP_RoverVehicle
  current rover prototype
  Chaos WheeledVehiclePawn-based rover
  should have RoverCmdVelVehicleControllerComponent for /cmd_vel movement
  should have RoverGroundTruthPublisherComponent for synchronized GT outputs
  should have a RoverSensorMount SceneComponent for camera rig mounting
```

`RobotCamRig` is the main camera/capture actor. The UnrealGT Blueprint camera is spawned inside `RobotCamRig` as a child actor, so it does not need to be placed separately in the level.

The occupancy map publisher is intentionally separate from `RobotCamRig`.

---

## Rover and camera mounting architecture

The current rover architecture is component-based. The rover actor is not hard-coded into the capture pipeline. Any current or future rover Blueprint can be used if it has the reusable rover components attached.

Current rover setup:

```text
BP_RoverVehicle
├── Chaos Vehicle Movement Component
├── RoverCmdVelVehicleControllerComponent
│   └── subscribes to /cmd_vel
├── RoverGroundTruthPublisherComponent
│   ├── acts as CapturePoseSourceComponent
│   ├── publishes /rover/gt/pose
│   ├── publishes /gt/odom
│   ├── publishes /tf
│   └── publishes /gt/path
└── RoverSensorMount
    └── RobotCamRig attaches here at BeginPlay
```

`RoverSensorMount` is a `SceneComponent` placed on the rover at the physical location of the left/reference camera.

Current convention:

```text
RobotCamRig origin = left/reference camera physical location
right camera = local +Y offset by StereoBaselineCm in Unreal
```

Because Unreal `+Y` becomes ROS `-Y`, a `StereoBaselineCm = 20` produces:

```text
right_camera_link.y ≈ left_camera_link.y - 0.20
```

To move the same logic to the final rover:

```text
1. Add RoverCmdVelVehicleControllerComponent to the final rover Blueprint.
2. Add RoverGroundTruthPublisherComponent to the final rover Blueprint.
3. Add a SceneComponent named RoverSensorMount to the final rover Blueprint.
4. Place RoverSensorMount at the left/reference camera position.
5. Assign the final rover actor in RobotCamRig as the Rover Actor.
6. Keep the capture, ROS, TF, and dataset pipeline unchanged.
```

---

## /cmd_vel rover movement

`RoverCmdVelVehicleControllerComponent` subscribes to:

```text
/cmd_vel
```

Message type:

```text
geometry_msgs/msg/Twist
```

Mapping:

```text
linear.x  -> throttle / brake-reverse
angular.z -> steering
```

The component finds the owner actor's `UChaosVehicleMovementComponent` and applies:

```text
SetThrottleInput
SetSteeringInput
SetBrakeInput
```

Control parameters exposed in the component:

```text
CmdVelTopic
MaxCmdLinearMps
MaxCmdAngularRadps
CmdTimeoutSec
ThrottleInterpSpeed
SteeringInterpSpeed
BrakeInterpSpeed
bInvertSteering
bInvertThrottle
```

Current behavior:

```text
linear.x > 0:
  throttle = linear.x / MaxCmdLinearMps
  brake = 0

linear.x < 0:
  throttle = 0
  brake/reverse = abs(linear.x / MaxCmdLinearMps)

angular.z:
  steering = angular.z / MaxCmdAngularRadps
```

If no `/cmd_vel` message is received for `CmdTimeoutSec`, the component zeroes the command.

Manual keyboard input should not directly write to the vehicle movement component at the same time as ROS control, because direct Blueprint input nodes can overwrite the ROS component every frame. If both keyboard and ROS control are needed, they should eventually be routed through the same controller component.

---

## Joystick teleop

The current ROS 2 joystick launch flow uses:

```text
joy_node
teleop_twist_joy
```

Launch file:

```text
simulator_ros/launch/teleop.launch.py
```

Config file:

```text
simulator_ros/config/f710.yaml
```

The launch file starts:

```text
joy_node
teleop_twist_joy
```

The F710 config publishes `/cmd_vel` while the enable button is held.

Useful checks:

```bash
ros2 launch simulator_ros teleop.launch.py
ros2 topic echo /cmd_vel
ros2 topic info /cmd_vel -v
```

If a YAML value does not appear to change in the launch output, rebuild/source the ROS workspace:

```bash
cd /ws/ros2_ws
colcon build --packages-select simulator_ros --symlink-install
source install/setup.bash
ros2 launch simulator_ros teleop.launch.py
```

---

## Rover ground truth ROS topics

`RoverGroundTruthPublisherComponent` publishes synchronized rover ground truth.

Topics:

```text
/rover/gt/pose    geometry_msgs/msg/PoseStamped
/gt/odom          nav_msgs/msg/Odometry
/tf               tf2_msgs/msg/TFMessage
/gt/path          nav_msgs/msg/Path
```

Default frame IDs:

```text
FrameId = map
ChildFrameId = base_link
```

The component publishes only when `RobotCamRig` creates a synchronized frame. It should not publish freely every Tick, because the dataset needs the same timestamp/frame index as the cameras, UnrealGT, manifest, trajectory CSV files, and path.

`/rover/gt/pose`:

```text
frame_id = map
pose = rover/base_link pose in the simulator map frame
```

`/gt/odom`:

```text
header.frame_id = map
child_frame_id = base_link
pose = same synchronized ground-truth rover pose
twist = zero for now
```

`/tf`:

```text
map -> base_link
timestamp = synchronized FrameInfo.StampSeconds
QoS = VOLATILE
```

`/gt/path`:

```text
frame_id = map
poses = accumulated synchronized ground-truth rover poses
```

`/gt/odom` currently contains synchronized pose ground truth. The twist fields are intentionally zero for now. It is not wheel odometry estimation.

---

## Camera TF and camera topics

Static camera TFs are published by `RobotCamRig` at BeginPlay.

Static TF tree:

```text
base_link -> left_camera_link
base_link -> right_camera_link
left_camera_link -> left_camera_optical_frame
right_camera_link -> right_camera_optical_frame
```

The camera mounting transforms are static because the cameras are fixed relative to the rover.

The current validated example is:

```text
base_link -> left_camera_link:
  translation approximately [1.3, -0.1, 0.9]

base_link -> right_camera_link:
  translation approximately [1.3, -0.3, 0.9]
```

With a 20 cm stereo baseline:

```text
right_y - left_y = -0.2 m
```

The optical frame transform is:

```text
camera_link -> camera_optical_frame:
  translation = [0, 0, 0]
  RPY = [-90 deg, 0 deg, -90 deg]
```

---

## Stereo camera setup

The stereo setup uses the left camera as the reference camera:

```text
RobotCamRig origin
├── left/reference ROS camera at local (0, 0, 0)
├── UnrealGT camera aligned with the left/reference camera
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

The image and camera_info headers use optical frames:

```text
/left_camera/rgb/image_raw.header.frame_id  = left_camera_optical_frame
/left_camera/camera_info.header.frame_id    = left_camera_optical_frame

/right_camera/rgb/image_raw.header.frame_id = right_camera_optical_frame
/right_camera/camera_info.header.frame_id   = right_camera_optical_frame
```

The right camera `camera_info` includes the stereo baseline in the projection matrix:

```text
P_left[3]  = 0
P_right[3] = -fx * baseline_m
```

Example:

```text
image width = 1280
horizontal FOV = 90 degrees
StereoBaselineCm = 20

fx = width / (2 * tan(FOV / 2))
fx = 1280 / (2 * tan(45 degrees)) = 640 px
baseline_m = 20 / 100 = 0.2 m
P_right[3] = -640 * 0.2 = -128
```

This makes the stereo `camera_info` usable by ROS stereo tools.

---

## Ground-truth occupancy map

The simulator can publish a static ground-truth occupancy map:

```text
/gt/map/occupancy
```

Message type:

```text
nav_msgs/msg/OccupancyGrid
```

Frame:

```text
map
```

The map publisher is independent from the synchronized capture pipeline.

Recommended setup:

```text
BP_GroundTruthMapPublisher
└── OccupancyMapPublisherComponent
```

Place `BP_GroundTruthMapPublisher` at the center of the area to map.

Default example settings:

```text
Map size: 50m x 50m
Resolution: 0.1m
Grid: 500 x 500 cells
```

Occupancy values:

```text
0    = free
100  = occupied
-1   = unknown
```

Current simple behavior:

```text
Tagged obstacle hit  -> occupied
Other world hit      -> free
No hit               -> unknown
```

Obstacle actors/components can be tagged with:

```text
MapObstacle
```

Important RViz note:

```text
The RViz Grid display is only a visual helper centered on the map frame origin (0,0,0).
The occupancy map is centered around the BP_GroundTruthMapPublisher actor.
Therefore, the occupancy map does not have to be centered on the RViz helper grid.
```

To verify the rover is inside the map, compute:

```text
min_x = origin.x
max_x = origin.x + width * resolution

min_y = origin.y
max_y = origin.y + height * resolution
```

The rover `/rover/gt/pose` x/y should be inside those bounds.

---

## UnrealGT setup

Ground-truth image generation is done through an UnrealGT Blueprint camera.

`GTCamera` is the C++ bridge. It exposes:

```text
CaptureGroundTruthNow(FrameIndex, StampSeconds, SessionId, CaptureManager)
```

as a BlueprintImplementableEvent.

The placed `RobotCamRig` actor has:

```text
GroundTruthCameraClass
```

This should be assigned to the UnrealGT Blueprint camera, for example:

```text
BP_UnrealGT_Camera
```

The Blueprint camera is spawned as a child actor inside `RobotCamRig` and is aligned with the left/reference camera.

If Ground Truth mode is enabled but no images are written, check:

```text
GroundTruthCameraClass is assigned
The assigned class derives from AGTCamera
The Blueprint implements CaptureGroundTruthNow
The Blueprint writes under CaptureManager.GetCurrentImagesDirectory()
The capture was started with /control = 1
```

---

## Manifest

`manifest.csv` is currently a synchronization index.

Current header:

```csv
session_id,frame_index,timestamp_sec,has_rover_gt,has_left_camera,has_right_camera,has_gt_camera
```

Each row says which synchronized outputs were available for a frame.

Example meaning:

```text
has_rover_gt     = rover pose source was valid
has_left_camera  = left camera pose was valid and left camera/GT output was enabled
has_right_camera = right camera pose was valid and right ROS camera was enabled
has_gt_camera    = ground-truth mode was enabled
```

The detailed ROS-style poses are written to the trajectory CSV files in `Navigation/`.

---

## Navigation trajectory files

The navigation trajectory files are written under:

```text
Saved/Datasets/<session_name>/Navigation/
```

Files:

```text
rover_gt_trajectory_ros.csv
left_camera_gt_trajectory_ros.csv
right_camera_gt_trajectory_ros.csv
```

Header:

```csv
timestamp_sec,frame_index,frame_id,child_frame_id,x_m,y_m,z_m,qx,qy,qz,qw
```

These files store synchronized poses using:

```text
position: meters
orientation: quaternion
frame_id: map
child_frame_id: base_link / left_camera_optical_frame / right_camera_optical_frame depending the pose source
```

Rows use the same `frame_index` and timestamp as the rest of the capture pipeline.

---

## Coordinate conversion

Unreal uses:

```text
position: centimeters
rotation: FRotator/FQuat
axis convention: X forward, Y right, Z up
```

ROS/navigation outputs use:

```text
position: meters
orientation: quaternion
axis convention: X forward, Y left, Z up
```

The shared conversion helper is:

```text
Source/simulator/Public/Utils/UnrealToRosConversion.h
Source/simulator/Private/Utils/UnrealToRosConversion.cpp
```

Position conversion:

```text
ros_x =  unreal_x / 100.0
ros_y = -unreal_y / 100.0
ros_z =  unreal_z / 100.0
```

Rotation conversion:

```text
q_ros = (-qx_unreal, qy_unreal, -qz_unreal, qw_unreal)
```

This represents:

```text
S = diag(1, -1, 1)
R_ros = S * R_unreal * S
```

Important:

```text
Do not use yaw-only conversion.
Full quaternion conversion is required so roll and pitch are preserved on slopes/uneven terrain.
```

Manual ROS message fields use `UnrealToRosConversion`.

TempoROS TF helper functions such as `PublishStaticTransform` take native Unreal `FTransform` and perform their own conversion internally.

For synchronized rover `/tf`, the project currently manually publishes `tf2_msgs/msg/TFMessage` so the transform count and timestamp exactly match `FCaptureFrameInfo`.

---

## Recommended RViz validation

Start Unreal and press Play.

Start capture:

```bash
./startcapture
```

Open RViz:

```bash
rviz2
```

Set:

```text
Fixed Frame = map
```

Add displays:

```text
TF
Map: /gt/map/occupancy
Pose: /rover/gt/pose
Path: /gt/path
Image: /left_camera/rgb/image_raw
Image: /right_camera/rgb/image_raw
Camera: /left_camera/rgb/image_raw
Camera: /right_camera/rgb/image_raw
```

Expected:

```text
TF tree contains map -> base_link -> cameras.
Pose arrow appears at base_link.
Path follows rover motion.
Occupancy map appears in map frame.
Camera images update while capture is running.
```

Useful TF checks:

```bash
ros2 run tf2_ros tf2_echo map base_link
ros2 run tf2_ros tf2_echo base_link left_camera_link
ros2 run tf2_ros tf2_echo base_link right_camera_link
ros2 run tf2_ros tf2_echo left_camera_link left_camera_optical_frame
ros2 run tf2_ros tf2_echo right_camera_link right_camera_optical_frame
```

Expected examples:

```text
map -> base_link:
  translation should match /rover/gt/pose.position

base_link -> left_camera_link:
  fixed camera mount transform

base_link -> right_camera_link:
  fixed camera mount transform
  y difference from left camera should match stereo baseline

camera_link -> camera_optical_frame:
  translation = [0,0,0]
  RPY = [-90,0,-90] degrees
```

If RViz shows OpenGL/map shader errors, try:

```bash
LIBGL_ALWAYS_SOFTWARE=1 rviz2
```

---

## Recommended sync test

Record the synchronized GT and frame-index topics:

```bash
ros2 bag record \
  /rover/gt/pose \
  /gt/odom \
  /tf \
  /gt/path \
  /left_camera/frame_index \
  /right_camera/frame_index \
  -o gt_sync_check
```

A healthy result should show matching message counts, for example:

```text
/rover/gt/pose            153
/gt/odom                  153
/gt/path                  153
/tf                       153
/left_camera/frame_index  153
/right_camera/frame_index 153
```

Matching counts mean the rover GT topics are being published at the same capture-frame rate as the camera frame-index topics.

`ros2 topic echo --once` in separate terminals can catch different frames, so timestamps may differ between terminals. Use a rosbag for a stronger synchronization check.

---

## Full rosbag validation test

For Stereo ROS + Ground Truth mode:

```bash
ros2 bag record \
  /tf \
  /tf_static \
  /left_camera/rgb/image_raw \
  /left_camera/camera_info \
  /left_camera/frame_index \
  /right_camera/rgb/image_raw \
  /right_camera/camera_info \
  /right_camera/frame_index \
  /rover/gt/pose \
  /gt/odom \
  /gt/path \
  /gt/map/occupancy \
  /cmd_vel \
  /control \
  -o final_validation_check
```

Recommended flow:

```bash
./startcapture
# move rover for a few seconds
ros2 topic pub /control std_msgs/msg/Int32 "{data: 0}" --once
sleep 5
# Ctrl+C the rosbag recorder
```

Then check:

```bash
ros2 bag info final_validation_check
```

Expected counts:

```text
/rover/gt/pose              same count as /gt/odom
/gt/odom                    same count as /gt/path
/tf                         same count as /rover/gt/pose
/left_camera/frame_index    same count as /right_camera/frame_index
/left_camera/rgb/image_raw  same count as /right_camera/rgb/image_raw
/left_camera/camera_info    same count as /left_camera/frame_index
/right_camera/camera_info   same count as /right_camera/frame_index
/tf_static                  1
/gt/map/occupancy           may be different; map is independent/static
```

Current validated example:

```text
/rover/gt/pose              56
/gt/odom                    56
/gt/path                    56
/tf                         56
/left_camera/frame_index    56
/right_camera/frame_index   56
/left_camera/rgb/image_raw  56
/right_camera/rgb/image_raw 56
/left_camera/camera_info    56
/right_camera/camera_info   56
/tf_static                  1
```

Also validate `/tf` QoS:

```bash
ros2 topic info /tf -v
```

Every `/tf` publisher should show:

```text
Durability: VOLATILE
```

`/tf_static` is allowed to be transient-local/latched.

---

## Basic manual test flow

1. Open Unreal.
2. Place/confirm one `RobotCamRig` in the level.
3. Assign `GroundTruthCameraClass` if using Ground Truth modes.
4. Place/confirm `BP_RoverVehicle` in the level.
5. Add/confirm these components on `BP_RoverVehicle`:

```text
RoverCmdVelVehicleControllerComponent
RoverGroundTruthPublisherComponent
RoverSensorMount
```

6. In `RobotCamRig`, assign the current rover actor as `RoverActor`.
7. Confirm `RobotCamRig` attaches to `RoverSensorMount` at BeginPlay.
8. Place/confirm `BP_GroundTruthMapPublisher` if map output is needed.
9. Set the capture mode in the Simulator Config window.
10. Press Play.
11. Start capture:

```bash
./startcapture
```

12. Move the rover:

```bash
ros2 launch simulator_ros teleop.launch.py
```

13. Stop capture:

```bash
./stopcapture
```

14. Wait a few seconds before stopping PIE if UnrealGT or RGB capture is enabled.

---

## Shutdown flow

UnrealGT writes image files asynchronously. RGB image publishing also uses async GPU readback. To avoid losing final files/messages:

```text
1. Start capture.
2. Stop capture with /control = 0.
3. Wait 3-5 seconds.
4. Stop Play / PIE.
```

A future improvement is to add an explicit flush/wait mechanism for pending UnrealGT save tasks and pending RGB readbacks on capture stop.

---

## Notes

A timestamp difference of `0.000000001` seconds is one nanosecond. This can happen when converting between C++ `double`, CSV text, and ROS `sec/nanosec`. It is safe for evaluation and far smaller than a normal frame interval.

`segmentation_info.json` is metadata, not one file per frame. Per-frame segmentation images are stored in `Images/Segmentation/`.

The current rover is a prototype. Rover motion realism is not the core dataset pipeline. The important reusable parts are:

```text
RoverCmdVelVehicleControllerComponent
RoverGroundTruthPublisherComponent
CapturePoseSourceComponent
CaptureManager synchronization
RobotCamRig synchronized frame triggering
RoverSensorMount camera-rig mounting
UnrealToRosConversion helper
OccupancyMapPublisherComponent
```

These should be kept reusable so the final rover can replace `BP_RoverVehicle` with minimal code changes.

---

## Suggested future improvements

```text
- Route keyboard WASD input through RoverCmdVelVehicleControllerComponent if manual and ROS control are both needed
- Add velocity/twist computation to /gt/odom instead of leaving twist at zero
- Add an optional /odom topic for robot-consumed odometry
- Add optional map -> odom -> base_link mode for Nav2/SLAM compatibility
- Add optional noisy/drifting odometry while keeping /gt/odom perfect
- Add TUM trajectory export for SLAM evaluation
- Add explicit flush/wait for pending UnrealGT save tasks and RGB readbacks on capture stop
- Save /gt/map/occupancy as .pgm + .yaml in the dataset folder
- Add object metadata and instance-mask outputs
- Add dataset validation script that checks missing frame indexes and missing files
- Add map exports such as heightmap, semantic map, elevation map, or traversability map
```