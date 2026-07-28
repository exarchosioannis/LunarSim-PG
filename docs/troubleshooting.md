# Troubleshooting

Use the repository scripts and Unreal Output Log as the first diagnostics. Do
not manually delete Unreal build directories when the supported
`./build.sh --clean` operation is sufficient.

## Git LFS assets are pointers or missing

**Likely cause**

Git LFS was not installed or its objects were not retrieved. Two lunar-regolith
material assets are LFS-managed.

**Fix**

From the repository root:

```bash
git lfs install
git lfs pull
git lfs ls-files
```

The final command should list the base-color and normal material assets below
`Content/Materials/LunarRegolithShader/`.

## `setup.sh` reports that `UE_ROOT` is not set or has the wrong version

**Likely cause**

`UE_ROOT` is absent, points at the `Engine/` subdirectory, or does not describe
Unreal Engine 5.7.x.

**Fix**

Point it at the engine root and rerun setup:

```bash
export UE_ROOT=/path/to/UnrealEngine-5.7.x
test -f "$UE_ROOT/Engine/Build/Build.version"
./setup.sh
```

The setup script reads the major and minor values and requires `5.7`.

## `setup.sh` reports a missing command

**Likely cause**

One of `git`, `git-lfs`, `curl`, or `jq` is unavailable.

**Fix**

Install the named tool through the host's package-management policy, then
rerun `./setup.sh`. Exact distribution installation commands are
**To be documented**.

## TempoROS setup or plugin checks fail

**Likely cause**

The repository is incomplete, `UE_ROOT` is invalid, or TempoROS setup did not
create its Humble third-party includes, libraries, and binaries.

**Fix**

Retrieve LFS data and rerun the canonical setup:

```bash
git lfs pull
./setup.sh
```

The script verifies:

```text
Plugins/TempoROS/TempoROS.uplugin
Plugins/unrealgt/UnrealGT.uplugin
Plugins/TempoROS/Source/ThirdParty/rclcpp/Includes
Plugins/TempoROS/Source/ThirdParty/rclcpp/Libraries/Linux
Plugins/TempoROS/Source/ThirdParty/rclcpp/Binaries/Linux
```

There is no `.gitmodules` file in the current repository, so a missing plugin
is not repaired by submodule initialization.

## Unreal project files or editor modules fail to load

**Likely cause**

Setup was skipped, the editor target was built against a different engine, or
the build is stale. The repository does not provide a separate
GenerateProjectFiles command.

**Fix**

From the repository root:

```bash
export UE_ROOT=/path/to/UnrealEngine-5.7.x
./setup.sh
./build.sh --clean
./open_project.sh
```

Inspect the terminal build output and **Window > Output Log** for the first
reported compile or module error.

## Rover setup is only partially created

**Likely cause**

The editor action could not resolve a complete rover Blueprint pipeline. The
tracked rover asset is:

```text
Content/3D_Models/LargeRover/Model/ESA_Rover.uasset
```

The editor's hard-coded candidate load paths do not include that full content
path, although it can find an already loaded class named `ESA_Rover_C`.

**Fix**

With Play In Editor stopped, locate and open `ESA_Rover` in the Content Browser
so its class is loaded, then retry **Create/Update Rover + Ground Truth**.
Check the Output Log. If it still fails, manually placing the tracked
`ESA_Rover` asset is possible, but the required complete component wiring is
**To be documented**; do not assume an arbitrary rover actor is equivalent.

## Editor opens with missing material assets

**Likely cause**

Git LFS data is missing.

**Fix**

Close Play In Editor, run:

```bash
git lfs pull
```

Reopen the project. If non-LFS assets are missing, verify the checkout rather
than copying assets from an unrelated project.

## Vulkan or rendering initialization fails

**Likely cause**

The host driver cannot provide the Linux Vulkan SM6 path selected by
`Config/DefaultEngine.ini`, or ray-tracing support/configuration is
incompatible.

**Fix**

Read the first Vulkan/RHI error in the Unreal launch terminal or Output Log and
verify that the host driver supports Vulkan SM6. Exact supported driver
versions and a reduced-rendering fallback are **Not yet verified**. The
repository does not establish an NVIDIA-only requirement.

## ROS 2 topics are not visible

**Likely cause**

Play In Editor is not running, the container/workspace is not sourced, or ROS
domain/RMW settings do not match.

