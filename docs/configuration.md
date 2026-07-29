# Simulator configuration

The **Simulator Config** window lets you choose the capture, ROS 2, timing,
camera-pose, rover-pose, sensor, and rover-control settings for the next Play In
Editor run. It also provides actions for preparing the current level.

## Opening the window

1. Open the level you want to run.
2. Go to **Window > Simulator > Simulator Config**.
3. Choose the settings you need.
4. Press **Apply Settings**.
5. Start Play In Editor.

## Configuration blocks

The defaults below are the implementation defaults. If the level already
contains a configured rover, the window shows that rover's saved values
instead. The first four blocks appear under **Data Generation and ROS 2** in
the window.

### Outputs

This block controls the main session outputs and the run-level ground-truth
maps.

| Option | Default | What it does |
| --- | --- | --- |
| **Stereo ROS Images + CameraInfo** | Enabled | Publishes left and right RGB images with matching camera information during an active capture session. Enable this when ROS 2 needs the stereo camera stream; it does not write the stereo images to the dataset folder. |
| **Trajectory CSV** | Enabled | Writes synchronized pose rows at **Capture Hz** while capture is active. Enable this when you need offline rover or camera poses; camera pose files depend on which camera outputs are enabled. |
| **Ground Truth ROS Maps** | Enabled | Computes the ground-truth maps, writes the run-level map outputs, and publishes the ROS 2 map data. Enable this when you need occupancy and elevation products; the calculation starts during Play In Editor and does not wait for `C`. |

**Ground Truth ROS Maps** can be changed only when the level contains the
ground-truth map publisher. Map calculation can reduce runtime performance
while it is running.

### Ground Truth Outputs

This block controls the single ground-truth camera view, which is aligned with
the left camera. **Ground Truth Images** is the master switch; its four output
choices are available only while the master switch is enabled.

| Option | Default | What it does |
| --- | --- | --- |
| **Ground Truth Images** | Enabled | Enables file-backed ground-truth capture. Enable this when you want any of the four outputs below. |
| **RGB** | Enabled | Writes the ground-truth RGB image for each active capture frame. Choose this when you need a color image aligned with the other ground-truth products. |
| **Depth** | Enabled | Writes a depth image for each active capture frame. Use this option when your dataset needs per-pixel distance information. |
| **Segmentation** | Enabled | Writes the color-coded segmentation image and its legends. Enable this when you need labeled scene regions. |
| **Bounding Boxes** | Unchecked | Writes per-frame image-space bounding-box data. Enable this only when needed because it can significantly reduce capture performance in dense scenes. |

Turning off **Ground Truth Images** leaves the four choices selected but makes
them inactive. If the master switch is enabled with no output selected,
**Apply Settings** reports that there are no selected ground-truth outputs.

### Capture

This block controls the shared camera calibration and the rate of active
capture sessions. The resolution and field of view apply to both stereo ROS 2
images and ground-truth images.

| Option | Default and allowed values | What it does |
| --- | --- | --- |
| **Resolution** | Default: `1024x1024`<br>Choices: `640x360`, `1024x576`, `1280x720`, `1920x1080`, `640x640`, `1024x1024` | Sets the image size and the matching camera calibration. Choose a smaller size for lighter capture or a larger size when you need more image detail. |
| **Horizontal FOV deg** | Default: `90`<br>Range: `5`–`170` degrees | Sets the horizontal camera field of view and updates the camera calibration. Change it when you need a wider or narrower view. |
| **Capture Hz** | Default: `6`<br>Range: `0.001`–`60` Hz | Sets the target rate for active capture frames. Lower it to reduce capture load or raise it for denser sampling; the achieved rate can be lower if the simulation or rendering cannot keep up. |
| **Stereo Baseline cm** | Default: `20`<br>Range: `1`–`200` cm | Sets the distance between the left and right cameras, including the right camera pose and stereo calibration. Change it when you want the simulated stereo rig to match a different physical baseline. |

### Rover / Sensors

This block controls the IMU publication rate and which input can drive the
rover.

| Option | Default and allowed values | What it does |
| --- | --- | --- |
| **IMU Hz** | Default: `100`<br>Range: `1`–`400` Hz | Sets the requested ROS 2 IMU publication rate. Change it when your ROS 2 workflow needs a different sampling rate; it runs independently of capture and cannot exceed the simulation frame rate. |
| **Control Mode** | Default: **WASD**<br>Choices: **WASD**, **cmd_vel** | Chooses the rover input source. Use **WASD** for keyboard driving, or choose **cmd_vel** before sending ROS 2 velocity commands. |

In **WASD** mode, use `W` to drive forward, `S` to brake or reverse, and `A`
and `D` to steer. For ROS 2 control commands and limits, see the
[Data outputs and ROS 2](data-and-ros2.md) guide.

### Apply Settings

Press **Apply Settings** after making changes. The button requires a complete
rover setup in the open level and is unavailable while Play In Editor or
simulation is running. If the level is missing that setup, use
**Create / Update Rover + Ground Truth** in the next block.

The action updates the current level, so save the level if you want to keep the
settings for later editor sessions. If the level contains more than one
complete rover setup, the window applies the settings to the first one it
finds and reports this below the button.

### World Setup

This block prepares required actors in the current level. Both actions are
disabled during Play In Editor.

| Option | What it does |
| --- | --- |
| **Create / Update Sky, Earth and Sun** | Creates or updates the standard directional light, Earth, Sky, Sun, and Sun glow controller. Use this option to prepare a new level or restore the standard environment setup; it resets the Earth, Sky, and Sun to their preset transforms. |
| **Create / Update Rover + Ground Truth** | Creates or updates the rover and ground-truth map publisher, then refreshes the window's configuration target. Use this option when the level does not yet have a complete rover setup; it places the rover and map publisher at the world origin. |

These actions change the level immediately. Save the level when you want to
keep the result.

### Terrain Generation

The **Terrain Generation** block contains the separate **Rock Field Baking**
workflow. See [Terrain generator workflow](terrain-generator-workflow.md) for
its inputs and actions.

## Important behavior

- **Capture Hz** controls session capture frames. It affects enabled
  ground-truth images, stereo ROS 2 images, and **Trajectory CSV** sampling,
  but it does not change **IMU Hz**, live rover ground-truth publication, or
  map publication.
- **Trajectory CSV** uses the same frame index and simulation timestamp as the
  matching capture frame. It writes the rover pose, the left camera pose when
  stereo or ground truth is enabled, and the right camera pose when stereo is
  enabled. There are no separate frame, timestamp, or pose switches in this
  window.
- **Ground Truth ROS Maps** is a run-level option. When enabled, map generation
  and publication begin after Play In Editor starts and do not require an
  active capture session.
- With the Play In Editor viewport focused, `C` starts or stops the enabled
  session outputs: ground-truth images, stereo ROS 2 images, and trajectory
  capture. It does not toggle the IMU, live rover ground truth, clock,
  transforms, or run-level maps.
