# LunarSim-PG documentation

Use this page to find the canonical guide for each task. The root
[README](../README.md) remains a short project landing page.

## Getting started

- [Installation](installation.md) — prepare Ubuntu, Unreal Engine, Git LFS, and
  the ROS 2 Docker environment.
- [Quickstart](quickstart.md) — build, launch, verify ROS 2, view an image, and
  control the rover.
- [Troubleshooting](troubleshooting.md) — diagnose setup, build, rendering,
  ROS 2, and capture failures.

## Using the simulator

- [Unreal simulator](unreal-simulator.md) — levels, editor tools, rover setup,
  Play In Editor, controls, logs, and capture triggers.
- [Configuration](configuration.md) — simulator output, camera, IMU, rover,
  ROS, terrain, and output-location settings.
- [Coordinate frames](coordinate-frames.md) — axes, units, transforms,
  quaternions, camera frames, and exported poses.

## ROS 2 integration

- [ROS 2 interface](ros2-interface.md) — every implemented topic, QoS profile,
  TF frame, control command, inspection command, and rosbag example.

## Terrain generation

- [Terrain generation](terrain-generation.md) — regional presets, seeds,
  crater and rock generation, output packages, and Unreal import.
  - [Terrain generator workflow](terrain-generator-workflow.md) — step-by-step
  use of the GUI, generated files, Landscape import, material setup, and rock
  baking in Unreal Engine.
  - [Terrain generation model](terrain-generation-model.md) — procedural
  heightfield model, crater population and morphology equations, preset
  assumptions, scientific sources, and limitations.
  - [Rock distribution model](rock-distribution-model.md) — crater-relative rock
  placement, boulder size and distance scaling, radial zones, background
  populations, scientific sources, and procedural assumptions.

## Dataset generation

- [Dataset workflow](dataset-generation.md) — configure, capture, drive,
  stop, locate, inspect, and post-process a run.
- [Output format](output-format.md) — dataset directory layout, modality
  encodings, CSV and JSON schemas, maps, and synchronization limits.

## Architecture and development

- [Architecture](architecture.md) — implemented data flow from terrain tools
  through Unreal, ROS 2, and offline capture.
- [Repository structure](repository-structure.md) — responsibilities of the
  important source, content, plugin, tool, container, and workspace paths.
- [Development](development.md) — runtime and editor extension points, builds,
  tests, and synchronization checks.