**Fix**

Inside the container:

```bash
source /opt/ros/humble/setup.bash
cd /ws/ros2_ws
colcon build --symlink-install
source install/setup.bash
env | grep -E 'ROS_DOMAIN_ID|ROS_LOCALHOST_ONLY|RMW_IMPLEMENTATION'
ros2 topic list
```

The values must be domain `9`, localhost-only `0`, and
`rmw_cyclonedds_cpp`. Start Play In Editor and check `/clock`:

```bash
ros2 topic echo /clock --once
```

If the domain was edited, update both `Config/DefaultPlugins.ini` and the
Compose environment, then restart editor and container.

## Images do not publish

**Likely cause**

Capture is inactive, Stereo ROS output is disabled, settings were changed
during the current run, or ground-truth warm-up did not complete.

**Fix**

1. Stop Play In Editor.
2. Enable **Stereo ROS Images + CameraInfo** in Simulator Config.
3. Click **Apply Settings** and restart Play In Editor.
4. Start capture:

   ```bash
   bash /ws/scripts/startcapture
   ros2 topic hz /stereo/left/image_raw
   ```

Check the Unreal Output Log for camera or warm-up errors. Image data is
intentionally absent outside active capture.

## Ground-truth files do not appear

**Likely cause**

Ground-truth master/subtype outputs are disabled, capture never started, the
ground-truth camera is not ready, or Play In Editor was stopped before
finalization.

**Fix**

Enable the desired ground-truth outputs and trajectory CSV, apply them before
Play In Editor, start capture, and wait for warm-up. On stop, wait through the
three-second cooldown. Locate the active run with:

```bash
tr -d '\r\n' < Saved/Datasets/current_dataset_run.txt
```

Review the Output Log for `Capture session incomplete` or
`Capture frame incomplete`.

## Ground-truth maps do not appear

**Likely cause**

Ground Truth ROS Maps is disabled, `BP_GroundTruthMapPublisher` is absent, no
Landscape bounds can be resolved, or terrain/obstacle tags are incomplete.

**Fix**

Stop Play In Editor, enable maps, and use **Create/Update Rover + Ground
Truth**. Confirm the intended terrain uses `MapTerrain`, obstacles use
`MapObstacle`, and the rover/dynamic objects use `MapIgnore`, then restart
play. Check the Output Log if the automatic 0.40 m grid would exceed the
10,000,000-cell safety limit.

## Dataset output is not writable

**Likely cause**

The account running Unreal cannot write to the project `Saved/` directory.

**Fix**

From the repository root, diagnose with:

```bash
test -w . && test -w Saved
```

Run Unreal as the user that owns the working copy or correct ownership through
the host's administration policy. A safe universal ownership command cannot be
derived from the repository and is **To be documented** for each deployment.

## ROS GUI or joystick access fails in Docker

**Likely cause**

`DISPLAY`/Xauthority is unavailable, or the host lacks accessible `/dev/dri`
or `/dev/input` devices. Compose mounts all of these explicitly.

**Fix**

From the repository root:

```bash
docker compose -f docker/docker-compose.yml config --quiet
test -n "$DISPLAY"
ls -ld /dev/dri /dev/input
```

Restart the service after correcting the host display or device availability:

```bash
docker compose -f docker/docker-compose.yml down
docker compose -f docker/docker-compose.yml up -d
```

Host X-server authorization and device permission commands are environment
specific and **To be documented**.

## Terrain GUI fails with a display or import error

**Likely cause**

The Tk GUI is being launched without a graphical display, or a Python module is
missing.

**Fix**

Run on a graphical desktop and verify imports:

```bash
python3 -c 'import tkinter, numpy, PIL, matplotlib'
./Tools/Terrain_Generation/moonsim.sh
```

The launcher does not implement `--help` or a headless mode.

## Rock Baker rejects a JSON file

**Likely cause**

The file is not the generator's Unreal import contract or uses incompatible
units/coordinates.

**Fix**

Choose a JSON copied below:

```text
Tools/Terrain_Generation/unreal_import/rockfields/
```

The accepted header is `MoonSimOfflineRockField`, version `1`, units `meters`,
coordinate frame `centered_map_meters`; each rock requires `x_m`, `y_m`, and
`diameter_m`. Bake only while Play In Editor is stopped.
