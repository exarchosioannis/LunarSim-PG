# Dataset generation

LunarSim-PG can write ground-truth sessions from Play In Editor and stream
additional data to ROS 2. These paths are complementary:

| Workflow | Implemented behavior |
| --- | --- |
| Offline ground-truth capture | Writes ground-truth camera images, bounding boxes, calibration/session metadata, trajectories, and run-level maps below `Saved/Datasets/` |
| Online ROS 2 operation | Streams stereo images, IMU, rover ground truth, TF, clock, and maps; accepts `cmd_vel` |
| Rosbag recording | Persists selected ROS streams in host `bags/` |
| Scripted dataset trajectory | Not present; use manual keyboard or ROS control |

## 1. Prepare terrain provenance

Generate or select a terrain as described in
[Terrain generation](terrain-generation.md), then import it into the desired
Unreal level.

Keep the matching heightmap and rockfield run directories. Unreal session
metadata does not currently include the terrain profile, seed, crater
parameters, rock settings, or preview illumination metadata. Copying or
otherwise associating those packages with the resulting dataset is a manual
provenance step.

The generator's recorded illumination values apply to preview hillshade only.
End-to-end configurable illumination transfer into Unreal is not implemented;
set scene lighting in Unreal and record it externally if required.

## 2. Configure capture

With Play In Editor stopped:

1. Open **Window > Simulator > Simulator Config**.
2. Ensure the level contains the rover and ground-truth actors. Use
   **Create/Update Rover + Ground Truth** if needed.
3. Enable the desired outputs.
4. Set resolution, horizontal FOV, Capture Hz, stereo baseline, IMU rate, and
   rover control mode.
5. Click **Apply Settings** and save the level if prompted.

The first applied capture configuration used in a Play In Editor run is frozen
for its run-level producers. Restart Play In Editor after changing it.

## 3. Start the run

Start Play In Editor. Unreal creates:

```text
Saved/Datasets/YYYY-MM-DD_HH-MM-SS/
```

If that name exists, suffixes begin at `_02`. The active directory path is
written to:

```text
Saved/Datasets/current_dataset_run.txt
```

When maps are enabled and `BP_GroundTruthMapPublisher` is present, its run-level
map products are generated without starting a capture session.

## 4. Start capture

Either press `C` in the Play In Editor viewport or start it from the sourced
ROS container:

```bash
bash /ws/scripts/startcapture
```

The script publishes:

```bash
ros2 topic pub /capture/control std_msgs/msg/Int32 "{data: 1}" --once
```

The first active capture becomes `Session_001`; later captures in the same
Play In Editor run become `Session_002`, `Session_003`, and so on.

When ground-truth camera outputs are enabled, capture begins only after their
warm-up checks complete. The configured default rate is 6 Hz.

## 5. Drive a trajectory

Choose one implemented control method:

- **WASD mode:** `W`/`S` for forward/brake or reverse, `A`/`D` for steering.
- **cmd_vel mode:** publish `/cmd_vel` or run:

  ```bash
  ros2 launch simulator_ros teleop.launch.py
  ```

No scripted path runner is included. If a deterministic external controller is
used, its code, configuration, and commands must be preserved separately.

## 6. Stop and finalize

Press `C` again or run:

```bash
bash /ws/scripts/stopcapture
```

The script publishes data `0` once. Wait for the three-second cooldown and the
Ready state before stopping Play In Editor. Image writers are asynchronous;
the cooldown allows outstanding work to progress, but no automated file
completeness validator is included.

## 7. Locate and inspect the session

From the repository root:

```bash
DATASET_RUN="$(tr -d '\r\n' < Saved/Datasets/current_dataset_run.txt)"
find "$DATASET_RUN/Session_001" -maxdepth 3 -type f | sort
```

The session contains `manifest.csv`, `session_metadata.json`, enabled image
folders, and enabled trajectory files. Maps live in the run-level `Maps/`
directory, not under `Session_001`.

Review the exact schemas in [Output format](output-format.md). At minimum,
manually confirm that each non-empty image path in `manifest.csv` exists and
that expected trajectory rows contain the same frame indices.

## 8. Post-process optional previews

NumPy and Pillow are required. From the repository root:

```bash
DATASET_RUN="$(tr -d '\r\n' < Saved/Datasets/current_dataset_run.txt)"
python3 Tools/convert_depth.py "$DATASET_RUN/Session_001"
python3 Tools/draw_bounding_boxes.py "$DATASET_RUN/Session_001"
```

The first command writes `Images/Depth_Greyscale/`; the second writes
`Images/RGB_BoundingBoxes/`. These are visualization products, not replacements
for the encoded source data.

Available options are:

```bash
python3 Tools/convert_depth.py --help
python3 Tools/draw_bounding_boxes.py --help
```

No dataset export or validation script beyond these visualization tools is
present.

## Record ROS-only modalities

Stereo views, IMU, live odometry, TF, path, clock, elevation points, and ROS
map messages are not all duplicated as per-frame offline files. Record a
rosbag during the same run when they are required:

```bash
cd /ws/bags
ros2 bag record \
  /clock /tf /tf_static \
  /stereo/left/image_raw /stereo/left/camera_info \
  /stereo/right/image_raw /stereo/right/camera_info \
  /imu/data /ground_truth/pose /ground_truth/odom /ground_truth/path
```

Stop with `Ctrl+C`. See [ROS 2 interface](ros2-interface.md) for the complete
topic set.

## Reproducibility record

| Value | Where it is recorded |
| --- | --- |
| Dataset run ID | Run directory name from wall-clock time |
| Session ID/name | `session_metadata.json` and `manifest.csv` |
| Scene level | `session_metadata.json` |
| Camera settings/calibration | `session_metadata.json` |
| Enabled ground-truth outputs | `session_metadata.json` |
| Capture rate | `session_metadata.json` |
| Simulation timestamp and frame index | `manifest.csv` and trajectory CSV files |
| Terrain profile and seed | Terrain `metadata.json` / `rock_settings.json`; not copied to session metadata |
| Crater and rock instances | Terrain `craters.json` / `unreal_rockfield.json`; not copied to session |
| Unreal illumination settings | **To be documented**; not captured by session metadata |
| External controller or path | Not recorded by the offline dataset writer |

