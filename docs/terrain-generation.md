# Terrain generation

The MoonSim asset generator creates deterministic heightmap and rockfield
packages for later import into Unreal Editor. It is a graphical desktop tool;
there is no headless wrapper around the full workflow.

## Requirements

The launcher imports `tkinter`, NumPy, and Pillow. Matplotlib is required by
the analysis dialogs. These dependencies are not installed by `setup.sh`, and
the repository does not provide a Python requirements file.

Verify an existing environment with:

```bash
python3 -c 'import tkinter, numpy, PIL, matplotlib'
```

Exact Ubuntu package installation commands are **To be documented**.

## Start the GUI

Working directory: repository root on a graphical desktop.

```bash
./Tools/Terrain_Generation/moonsim.sh
```

The launcher has no command-line options; `--help` is not supported. Choose a
preset, edit settings if needed, then generate a heightmap before generating a
rockfield from that heightmap run.

## Regional presets

Presets are implemented in `moonsim.sh`. All values remain editable in the GUI.

| GUI preset | Heightmap / rock profile | Seed | Size | Map / encoded height range | Crater K, 2.5–50 m / 50–250 m | Intended terrain |
| --- | --- | ---: | ---: | --- | --- | --- |
| Mare | `mare_scientific` / `mare` | 25654 | 1009 | 500 m / 80 m fixed | 0.015 / 0.020 | Mare baseline with lower crater abundance |
| Apollo 17 | `apollo17_scientific` / `apollo_17` | 12345 | 1009 | 500 m / 90 m fixed | 0.006 / 0.020 | Apollo 17 regional profile |
| Polar Highlands | `highland_scientific` / `polar_highlands` | 13542 | 1009 | 500 m / 100 m fixed | 0.030 / 0.060 | Crater-rich highland profile |
| New Fresh Zone | `fresh_crater_scientific` / `new_fresh_zone` | 12345 | 1009 | 500 m / 120 m fixed | 0.080 / 0.015 | Fresh-crater and ejecta-focused profile |
| Custom | `custom_scientific` / `custom` | 24680 | 1009 | 500 m / 110 m fixed | 0.060 / 0.100 | Highland-like profile with enabled big-rock clumps |

Heightmap sizes offered by the GUI are 1009, 2017, and 4033 pixels. The map
size and height range are numeric entries rather than a verified fixed range.

## Reproducibility and crater generation

The seed is passed to both the heightmap and rockfield generators. Reusing the
same code, settings, source heightmap package, and seed is the implemented
reproducibility mechanism.

The GUI writes two crater size-frequency segments:

| Heightmap preset | Diameter ranges | Cumulative exponents |
| --- | --- | --- |
| Mare | 2.5–50 m, 50–250 m | 2.00, 2.00 |
| Apollo 17 | 2.5–50 m, 50–250 m | 1.80, 2.00 |
| Highlands | 2.5–50 m, 50–250 m | 2.60, 2.10 |
| Fresh crater | 2.5–50 m, 50–250 m | 3.50, 3.00 |
| Custom | 2.5–50 m, 50–250 m | 2.60, 2.10 |

The editable K values control abundance. Each crater record includes centered
position, diameter, degradation, and morphology data. The heightmap generator
uses the selected preset to produce crater shape, degradation, broad
landforms, and surface roughness.

## Rockfield generation

Select a complete heightmap run folder. The GUI resolves its `heightmap.png`,
`metadata.json`, and `craters.json`, writes the selected settings, and invokes
`rockfield_generator.py`.

Rock generation combines:

- a power-law background population with optional clumping;
- crater freshness and degradation filtering;
- crater-owned interior, rim, proximal, and distal placement zones;
- distance-dependent counts and sizes;
- optional random big-rock clumps;
- random orientation, terrain-aligned placement, and burial;
- material labels for background, floor, old ejecta, and fresh ejecta.

The complete GUI rock schema is:

