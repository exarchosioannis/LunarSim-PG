# LunarSim-PG setup

## Prerequisites

- Ubuntu 24.04
- Unreal Engine 5.7.x already installed
- Git
- Git LFS
- `curl`
- `jq`
- Docker

## Clone

```bash
git lfs install
git clone https://github.com/exarchosgiannis/simulator57.git
cd simulator57
```

## Set Unreal Engine path

Set `UE_ROOT` to the absolute path of the Unreal Engine installation:

```bash
export UE_ROOT=/absolute/path/to/UnrealEngine-5.7.4
```

## Prepare, build and open

```bash
./setup.sh
./build.sh
./open_project.sh
```

- `setup.sh` pulls Git LFS files and prepares TempoROS.
- `build.sh` compiles the Unreal Editor project.
- `open_project.sh` opens `LunarSimPG.uproject`.
