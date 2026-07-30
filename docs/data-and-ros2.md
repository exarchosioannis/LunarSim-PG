# Data outputs and ROS 2

LunarSim-PG can save synchronised ground-truth data directly to
`Saved/Datasets/`, publish sensor and control data through ROS 2, or do both
during the same run.

Choose what to produce in
[Simulator configuration](configuration.md), press **Apply Settings** for the
capture, sensor, and rover settings, and then start Play In Editor. Sun
elevation and azimuth use the separate **Apply Sun Direction** action and do
not change which data outputs are enabled.

## Data saved to disk

Press `C` during Play In Editor to start a capture session. Press it again to
stop. A run can contain several sessions:

```text
Saved/Datasets/<run>/
├── Maps/
└── Session_001/
    ├── Images/
    ├── Navigation/
    ├── manifest.csv
    └── session_metadata.json
```

Starting capture again creates `Session_002`, then `Session_003`, and so on.
Only the outputs enabled in Simulator Config are created.

### Images

| Output | What you get |
| --- | --- |
| **RGB** | Ground-truth PNG images aligned with the left camera view. |
| **Depth** | Depth PNG images with distance stored in millimeters. Use `Tools/convert_depth.py` to create grayscale previews; keep the original files for numerical use. |
| **Segmentation** | Color-coded PNG images with JSON legends describing the colors and classes generated for the current level. |
| **Bounding boxes** | One image-space CSV per frame. Use `Tools/draw_bounding_boxes.py` to preview the boxes over the RGB images. |

Bounding-box generation can reduce capture performance in dense scenes. Check
the generated CSV files and previews before using them as final benchmark
labels.

### UnrealGT integration

