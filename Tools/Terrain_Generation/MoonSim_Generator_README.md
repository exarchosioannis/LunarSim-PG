# MoonSim Asset Generator README

This guide explains how to generate MoonSim terrain assets, import the heightmap into Unreal Engine, and bake the generated rocks into the level.

The workflow is:

1. Use `generate_moonsim_assets.sh` to generate terrain and rock files.
2. Import the generated heightmap PNG into Unreal as a Landscape.
3. Apply the Moon landscape material.
4. Use the MoonSim Rock Baker in Unreal to place the rocks.

---

## 1. Before you start

Make sure these files are in the same `Tools` folder:

```text
generate_moonsim_assets.sh
heightmap_scientific.py
generate_rockfields_offline_scientific_balanced.py
```

Then run the generator:

```bash
./generate_moonsim_assets.sh
```

A popup window will open.

---

## 2. What the asset generator creates

The generator creates several folders next to `generate_moonsim_assets.sh`.

```text
heightmap_pngs/
crater_jsons/
unreal_rockfields/
generation_files/
```

### `heightmap_pngs/`

This folder contains the heightmap image used to create the Unreal Landscape.

Use this file in Unreal:

```text
*.png
```

### `crater_jsons/`

This folder contains the crater data created by the heightmap generator.

These files are used by the rock generator:

```text
*_rockfield_craters.json
```

### `unreal_rockfields/`

This folder contains the final Unreal-ready rock distribution.

Use this file in the Unreal Rock Baker:

```text
*_unreal_rockfield.json
```

### `generation_files/`

This folder contains supporting files such as metadata, summaries, settings, and logs.

Normally, you do not need to manually use these files in Unreal.

---

## 3. Generate a new heightmap

In the popup:

1. Choose a terrain preset.
2. Adjust the heightmap settings if needed.
3. Click **Generate heightmap**.

After the heightmap finishes, look at the log window.

The log will show the Unreal Landscape import scale values:

```text
X Scale = ...
Y Scale = ...
Z Scale = ...
```

Keep these values open or write them down. You will need them when importing the heightmap into Unreal.

The generated heightmap PNG will be saved in:

```text
heightmap_pngs/
```

The generated crater JSON will be saved in:

```text
crater_jsons/
```

---

## 4. Generate rocks

To generate rocks, the generator needs two files:

```text
*_rockfield_craters.json
<matching metadata>.json
```

If you just generated the heightmap in the same popup session, these fields should already be filled in for you.

Then:

1. Adjust the rock settings.
2. Click **Generate rocks**.
3. Wait for the log to say that rock generation finished.

The final Unreal rock file will be saved in:

```text
unreal_rockfields/
```

The file you will use in Unreal ends with:

```text
_unreal_rockfield.json
```

You can generate multiple rock distributions from the same crater JSON by changing the rock settings and clicking **Generate rocks** again.

---

## 5. Import the heightmap into Unreal Engine

Open your MoonSim project in Unreal Engine.

### Step 1: Delete the default landscape

1. In the level, select the default Landscape.
2. Delete it.

### Step 2: Open Landscape Mode

1. In Unreal, switch to **Landscape Mode**.
2. Go to the **Manage** tab.
3. Choose **New**.
4. Select **Import from File**.

### Step 3: Select the heightmap PNG

Choose the PNG file from:

```text
heightmap_pngs/
```

### Step 4: Enter the scale values

Use the scale values shown in the asset generator log:

```text
X Scale
Y Scale
Z Scale
```

Enter those values into the Landscape import settings.

Then click **Import**.

---

## 6. Apply the Moon landscape material

After the Landscape imports:

1. Select the Landscape.
2. In the Details panel, find the Landscape Material slot.
3. Set the material to:

```text
M_Landscape_Material_Moon_Inst
```

Then set up the paint layer:

1. Go to the Landscape **Paint** options.
2. Choose **Create Layers From Assigned Material**. (Middle icon at the right side of Target Layers > Layers)
3. Find the **SoftSand** layer dropdown, apply the LayerInfo.
4. Right click on Soft Sand and select **Fill Layer**.

The whole landscape should now use the Moon surface material.

---

## 7. Bake rocks into the level

After the Landscape is imported and the material is applied, bake the rocks.

1. Open the Simulator Config window in Unreal.
2. In **Rock Field JSON File**, select the generated file from:

```text
unreal_rockfields/
```

The correct file ends with:

```text
_unreal_rockfield.json
```

3. In the rock mesh folder field, select the folder that contains your rock static meshes.
4. Click **Bake Rocks**.

Unreal will read the rock positions, sizes, yaw, tilt, and burial values from the JSON and place the rocks on the Landscape.

---

## 8. Clear and rebake rocks

If you want to remove the baked rocks:

1. Open the MoonSim Rock Baker window.
2. Click **Clear Rocks**.

Then you can select another `_unreal_rockfield.json` file and click **Bake Rocks** again.

---

To change the preset lighting conditions, select the directionalLight and chanfe the rotation corrdinates. when pressing play, the bp_sunglowcontroller will move the sphere and shine accondinlgly

## 9. Checklist

```text
[ ] Run ./generate_moonsim_assets.sh
[ ] Generate heightmap
[ ] Write down X Scale, Y Scale, and Z Scale from the log
[ ] Generate rocks
[ ] Open Unreal Engine
[ ] Delete default Landscape
[ ] Landscape Mode > Manage > New > Import from File
[ ] Select PNG from heightmap_pngs/
[ ] Enter X/Y/Z Scale values
[ ] Import
[ ] Assign M_Landscape_Material_Moon_Inst
[ ] Paint options > Create Layers From Assigned Material
[ ] SoftSand dropdown > Fill Layer
[ ] Open MoonSim Rock Baker
[ ] Select *_unreal_rockfield.json from unreal_rockfields/
[ ] Select rock mesh folder
[ ] Click Bake Rocks
```

