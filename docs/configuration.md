# Configuration

LunarSim-PG uses three user-facing configuration mechanisms:

1. **Simulator Config** in Unreal Editor for runtime outputs, cameras, IMU, and
   rover control.
2. `Config/DefaultPlugins.ini` and `docker/docker-compose.yml` for ROS
   networking.
3. The MoonSim terrain GUI for profile, seed, heightmap, crater, and rock
   settings.

## Simulator Config

Open **Window > Simulator > Simulator Config**. Apply settings while Play In
Editor is stopped; the Apply button and world-setup actions are disabled during
play.

### Outputs

| Setting | Type | Default | Allowed values | Description |
| --- | --- | --- | --- | --- |
| Stereo ROS Images + CameraInfo | Boolean | Enabled | Enabled/disabled | Publishes both stereo RGB images and camera-info messages during active capture. |
| Ground Truth Images | Boolean | Enabled | Enabled/disabled | Master switch for the four file-backed ground-truth modalities below. |
| RGB | Boolean | Enabled | Enabled/disabled | Writes the ground-truth camera RGB PNG. |
| Depth | Boolean | Enabled | Enabled/disabled | Writes UnrealGT RGB-packed depth PNG. |
| Segmentation | Boolean | Enabled | Enabled/disabled | Writes color-coded segmentation PNG and legends. |
| Bounding Boxes | Boolean | Enabled | Enabled/disabled | Writes per-frame bounding-box CSV. |
| Trajectory CSV | Boolean | Enabled | Enabled/disabled | Writes rover and applicable camera pose CSV files. |
| Ground Truth ROS Maps | Boolean | Enabled | Enabled/disabled | Generates run-level map files and publishes occupancy/elevation messages. |
| Show Capture Status Overlay | Boolean | Enabled | Enabled/disabled | Shows capture state without changing the stop cooldown. |

Ground-truth subtype switches have no effect when the master switch is
disabled. Map files are run-level products and do not schedule capture frames.

### Camera and capture

| Setting | Type | Default | Allowed values | Description |
| --- | --- | --- | --- | --- |
| Resolution | Enum | 1024x1024 | 640x360, 1024x576, 1280x720, 1920x1080, 640x640, 1024x1024 | Shared RGB, calibration, and ground-truth image size. |
| Horizontal FOV deg | Float | 90 | 5–170 degrees | Horizontal pinhole field of view. |
| Capture Hz | Float | 6 | 0.001–60 Hz in the editor | Schedules active-capture camera and file outputs. Effective rate cannot exceed Unreal performance. |
| Stereo Baseline cm | Float | 20 | 1–200 cm | Separation used for the stereo rig, calibration, and right projection matrix. |

Intrinsics are derived rather than entered:

```text
fx = fy = image_width / (2 * tan(horizontal_fov / 2))
cx = image_width / 2
cy = image_height / 2
```

The distortion model is `plumb_bob` with five zero coefficients.

### Rover and IMU

| Setting | Type | Default | Allowed values | Description |
| --- | --- | --- | --- | --- |
| IMU Hz | Float | 100 | 1–400 Hz | Requested IMU publication rate. The implementation explicitly caps effective publication to game-thread FPS. |
| Control Mode | Enum | WASD | WASD, `cmd_vel`, Disabled | Selects keyboard, ROS 2 Twist, or no rover control. |

The `cmd_vel` controller defaults are implemented in the rover component:

| Setting | Default | Valid constraint |
| --- | --- | --- |
| Topic | `/cmd_vel` | Canonical project endpoint |
| Maximum linear speed | 1 m/s | At least 0.01 m/s |
| Maximum angular speed | 1 rad/s | At least 0.01 rad/s |
| Command timeout | 0.75 s | Non-negative |
| Input dead zone | 0.02 | 0–1 |
| Invert throttle/steering | Both false | Boolean |
| Timeout stop | Rover stop / idle brake | Rover stop or full brake |

These detailed controller fields are component properties, not controls in the
Simulator Config window.

## Applying changes

Click **Apply Settings**, save the affected level/assets if prompted, then
start or restart Play In Editor. A C++ rebuild is not required for these
setting changes.

The first capture configuration registered during a Play In Editor session is
frozen for that dataset run. Stop and restart Play In Editor before changing
run-level capture settings.

## ROS networking

`Config/DefaultPlugins.ini` contains:

```ini
[/Script/TempoROS.TempoROSSettings]
FixedFrameName=map
ROSDomainID=9
RMWImplementation=CycloneDDS
```

The Docker environment matches these values with `ROS_DOMAIN_ID=9`,
`ROS_LOCALHOST_ONLY=0`, and
`RMW_IMPLEMENTATION=rmw_cyclonedds_cpp`.

If changing the domain ID, keep the Unreal config and Docker environment
identical, then restart both the editor and the container. Runtime ROS
parameters, ROS services, and ROS actions are not implemented.

## Terrain configuration

Run the graphical tool from the repository root:

```bash
./Tools/Terrain_Generation/moonsim.sh
```

The frequently used fields are profile, seed, heightmap size, map size, encoded
height range, range mode, heightmap preset, rock profile, and the two crater
abundance values. Rock settings are grouped under Limits, Background,
Bart–Melosh scaling, Crater abundance, Zone fractions, Zone bounds, Distance
and size, Clumping, Random big-rock clumps, Unreal placement, and Materials.

Changing terrain settings requires regenerating the affected heightmap and
rockfield. The repository does not connect terrain settings to an existing
Unreal Landscape automatically. See [Terrain generation](terrain-generation.md).

The generator records a fixed preview hillshade azimuth of 135 degrees and
elevation of 25 degrees. These are metadata for the preview calculation; they
are not user-facing illumination controls and do not configure Unreal lights.
Configurable end-to-end illumination transfer is **Not yet verified**.

## Output locations

Output directories are fixed by the implementation:

| Product | Location |
| --- | --- |
| Terrain run packages | `Tools/Terrain_Generation/generated/` |
| Copies prepared for Unreal import | `Tools/Terrain_Generation/unreal_import/` |
| Dataset runs | `Saved/Datasets/` |
| Rosbags written in the container | Host `bags/` through container `/ws/bags` |

The Unreal dataset root is not user-configurable in the current interface.