| Group | Fields |
| --- | --- |
| Limits | `max_rocks`, `min_rock_diameter`, `max_rock_diameter_cap`, `power_law_exponent`, `min_source_crater_diameter_meters`, `max_source_crater_degrade` |
| Background | `background_density_per_m2`, `background_fraction_cap`, `background_clump_fraction`, `background_cluster_sigma_m` |
| Bart–Melosh scaling | `bm_max_diameter_a/b`, `bm_median_diameter_a/b`, `bm_max_distance_a/b`, `bm_median_distance_a/b`, `boulder_distance_scale` |
| Crater abundance | `boulder_diameter_scale`, `crater_boulder_density_scale`, `crater_count_exponent`, `freshness_gamma`, `freshness_floor`, `max_rocks_per_crater` |
| Zone fractions | `interior_fraction`, `rim_fraction`, `proximal_fraction`, `distal_fraction` |
| Zone bounds | `interior_r_min/max`, `rim_r_min/max`, `proximal_r_min/max`, `distal_r_min/max` |
| Distance and size | Per-zone distance powers and size multipliers, plus `distance_size_decay` |
| Clumping | `crater_clump_fraction`, `mean_cluster_size`, `cluster_sigma_m`, `cluster_zone_bias` |
| Random big-rock clumps | Enable, count, mean rocks, sigma, min/max diameter, and crater exclusion diameter/radius |
| Unreal placement | `max_random_tilt_degrees`, `min_burial_fraction`, `max_burial_fraction` |
| Materials | `background_material`, `floor_material`, `old_ejecta_material`, `fresh_ejecta_material` |

`rock_settings.json` is the authoritative per-run record of exact values.
Allowed numeric ranges are **Not yet verified** beyond validation performed by
the generator.

## Minimal heightmap command

The GUI is canonical, but the underlying heightmap CLI is implemented and its
help output has been validated. From the repository root:

```bash
python3 Tools/Terrain_Generation/heightmap_generator.py \
  --out Tools/Terrain_Generation/generated/heightmaps/manual_mare_seed_25654 \
  --preset mare_scientific \
  --seed 25654 \
  --size 1009 \
  --map-size-m 500 \
  --height-range-m 80 \
  --range-mode fixed
```

This writes the four-file heightmap package described below. Use the GUI to
create the matching compact rockfield package and Unreal-import copy.

`rockfield_generator.py` also supports directory inputs and `--out-root` for
batch rockfield generation. A batch wrapper for heightmap profiles is not
present.

## Generated packages

The GUI creates timestamped, slugged run directories:

```text
Tools/Terrain_Generation/
├── generated/
│   ├── heightmaps/<timestamp>_<preset>/
│   │   ├── heightmap.png
│   │   ├── metadata.json
│   │   ├── craters.json
│   │   └── generation_summary.txt
│   └── rockfields/<timestamp>_<profile>/
│       ├── unreal_rockfield.json
│       ├── rock_settings.json
│       └── source_heightmap.json
└── unreal_import/
    ├── heightmaps/<run>.png
    └── rockfields/<run>.json
```

| File | Implemented content |
| --- | --- |
| `heightmap.png` | 16-bit terrain elevation encoding for Unreal Landscape import |
| `metadata.json` | Format/version, units, profile, seed, dimensions, meters per pixel, coordinate descriptions, actual height statistics, generator settings, output filenames, and Unreal X/Y/Z import scales |
| `craters.json` | Terrain-generation and fixed preview-illumination metadata plus centered crater records |
| `generation_summary.txt` | Human-readable settings, statistics, and Unreal scale summary |
| `unreal_rockfield.json` | `MoonSimOfflineRockField` v1 with centered-meter rock instances |
| `rock_settings.json` | Exact rock generator settings selected for the run |
| `source_heightmap.json` | Linkage to the source heightmap package |

The heightmap coordinate description is top-left based, while crater and
rockfield X/Y coordinates are centered about the map origin. See
[Coordinate frames](coordinate-frames.md).

The terrain generator does not produce occupancy, elevation-grid, slope-grid,
crater-zone-image, or rock-density-image files. Occupancy, elevation, and slope
products are generated from the Unreal scene during Play In Editor; see
[Output format](output-format.md).

## Import into Unreal

1. Import the `unreal_import/heightmaps/<run>.png` file as a Landscape
   heightmap.
2. Use the X/Y/Z import scales recorded under `unreal_import` in the matching
   `metadata.json`.
3. Open **Window > Simulator > Simulator Config**.
4. In **Terrain Generation**, select
   `unreal_import/rockfields/<run>.json`.
5. Keep or change the default rock mesh folder `/Game/Meshes/Rocks`, then bake.

The Rock Baker accepts `MoonSimOfflineRockField` version 1, meters, and
`centered_map_meters`. Each rock requires `x_m`, `y_m`, and `diameter_m`; it
uses optional `instance_id`, yaw, tilt, tilt axis, and burial fields. Baking is
disabled during Play In Editor.

The generator's 135-degree azimuth and 25-degree elevation values describe
hillshade-preview metadata only. Set Unreal lighting independently; automatic
illumination transfer is not implemented.
