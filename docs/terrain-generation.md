# Terrain generation

The MoonSim asset generator creates deterministic heightmap and rockfield
packages for later import into Unreal Editor trhough a graphical desktop tool.

## Requirements
Terrain generation: Python 3 with `tkinter`, `NumPy`, `Pillow`.

Terrain analysis: Python 3 with `Matplotlib` and `SciPy`. 

`setup.sh` checks these imports and installs missing packages withapt

## Start the GUI

Working directory: repository root on a graphical desktop.

```bash
./Tools/Terrain_Generation/moonsim.sh
```

Useful launcher options are:

./Tools/Terrain_Generation/moonsim.sh `--help`

./Tools/Terrain_Generation/moonsim.sh `--check-deps`

Choose a preset, edit settings if needed, generate a heightmap, and then generate a rockfield from that heightmap run.

For a complete walkthrough of the GUI, generated files, and Unreal Engine
import process, see [Terrain generator workflow](terrain-generator-workflow.md).

## Regional presets

Presets are implemented in `moonsim.sh`. All values remain editable in the GUI.

| GUI preset | Heightmap / rock profile | Seed | Size | Map / encoded height range | Crater K, 2.5–50 m / 50–250 m | Intended terrain |
| --- | --- | ---: | ---: | --- | --- | --- |
| Mare | `mare_scientific` / `mare` | 25654 | 1009 | 500 m / 80 m fixed | 0.015 / 0.020 | Mare baseline with lower crater abundance |
| Apollo 17 | `apollo17_scientific` / `apollo_17` | 12345 | 1009 | 500 m / 90 m fixed | 0.006 / 0.020 | Apollo 17 regional profile |
| Polar Highlands | `highland_scientific` / `polar_highlands` | 13542 | 1009 | 500 m / 100 m fixed | 0.030 / 0.060 | Crater-rich highland profile |
| New Fresh Zone | `fresh_crater_scientific` / `new_fresh_zone` | 12345 | 1009 | 500 m / 120 m fixed | 0.080 / 0.015 | Fresh-crater and ejecta-focused profile |
| Custom | `custom_scientific` / `custom` | 24680 | 1009 | 500 m / 110 m fixed | 0.060 / 0.100 | Highland-like profile with enabled big-rock clumps |

Heightmap sizes offered by the GUI are 1009, 2017, and 4033 pixels. 

The presets are entry points into a scientifically informed procedural model. For the crater population, degradation, landform, crater-shape, and roughness equations—and the distinction between literature-constrained and procedural parameters—see [Terrain generation model](terrain-generation-model.md).


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

For more information on the heighmap generation model check: xxxxxxxxxxxxxx

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

Rock positions are generated with a crater-first statistical model combining empirical boulder scaling, crater freshness, radial ejecta zones, power-law sizes, and procedural background and clumping terms. See [Rock distribution model](rock-distribution-model.md) for the equations, scientific sources, GUI preset values, and limitations.


## Advanced command-line use

The GUI is the canonical workflow. The underlying Python scripts expose theirown `--help` output for testing, automation, and advanced use:

python3 Tools/Terrain_Generation/heightmap_generator.py `--help`

python3 Tools/Terrain_Generation/rockfield_generator.py `--help`

python3 Tools/Terrain_Generation/heightmap_analysis.py `--help`

python3 Tools/Terrain_Generation/rockfield_analysis.py `--help`

These commands do not replace all GUI packaging and Unreal-copy steps.

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
| `unreal_rockfield.json` | Unreal-ready rock placement data for the generated terrain |
| `rock_settings.json` | Exact rock generator settings selected for the run |
| `source_heightmap.json` | Linkage to the source heightmap package |

The heightmap coordinate description is top-left based, while crater and
rockfield X/Y coordinates are centered about the map origin. See
[Coordinate frames](coordinate-frames.md).

The analysis dialogs can create elevation and slope visualization PNGs from a heightmap, plus crater/ejecta-zone and rock-density visualization PNGs from a rockfield. They also write metrics, summaries, and CSV files under analysis_results/.

These are offline analysis products, not the simulator dataset modalities generated from the Unreal scene during Play In Editor and defined in [Output format](output-format.md)

## Import into Unreal

1. Open the target level and remove the existing Landscape if it is being replaced.
2. In Landscape Mode > Manage > New, choose Import from File and select `unreal_import/heightmaps/<run>.png.`
3. Enter the X/Y/Z scales from the matching `metadata.json` under `unreal_import`, then import the Landscape.
4. Assign `M_Landscape_Material_Moon_Inst`. In Landscape Paint mode, create the layers from the assigned material, assign the LayerInfo for SoftSand, and fill that layer.
5. Open Window > Simulator > Simulator Config. In Terrain Generation,select `unreal_import/rockfields/<run>.json`, choose the rock mesh folder (default `/Game/Meshes/Rocks`), and bake the rocks.


The generator's 135-degree azimuth and 25-degree elevation values describe hillshade-preview metadata only. Set Unreal lighting independently; automatic illumination transfer is not implemented.
