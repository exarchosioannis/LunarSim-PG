# Terrain generator workflow

This guide covers the complete graphical workflow for generating MoonSim terrain assets, importing the heightmap into Unreal Engine, applying the Moon landscape material, and baking the generated rocks.

The workflow is:

1. Use `moonsim.sh` to generate terrain and rock files.
2. Import the generated heightmap PNG into Unreal as a Landscape.
3. Apply the Moon landscape material.
4. Use the MoonSim Rock Baker in Unreal to place the rocks.

---

## 1. Start the generator

From the repository root on a graphical desktop:

```bash
./Tools/Terrain_Generation/moonsim.sh
```

Check the launcher and Python dependencies without opening the GUI:

```bash
./Tools/Terrain_Generation/moonsim.sh --help
./Tools/Terrain_Generation/moonsim.sh --check-deps
```

The terrain tool directory contains these main scripts:

```text
Tools/Terrain_Generation/
├── moonsim.sh
├── heightmap_generator.py
├── rockfield_generator.py
├── heightmap_analysis.py
└── rockfield_analysis.py
```


## 2. Output layout

The GUI keeps complete run packages separate from the files copied for Unreal import:

```text
Tools/Terrain_Generation/
├── generated/
│   ├── heightmaps/<run>/
│   │   ├── heightmap.png
│   │   ├── metadata.json
│   │   ├── craters.json
│   │   └── generation_summary.txt
│   └── rockfields/<run>/
│       ├── unreal_rockfield.json
│       ├── rock_settings.json
│       └── source_heightmap.json
├── unreal_import/
│   ├── heightmaps/<run>.png
│   └── rockfields/<run>.json
└── analysis_results/
    ├── heightmaps/
    └── rockfields/
```

Use the files under `unreal_import/` for Unreal. Keep the corresponding `generated/` run folders as the reproducibility and metadata record.


## 3. Generate a heightmap

In the GUI:

1. Choose a terrain preset.
2. Adjust the heightmap settings if needed.
3. Click **Generate heightmap**.

When generation finishes, the GUI selects the new run for rock generation and prints the package paths in the log. The Unreal Landscape import scales are in the run's `metadata.json` under:

```text
unreal_import.x_scale_cm
unreal_import.y_scale_cm
unreal_import.z_scale
```


## 4. Generate rocks

Rock generation requires a complete heightmap run containing:

```text
heightmap.png
metadata.json
craters.json
```

A heightmap generated in the current GUI session is selected automatically.
To use an older run, select its folder in the existing-heightmap field.

Then:

1. Choose or edit the rock profile and settings.
2. Click **Generate rocks**.
3. Wait for the log to report successful completion.

The Unreal-ready rockfield copy is:

```text
Tools/Terrain_Generation/unreal_import/rockfields/<run>.json
```

The complete run, including the exact settings used, is stored under `generated/rockfields/<run>/`. You can generate multiple rockfields from the same heightmap run by changing the rock settings.

## 5. Import the Landscape into Unreal Engine

Open the target MoonSim level in Unreal Editor.

### Remove the existing Landscape or create a new level

When replacing the level's current terrain:

1. Select the existing Landscape actor.
2. Delete it.

Do not delete unrelated level actors.

### Import the generated heightmap

1. Switch to **Landscape Mode**.
2. Open **Manage**.
3. Choose **New** and then **Import from File**.
4. Select `unreal_import/heightmaps/<run>.png`.
5. Enter the X, Y, and Z scales from the matching heightmap `metadata.json`.
6. Select the material `M_Landscape_Material_Moon_Inst`.
7. Click **Import**.

The generated PNG is a 16-bit Landscape heightmap.

## 6. Apply the Moon landscape material

After the Landscape imports:

1. Select the Landscape.
2. In the Details panel, assign the Landscape Material if it was not done on the previous step:

```text
M_Landscape_Material_Moon_Inst
```

3. Open the Landscape **Paint** tools.
4. Choose **Create Layers From Assigned Material**.
5. For **SoftSand**, assign the appropriate LayerInfo asset.
6. Right-click **SoftSand** and choose **Fill Layer**.


## 7. Bake rocks into the Unreal Engine level

Rock baking must be done while the editor is not in Play In Editor.

1. Open **Window > Simulator > Simulator Config**.
2. In **Terrain Generation**, select the generated rockfield JSON:

```text
Tools/Terrain_Generation/unreal_import/rockfields/<run>.json
```

3. Select the folder containing the rock static meshes. The default is:

```text
/Game/Meshes/Rocks
```

4. Click **Bake Rocks**.

The Rock Baker reads the centred X/Y positions, diameters, and optional yaw,
tilt, tilt-axis, and burial values from the JSON. Unreal resolves the final
terrain height and places the mesh instances on the imported Landscape.

## 8. Update the Sun, Sky, Earth and Rover in the Unreal Engine level if needed

Use the **Setup Actions** buttons in Simulator Config to create or update the Sun, Sky, Earth, and Rover. Use the separate **Sun Direction** controls when you need to change the lighting direction.

## 9. Clear or rebake rocks into the Unreal Engine level

To replace a baked rockfield:

1. Open **Window > Simulator > Simulator Config**.
2. Use **Clear Baked Rocks**.
3. Select another rockfield JSON or mesh folder.
4. Click **Bake Rocks** again.

## 10. Analyze generated terrain

The GUI's **Analyze heightmap** dialog creates elevation, hillshade, slope, and roughness visualisations plus metrics and summaries. **Analyze rockfield** creates crater/ejecta-zone, source-crater, density, and large-rock visualisations plus metrics and CSV files.

These files are written under `analysis_results/`. They are offline analysis products and are not substitutes for the dataset occupancy, elevation, or slope modalities captured from the Unreal scene during Play In Editor.

## 11. Lighting

The azimuth and elevation stored by the terrain generator are used only for offline hillshade previews. To change Unreal lighting, open **Window > Simulator > Simulator Config**, set **Sun Elevation (deg)** and **Sun Azimuth (deg)**, and press **Apply Sun Direction**. These preview values are not transferred to the Unreal level automatically.
