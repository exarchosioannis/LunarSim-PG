# Coordinate frames

LunarSim-PG converts Unreal Engine transforms into ROS 2 coordinates before
publishing poses, odometry, IMU data, maps, and TF.

## Axis and unit conversion

Unreal Engine uses:

```text
X forward
Y right
Z up
distance in centimetres
```

The ROS 2 interface uses:

```text
X forward
Y left
Z up
distance in metres
```

For positions, the conversion is:

```text
x_ros =  x_unreal / 100
y_ros = -y_unreal / 100
z_ros =  z_unreal / 100
```

Rotations are converted with the same Y-axis reflection so that published
quaternions and transforms remain consistent with the ROS coordinate basis.

## Frame tree

The default frame hierarchy is:

```text
map
└── base_link
    ├── imu_link
    ├── left_camera_link
    │   └── left_camera_optical_frame
    └── right_camera_link
        └── right_camera_optical_frame
```

- `map` is the global simulation and ground-truth frame.
- `base_link` is the rover body frame. LunarSim-PG publishes the dynamic
  `map -> base_link` transform.
- `imu_link` is fixed relative to `base_link` using the rover's IMU mount.
- `left_camera_link` and `right_camera_link` are fixed relative to
  `base_link` using the simulated camera extrinsics.
- Each camera link has a fixed optical-frame transform. Image and CameraInfo
  headers use the optical frames, following the ROS optical convention:
  X right, Y down, Z forward.

The right camera is offset from the left/reference camera by the configured
stereo baseline. The left camera and the ground-truth camera share the same
reference pose.

## Topic frame IDs

| Topic | Header or child frame |
| --- | --- |
| `/stereo/left/image_raw`, `/stereo/left/camera_info` | `left_camera_optical_frame` |
| `/stereo/right/image_raw`, `/stereo/right/camera_info` | `right_camera_optical_frame` |
| `/imu/data` | `imu_link` |
| `/ground_truth/pose` | `map` |
| `/ground_truth/odom` | Header: `map`; child: `base_link` |
| `/ground_truth/path` | `map` |
| `/ground_truth/map/occupancy` | `map` |
| `/ground_truth/map/elevation_points` | `map` |

`/tf` carries the dynamic rover transform. `/tf_static` carries the IMU and
camera extrinsics.

## Units

- Positions, map coordinates, and elevation point-cloud coordinates: metres.
- Linear velocity: metres per second.
- Angular velocity: radians per second.
- IMU linear acceleration: metres per second squared.
- Orientations: unit quaternions.
- Occupancy-map resolution: metres per cell.
- Camera calibration: pixel units for focal lengths and principal points;
  metres for the stereo baseline encoded in the right projection matrix.

## Checking the frame tree

With Play In Editor running and the ROS 2 container active, inspect the topics
and transforms with standard ROS tools, for example:

```bash
ros2 topic echo --once /ground_truth/odom
ros2 topic echo --once /imu/data
ros2 topic echo --once /tf_static
```

Use ROS remapping when an external stack expects different topic names. For
different frame names, configure the external stack for the canonical frames
or add an external TF adapter; the simulator publishes one canonical frame
tree.