Ground-truth RGB, depth, segmentation, and bounding boxes are generated using a
modified version of [UnrealGT](https://github.com/unrealgt/unrealgt).

For LunarSim-PG, the plugin was adapted with asynchronous GPU readback to reduce
blocking during image capture, a camera warm-up phase before capture begins,
and capture frame and simulation timestamp association so every generated
output can be matched with the corresponding manifest and trajectory data.

The modified plugin is included under
[`Plugins/unrealgt/`](../Plugins/unrealgt/).

### Navigation and session information

When **Trajectory CSV** is enabled, LunarSim-PG saves ground-truth poses for the
rover and the available cameras:

| File | Contains |
| --- | --- |
| `rover_gt_trajectory_ros.csv` | Rover pose |
| `left_camera_gt_trajectory_ros.csv` | Left camera pose |
| `right_camera_gt_trajectory_ros.csv` | Right camera pose when stereo publishing is enabled |

Every trajectory row includes a capture frame index and simulation timestamp.
This lets you match poses with the corresponding ground-truth images and align
them with ROS 2 stereo images by timestamp. The trajectories can be used as
reference data when evaluating visual odometry, SLAM, localisation, or rover
motion estimates.

`manifest.csv` connects each captured frame with its generated files,
trajectory data, and timestamp.

`session_metadata.json` stores the main session information, including the
level, camera calibration, capture rate, enabled outputs, frame names, and map
paths.

### Ground-truth maps

Maps are generated once for the Play In Editor run and stored under `Maps/`.

| Map | Contains | Possible use |
| --- | --- | --- |
| **Occupancy** | Free, occupied, and unknown cells | Mapping and obstacle-map evaluation |
| **Elevation** | Terrain height values | Elevation-map and reconstruction evaluation |
| **Slope** | Local terrain slope values | Traversability research and evaluation |

These are reference products. LunarSim-PG provides the data but does not
automatically evaluate an algorithm against it.

## ROS 2 interface

LunarSim-PG uses [TempoROS](https://github.com/tempo-sim/TempoROS) as its
Unreal Engine–ROS 2 bridge.

| Topics | What they provide |
| --- | --- |
| `/stereo/left/image_raw`, `/stereo/right/image_raw` | Left and right stereo RGB images during active capture |
| `/stereo/left/camera_info`, `/stereo/right/camera_info` | Camera calibration for the stereo images |
| `/imu/data` | Simulated rover IMU data |
| `/ground_truth/pose`, `/ground_truth/odom`, `/ground_truth/path` | Live rover ground-truth state and trajectory |
| `/ground_truth/map/occupancy`, `/ground_truth/map/elevation_points` | Ground-truth map data |
| `/clock`, `/tf`, `/tf_static` | Simulation time and rover/sensor transforms |
| `/cmd_vel` | Rover velocity commands when **Control Mode** is **cmd_vel** |
| `/capture/control` | Starts capture with `1` and stops it with `0` |

### Detailed topic reference

| Topic | Message type | Frame or units | Default rate and QoS |
| --- | --- | --- | --- |
| `/stereo/left/image_raw`, `/stereo/right/image_raw` | `sensor_msgs/msg/Image` | `left_camera_optical_frame` or `right_camera_optical_frame`; `bgr8` encoding | **Capture Hz** while capture is active; reliable, volatile, queue depth `20` |
| `/stereo/left/camera_info`, `/stereo/right/camera_info` | `sensor_msgs/msg/CameraInfo` | Matching optical frame; ideal pinhole calibration with `plumb_bob` and zero distortion coefficients | Published with each matching image; reliable, volatile, queue depth `20` |
| `/imu/data` | `sensor_msgs/msg/Imu` | `imu_link`; angular velocity in rad/s and linear acceleration in m/s² | **IMU Hz**, default `100` Hz and limited by simulation FPS; reliable, volatile, queue depth `50` |
| `/ground_truth/pose` | `geometry_msgs/msg/PoseStamped` | `map`; position in metres | Default `20` Hz; reliable, volatile, queue depth `10` |
| `/ground_truth/odom` | `nav_msgs/msg/Odometry` | Header frame `map`, child frame `base_link`; twist in m/s and rad/s | Default `20` Hz; reliable, volatile, queue depth `10` |
| `/ground_truth/path` | `nav_msgs/msg/Path` | `map`; retains up to `5000` poses | Default `10` Hz; reliable, transient local, queue depth `1` |
| `/ground_truth/map/occupancy` | `nav_msgs/msg/OccupancyGrid` | `map`; default resolution `0.40` m/cell | Generated at run start when enabled and republished every `5` s; reliable, transient local, queue depth `1` |
| `/ground_truth/map/elevation_points` | `sensor_msgs/msg/PointCloud2` | `map`; `x`, `y`, and `z` are `float32` metres | Generated and republished with the occupancy map; reliable, transient local, queue depth `1` |
| `/cmd_vel` | `geometry_msgs/msg/Twist` | `linear.x` in m/s and `angular.z` in rad/s | Subscription active in **cmd_vel** mode; reliable, volatile, queue depth `1` |
| `/capture/control` | `std_msgs/msg/Int32` | `1` starts capture and `0` stops capture | Reliable, volatile, queue depth `1` |
| `/clock`, `/tf`, `/tf_static` | Standard ROS 2 clock and TF messages managed by TempoROS | Simulation time and dynamic/static transforms | Managed by the TempoROS clock and TF APIs |

Rates above are implementation defaults. Effective runtime rates can be lower
when Unreal cannot maintain the requested update frequency.

### Frames and coordinate conventions

The main frame tree is:

```text
map
└── base_link
    ├── imu_link
    ├── left_camera_link
    │   └── left_camera_optical_frame
    └── right_camera_link
        └── right_camera_optical_frame
```

See [Coordinate frames](coordinate-frames.md) for the Unreal-to-ROS axis
conversion, units, camera optical frames, and the topics associated with each
frame.

The stereo camera topics publish only while capture is active. Press `C` in the
Play In Editor viewport, or use `/capture/control`, to start and stop capture.

The IMU, rover ground truth, transforms, clock, and maps run independently of
the capture session. Their effective rates can still be limited by Unreal
runtime performance.

ROS 2 topics are not automatically saved to the dataset directory. Use
`ros2 bag` when you need to keep them after the simulation ends.