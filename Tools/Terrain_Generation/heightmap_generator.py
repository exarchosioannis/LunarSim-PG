#!/usr/bin/env python3
"""
LunarSim-PG terrain-aware lunar heightmap generator.

The GUI-facing script works in metres, generates a lunar floor and crater
population, integrates craters against local reference surfaces, and exports a
portable 16-bit heightmap package for Unreal Engine and downstream analysis.

Scientific sources used for preset comments and crater laws:
- Mahanti et al. (2018), small lunar crater d/D morphology classes.
- Minton et al. (2019), equilibrium size-frequency distributions.
- Bugiolacchi & Wöhler (2020), Apollo 17 small-crater populations.
- Plescia & Robinson (2019), Giordano Bruno self-secondaries.
- Williams et al. (2022), Giordano Bruno terrain properties.
- Oetting et al. (2023), Copernican crater size-frequency distributions.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import logging
import math
import platform
import re
import subprocess
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from importlib import metadata as importlib_metadata
from pathlib import Path
from typing import Dict, List, Optional

import numpy as np
from PIL import Image, ImageFilter


PROJECT_NAME = "LunarSim-PG"
SCRIPT_NAME = "heightmap_generator"
SCRIPT_VERSION = "2.1.0"


def utc_now() -> datetime:
    return datetime.now(timezone.utc)


def utc_file_timestamp(value: datetime) -> str:
    return value.strftime("%Y%m%d_%H%M%S")


def safe_name(value: str) -> str:
    cleaned = re.sub(r"[^0-9A-Za-z_.-]+", "_", str(value)).strip("_")
    return cleaned or "terrain"


def package_version(name: str) -> str | None:
    try:
        return importlib_metadata.version(name)
    except importlib_metadata.PackageNotFoundError:
        return None


def git_commit() -> str | None:
    try:
        completed = subprocess.run(
            ["git", "-C", str(Path(__file__).resolve().parent), "rev-parse", "HEAD"],
            check=True, capture_output=True, text=True, timeout=3,
        )
    except (FileNotFoundError, subprocess.SubprocessError):
        return None
    value = completed.stdout.strip()
    return value or None


def build_provenance(generated_at: datetime) -> Dict[str, object]:
    return {
        "project": PROJECT_NAME,
        "generator": SCRIPT_NAME,
        "generator_version": SCRIPT_VERSION,
        "generated_utc": generated_at.isoformat().replace("+00:00", "Z"),
        "git_commit": git_commit(),
        "runtime": {
            "python": platform.python_version(),
            "numpy": np.__version__,
            "Pillow": package_version("Pillow"),
        },
    }


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def temporary_path(path: Path) -> Path:
    return path.with_name(f".{path.stem}.tmp{path.suffix}")


def atomic_write_json(path: Path, payload: object) -> None:
    temp = temporary_path(path)
    try:
        with temp.open("w", encoding="utf-8") as file:
            json.dump(payload, file, indent=2, allow_nan=False)
            file.write("\n")
        temp.replace(path)
    finally:
        temp.unlink(missing_ok=True)


def atomic_write_text(path: Path, text: str) -> None:
    temp = temporary_path(path)
    try:
        temp.write_text(text, encoding="utf-8")
        temp.replace(path)
    finally:
        temp.unlink(missing_ok=True)


def atomic_save_png16(path: Path, array: np.ndarray) -> None:
    temp = temporary_path(path)
    try:
        Image.fromarray(array).save(temp, format="PNG")
        temp.replace(path)
    finally:
        temp.unlink(missing_ok=True)


# -----------------------------
# Configuration
# -----------------------------

@dataclass(frozen=True)
class CraterSegment:
    min_diameter_m: float       # Minimum crater diameter; used as Dmin in count = A*K*(Dmin^-b - Dmax^-b).
    max_diameter_m: float       # Maximum crater diameter; used as Dmax in count = A*K*(Dmin^-b - Dmax^-b).
    K: float                    # Crater density coefficient; higher K creates more craters.
    b: float                    # Cumulative power-law exponent; higher b means proportionally more small craters.


@dataclass
class GeneratorSettings:
    preset: str = "mare_smooth" # Preset name, e.g. mare_smooth, highland_old, or fresh_crater_field.
    seed: int = 99999           # Random seed; same seed and settings produce the same terrain.
    size: int = 1009            # Heightmap resolution; meters_per_pixel = map_size_m / (size - 1).
    map_size_m: float = 500.0   # Real-world square map size in meters.
    height_range_m: float = 80.0# Fixed export range; Z Scale = height_range_m * 100 / 512.

    # Base floor.
    base_relief_m: float = 3.0  # Smooth floor relief in meters; height += normalized_floor * base_relief_m.
    floor_final_blur_px: float = 4.0 # Final blur on base floor before craters; higher means smoother.
    add_broad_landforms: bool = True # Adds large smooth hills/basins before craters.
    landform_count: int = 18    # Number of broad hills/basins; these are not craters.
    landform_radius_min_m: float = 25.0 # Minimum broad landform radius in meters.
    landform_radius_max_m: float = 140.0# Maximum broad landform radius in meters.
    landform_height_min_m: float = -1.2 # Minimum landform height in meters; negative creates basins.
    landform_height_max_m: float = 1.8  # Maximum landform height in meters; positive creates rises.
    landform_elongation_max: float = 2.5# Max landform stretching; rx = radius*elong, ry = radius/sqrt(elong).

    # Crater population.
    crater_segments: Optional[List[CraterSegment]] = None # CSFD segments; count ~ Poisson(A*K*(Dmin^-b - Dmax^-b)).
    degradation_min: float = 0.25 # Minimum crater age/softening; 0 fresh, 1 old.
    degradation_max: float = 0.95 # Maximum crater age/softening; higher makes craters smoother/subdued.
    allow_overlaps: bool = True # Allows independent crater placement and natural overlaps.

    # Crater geometry.
    simple_depth_ratio: float = 0.115 # Legacy fallback; depth = simple_depth_ratio * D if laws are disabled.
    rim_height_ratio: float = 0.035 # Legacy fallback; rim_height = rim_height_ratio * D if laws are disabled.
    rim_width_radius_ratio: float = 0.10 # Rim Gaussian width relative to radius; higher makes wider rims.
    ejecta_height_ratio: float = 0.010 # Legacy fallback; ejecta_height = ejecta_height_ratio * D if laws are disabled.
    ejecta_decay_exponent: float = 3.0 # Ejecta fade law; ejecta ∝ r^-exponent, higher fades faster.
    crater_outer_radius_ratio: float = 2.65 # Crater/ejecta influence radius measured in crater radii.
    crater_edge_falloff_m: float = 8.0 # Extra blend distance in meters to fade crater into terrain.
    max_rim_irregularity: float = 0.020 # Max rim distortion; higher makes rims less circular.
    rim_strength: float = 1.0 # Preset multiplier for rim height; old mare lower, fresh crater higher.
    ejecta_strength: float = 1.0 # Preset multiplier for ejecta height; fresh ejecta higher, old terrain lower.
    fresh_blockiness: float = 0.0 # Adds boulder-like positive bumps around young craters; 0 disables.
    secondary_chain_probability: float = 0.0 # Chance that larger craters seed small secondary chains.
    slope_crater_loss_strength: float = 0.0 # Removes small craters on steep terrain; useful for highlands/massifs.

    # Terrain-aware integration.
    local_reference_radius_ratio: float = 0.45 # Local reference blur; sigma_px = crater_radius_px * this.
    min_local_reference_blur_px: float = 2.0 # Minimum blur for crater local reference surface.
    max_local_reference_blur_px: float = 80.0 # Maximum blur for crater local reference surface.
    max_degraded_crater_blur_px: float = 3.5 # Max local blur for old/degraded craters.

    # Post-crater surface texture.
    post_regolith_roughness_m: float = 0.035 # Small final roughness; height += normalized_noise * this.
    post_regolith_blur_px: float = 0.65 # Blur on final roughness noise; higher makes smoother texture.
    final_global_blur_px: float = 0.0 # Optional blur over entire final terrain; usually keep at 0.

    # Export / preview metadata.
    dither_lsb: float = 0.35 # Tiny 16-bit export dither to reduce banding.
    sun_azimuth_deg: float = 135.0 # Hillshade preview sun direction; does not affect heightmap.
    sun_elevation_deg: float = 25.0 # Hillshade preview sun elevation; does not affect heightmap.

    # Post-crater surface texture.
    crater_floor_roughness_m: float = 0.0
    crater_floor_roughness_blur_px: float = 1.4
    crater_floor_roughness_min_diameter_m: float = 35.0


PRESETS: Dict[str, Dict] = {
    # ------------------------------------------------------------------
    # Scientific presets
    # ------------------------------------------------------------------
    # Notes on source comments:
    # - d/D values come mainly from Mahanti et al. (2018), Icarus 299.
    # - Mare equilibrium crater SFD comes from Minton et al. (2019), Icarus 326.
    # - Apollo 17 crater SFD α/β values come from Bugiolacchi & Wöhler (2020), Icarus 350.
    # - Fresh crater/ejecta values come mainly from Plescia & Robinson (2019),
    #   Williams et al. (2022), and Oetting et al. (2023).
    # - Values marked "Procedural" are not direct measurements; they are terrain-art
    #   controls constrained by the measured crater/regolith behavior in those papers.

    "default": {
        # Alias to the scientifically grounded mare preset.
        "base_relief_m": 2.2,              # Procedural; smooth old mare floor; constrained by Minton et al. 2019 mare-equilibrium context
        "floor_final_blur_px": 6.0,        # Procedural; mare smoothing
        "landform_count": 18,              # Procedural; mare has fewer broad hills/basins than highlands
        "landform_radius_min_m": 35.0,     # Procedural
        "landform_radius_max_m": 180.0,    # Procedural
        "landform_height_min_m": -2.0,     # Procedural
        "landform_height_max_m": 2.5,      # Procedural
        "landform_elongation_max": 2.2,    # Procedural; low mare ridge elongation
        "degradation_min": 0.75,           # Mahanti et al. 2018: most small lunar craters are degraded, d/D < 0.1
        "degradation_max": 1.00,           # Mahanti et al. 2018: class C mean d/D ≈ 0.06
        "simple_depth_ratio": 0.060,       # Mahanti et al. 2018: class C mean d/D = 0.06
        "rim_height_ratio": 0.007,         # Procedural; subdued old-rim scaling
        "rim_strength": 0.45,              # Procedural; old mare rims are subdued
        "ejecta_height_ratio": 0.0015,     # Procedural; old mare ejecta mostly erased
        "ejecta_strength": 0.20,           # Procedural; old mare ejecta blankets weak
        "fresh_blockiness": 0.03,          # Procedural; mature mare has little visible blockiness
        "secondary_chain_probability": 0.01,# Procedural; low obvious secondary chains in this generic mare preset
        "slope_crater_loss_strength": 0.10,# Procedural; mare is relatively flat, low slope filtering
        "max_rim_irregularity": 0.04,      # Procedural; old rims smoothed by degradation
        "post_regolith_roughness_m": 0.012,# Procedural; low roughness for mature mare regolith
        "crater_segments": [
            CraterSegment(2.5, 50.0, 0.015, 2.00),   # Minton et al. 2019: equilibrium β≈2; K within 0.0106–0.060 converted to diameter form
            CraterSegment(50.0, 250.0, 0.020, 2.00), # Minton et al. 2019 / procedural extension; larger craters kept sparse
        ],
    },

    "mare_scientific": {
        # Smooth old mare: low relief and equilibrium-ish small craters.
        "base_relief_m": 2.2,              # Procedural; smooth old mare floor; constrained by Minton et al. 2019 mare-equilibrium context
        "floor_final_blur_px": 6.0,        # Procedural; mare smoothing
        "landform_count": 18,              # Procedural; mare has fewer broad hills/basins than highlands
        "landform_radius_min_m": 35.0,     # Procedural
        "landform_radius_max_m": 180.0,    # Procedural
        "landform_height_min_m": -2.0,     # Procedural
        "landform_height_max_m": 2.5,      # Procedural
        "landform_elongation_max": 2.2,    # Procedural; low mare ridge elongation
        "degradation_min": 0.75,           # Mahanti et al. 2018: most small lunar craters are degraded, d/D < 0.1
        "degradation_max": 1.00,           # Mahanti et al. 2018: class C mean d/D ≈ 0.06 
        "simple_depth_ratio": 0.060,       # Mahanti et al. 2018: class C mean d/D = 0.06
        "rim_height_ratio": 0.007,         # Procedural; subdued old-rim scaling
        "rim_strength": 0.45,              # Procedural; old mare rims are subdued
        "ejecta_height_ratio": 0.0015,     # Procedural; old mare ejecta mostly erased
        "ejecta_strength": 0.20,           # Procedural; old mare ejecta blankets weak
        "fresh_blockiness": 0.03,          # Procedural; mature mare has little visible blockiness
        "secondary_chain_probability": 0.01,# Procedural; low obvious secondary chains in this generic mare preset
        "slope_crater_loss_strength": 0.10,# Procedural; mare is relatively flat, low slope filtering
        "max_rim_irregularity": 0.04,      # Procedural; old rims smoothed by degradation
        "post_regolith_roughness_m": 0.012,# Procedural; low roughness for mature mare regolith
        "crater_segments": [
            CraterSegment(2.5, 50.0, 0.015, 2.00),   # Minton et al. 2019: equilibrium β≈2; K within 0.0106–0.060 converted to diameter form
            CraterSegment(50.0, 250.0, 0.020, 2.00), # Minton et al. 2019 / procedural extension; larger craters kept sparse
        ],
    },

    "highland_scientific": {
        # Old highland: higher broad relief, more roughness, degraded but less smooth than mare.
        "base_relief_m": 6.0,              # Procedural; highland relief higher than mare; consistent with highland/massif terrain context
        "floor_final_blur_px": 4.0,        # Procedural; highlands less smooth than mare
        "landform_count": 42,              # Procedural; highlands need more broad hills/basins
        "landform_radius_min_m": 18.0,     # Procedural
        "landform_radius_max_m": 220.0,    # Procedural
        "landform_height_min_m": -5.0,     # Procedural; basin-like broad landforms
        "landform_height_max_m": 8.0,      # Procedural; highland massifs/rises
        "landform_elongation_max": 4.0,    # Procedural; ridges/massifs more elongated
        "degradation_min": 0.65,           # Mahanti et al. 2018: highland/massif SLCs include degraded B/C populations
        "degradation_max": 1.00,           # Mahanti et al. 2018: class C mean d/D ≈ 0.06
        "simple_depth_ratio": 0.075,       # Mahanti et al. 2018: between BC mean d/D=0.08 and C mean d/D=0.06
        "rim_height_ratio": 0.012,         # Procedural; rougher old terrain keeps stronger local rims than mare
        "rim_strength": 0.70,              # Procedural; highland rims stronger than mare
        "ejecta_height_ratio": 0.0025,     # Procedural; old ejecta subdued but rougher than mare
        "ejecta_strength": 0.35,           # Procedural; highland ejecta still weak, but not as erased as mare
        "fresh_blockiness": 0.15,          # Procedural; highlands/fresh small impacts can show more blockiness
        "secondary_chain_probability": 0.05,# Procedural; old highlands can contain secondary-like clusters
        "slope_crater_loss_strength": 0.55,# Mahanti et al. 2018 / Mazarico et al. 2024: steep slopes retain fewer small craters
        "max_rim_irregularity": 0.1,      # Procedural; highland rims more distorted/rough
        "post_regolith_roughness_m": 0.035,# Procedural; rougher mature highland surface
        "crater_segments": [
            CraterSegment(2.5, 50.0, 0.030, 2.60),   # Bugiolacchi & Wöhler 2020: upland-like β≈2.4–2.9; K≈0.019–0.045
            CraterSegment(50.0, 250.0, 0.060, 2.10), # Procedural extension for larger visible craters
        ],
    },

    "apollo17_scientific": {
        # Taurus-Littrow valley: mare/highland mix, Tycho-secondary affected, many degraded small craters.
        "base_relief_m": 4.0,              # Procedural; between mare and highland for Taurus-Littrow valley
        "floor_final_blur_px": 4.5,        # Procedural
        "landform_count": 32,              # Procedural; valley plus massif-adjacent terrain
        "landform_radius_min_m": 20.0,     # Procedural
        "landform_radius_max_m": 180.0,    # Procedural
        "landform_height_min_m": -4.0,     # Procedural
        "landform_height_max_m": 6.0,      # Procedural
        "landform_elongation_max": 3.5,    # Procedural; Taurus-Littrow valley/ridge structure
        "degradation_min": 0.75,           # Mahanti et al. 2018: Apollo 17 TL-plains strongly degraded; many class C craters
        "degradation_max": 1.00,           # Mahanti et al. 2018: class C d/D <=0.07, mean ≈0.06
        "simple_depth_ratio": 0.060,       # Mahanti et al. 2018: class C mean d/D = 0.06
        "rim_height_ratio": 0.008,         # Procedural; degraded TL rims
        "rim_strength": 0.55,              # Procedural; TL rims stronger than mare, weaker than highlands
        "ejecta_height_ratio": 0.002,      # Procedural; secondary-reworked surface, weak individual ejecta
        "ejecta_strength": 0.30,           # Procedural; weak ejecta, but not zero
        "fresh_blockiness": 0.20,          # Procedural; Apollo 17 contains boulder/secondary-rich zones
        "secondary_chain_probability": 0.15,# Bugiolacchi & Wöhler 2020: Tycho surge/secondary modification important at Apollo 17
        "slope_crater_loss_strength": 0.35,# Mahanti et al. 2018: slopes/massifs reduce small-crater retention
        "max_rim_irregularity": 0.08,      # Procedural; mixed mare/upland and secondary-disturbed terrain
        "post_regolith_roughness_m": 0.025,# Procedural; rougher than mare, smoother than highlands
        "crater_segments": [
            CraterSegment(2.5, 50.0, 0.006, 1.80),   # Bugiolacchi & Wöhler 2020: mare-like Apollo 17 units K≈0.003–0.0107, β≈1.5–2.1
            CraterSegment(50.0, 250.0, 0.020, 2.00), # Procedural extension; larger crater background kept sparse
        ],
    },

    "fresh_crater_scientific": {
        # Fresh crater / young ejecta: strong rims, visible ejecta, blocky roughness, patchy crater density.
        "base_relief_m": 5.0,              # Procedural; fresh ejecta and crater-wall terrain rougher than mare
        "floor_final_blur_px": 2.5,        # Procedural; less smoothing preserves fresh roughness
        "landform_count": 22,              # Procedural; hummocky/lobate ejecta-like broad relief
        "landform_radius_min_m": 12.0,     # Procedural
        "landform_radius_max_m": 140.0,    # Procedural
        "landform_height_min_m": -8.0,     # Procedural; local hollows/slumps
        "landform_height_max_m": 12.0,     # Procedural; hummocky ejecta/blocky rises
        "landform_elongation_max": 4.0,    # Plescia & Robinson 2019: lineated/lobate ejecta facies; value procedural
        "degradation_min": 0.00,           # Mahanti et al. 2018 class A / fresh-crater endmember
        "degradation_max": 0.45,           # Keep fresh-to-moderate, not old mare-like
        "simple_depth_ratio": 0.145,       # Mahanti et al. 2018: class A mean d/D≈0.15; fresh SLC range ≈0.12–0.17
        "rim_height_ratio": 0.025,         # Procedural; stronger fresh rims
        "rim_strength": 1.20,              # Procedural; fresh rims emphasized
        "ejecta_height_ratio": 0.007,      # Procedural; visible fresh ejecta blanket
        "ejecta_strength": 1.50,           # Plescia & Robinson 2019: fresh ejecta/melt facies are visually distinct; scale procedural
        "fresh_blockiness": 0.80,          # Plescia & Robinson 2019 / Williams et al. 2022: fresh ejecta can be blocky/thermally heterogeneous
        "secondary_chain_probability": 0.25,# Plescia & Robinson 2019: Giordano Bruno self-secondaries are important
        "slope_crater_loss_strength": 0.20,# Fresh ejecta topography can obscure craters; modest generic slope loss
        "max_rim_irregularity": 0.035,     # Procedural; fresh rims visible but not extremely warped
        "post_regolith_roughness_m": 0.070,# Procedural; rough/blocky fresh ejecta
        "crater_segments": [
            CraterSegment(2.5, 50.0, 0.080, 3.50),   # Oetting et al. 2023 + Plescia & Robinson 2019: fresh ejecta slopes ~3–3.5; GB N(10) ~20–180+
            CraterSegment(50.0, 250.0, 0.015, 3.00), # Neukum small-crater slope near 3; sparse larger superposed craters
        ],
    },

    "fresh_impact_melt": {
        # Smooth fresh impact melt: low N(10), low roughness, weak boulder field.
        "base_relief_m": 2.8,              # Procedural; melt ponds smoother than clastic ejecta
        "floor_final_blur_px": 3.5,        # Procedural; smoother melt
        "landform_count": 10,              # Procedural
        "landform_radius_min_m": 20.0,     # Procedural
        "landform_radius_max_m": 110.0,    # Procedural
        "landform_height_min_m": -2.0,     # Procedural
        "landform_height_max_m": 3.5,      # Procedural
        "landform_elongation_max": 2.0,    # Procedural
        "degradation_min": 0.00,           # Fresh surface
        "degradation_max": 0.35,           # Fresh-to-lightly degraded
        "simple_depth_ratio": 0.135,       # Mahanti et al. 2018: fresh class A/AB range
        "rim_height_ratio": 0.020,         # Procedural
        "rim_strength": 1.00,              # Procedural
        "ejecta_height_ratio": 0.003,      # Procedural; melt has weaker clastic ejecta texture
        "ejecta_strength": 0.50,           # Plescia & Robinson 2019: melt surfaces have fewer/smoother craters than ejecta
        "fresh_blockiness": 0.20,          # Plescia & Robinson 2019: blocky areas excluded from some counts; melt smoother than clastic ejecta
        "secondary_chain_probability": 0.05,# Low self-secondary expression on melt surfaces
        "slope_crater_loss_strength": 0.10,# Procedural
        "max_rim_irregularity": 0.025,     # Procedural
        "post_regolith_roughness_m": 0.025,# Procedural; smoother melt
        "crater_segments": [
            CraterSegment(2.5, 50.0, 0.002, 3.00),   # Plescia & Robinson 2019: melt surfaces can be as low as N(10)≈2/km² -> K≈0.002 for b=3
            CraterSegment(50.0, 250.0, 0.006, 3.00), # Procedural sparse larger craters
        ],
    },

    "fresh_clastic_ejecta": {
        # Hummocky clastic ejecta: crater-rich and rough.
        "base_relief_m": 5.5,              # Procedural
        "floor_final_blur_px": 2.4,        # Procedural
        "landform_count": 26,              # Procedural
        "landform_radius_min_m": 10.0,     # Procedural
        "landform_radius_max_m": 150.0,    # Procedural
        "landform_height_min_m": -8.0,     # Procedural
        "landform_height_max_m": 14.0,     # Procedural
        "landform_elongation_max": 4.5,    # Plescia & Robinson 2019: hummocky/lineated/lobate ejecta facies; value procedural
        "degradation_min": 0.00,           # Fresh surface
        "degradation_max": 0.45,           # Fresh-to-moderate
        "simple_depth_ratio": 0.145,       # Mahanti et al. 2018: class A mean d/D≈0.15
        "rim_height_ratio": 0.026,         # Procedural
        "rim_strength": 1.25,              # Procedural
        "ejecta_height_ratio": 0.008,      # Procedural; visible clastic ejecta texture
        "ejecta_strength": 1.70,           # Procedural; stronger ejecta blanket
        "fresh_blockiness": 0.70,          # Plescia & Robinson 2019 / Williams et al. 2022: fresh ejecta is blocky/heterogeneous
        "secondary_chain_probability": 0.30,# Plescia & Robinson 2019: self-secondary cratering important on ejecta
        "slope_crater_loss_strength": 0.20,# Procedural
        "max_rim_irregularity": 0.04,      # Procedural
        "post_regolith_roughness_m": 0.075,# Procedural
        "crater_segments": [
            CraterSegment(2.5, 50.0, 0.080, 3.50),   # Oetting et al. 2023 / Plescia & Robinson 2019: typical GB-like ejecta N(10) ~24–180+
            CraterSegment(50.0, 250.0, 0.015, 3.00), # Procedural sparse larger craters
        ],
    },

    "fresh_blocky_ejecta": {
        # Blocky ejecta: very rough and bouldery, but fewer countable tiny craters because blocks obscure/alter them.
        "base_relief_m": 7.0,              # Procedural
        "floor_final_blur_px": 2.0,        # Procedural
        "landform_count": 32,              # Procedural
        "landform_radius_min_m": 8.0,      # Procedural
        "landform_radius_max_m": 130.0,    # Procedural
        "landform_height_min_m": -10.0,    # Procedural
        "landform_height_max_m": 18.0,     # Procedural
        "landform_elongation_max": 5.0,    # Procedural
        "degradation_min": 0.00,           # Fresh surface
        "degradation_max": 0.40,           # Fresh-to-lightly degraded
        "simple_depth_ratio": 0.140,       # Mahanti et al. 2018: fresh class A/AB range
        "rim_height_ratio": 0.025,         # Procedural
        "rim_strength": 1.15,              # Procedural
        "ejecta_height_ratio": 0.008,      # Procedural
        "ejecta_strength": 1.40,           # Procedural
        "fresh_blockiness": 1.20,          # Plescia & Robinson 2019: blocky facies; Williams et al. 2022: high rock abundance affects crater density
        "secondary_chain_probability": 0.18,# Blocks obscure/modify small craters; keep chains lower than clastic ejecta
        "slope_crater_loss_strength": 0.30,# Blocks/roughness reduce countable small-crater survival
        "max_rim_irregularity": 0.05,      # Procedural
        "post_regolith_roughness_m": 0.120,# Procedural; rough blocky surface
        "crater_segments": [
            CraterSegment(2.5, 50.0, 0.030, 3.50),   # Williams et al. 2022: higher rock abundance has fewer small craters; K reduced from clastic ejecta
            CraterSegment(50.0, 250.0, 0.010, 3.00), # Procedural sparse larger craters
        ],
    },

    # Backward-compatible aliases from the original file.
    "mare_smooth": {
        "base_relief_m": 2.2, "floor_final_blur_px": 6.0, "landform_count": 18,
        "landform_radius_min_m": 35.0, "landform_radius_max_m": 180.0,
        "landform_height_min_m": -2.0, "landform_height_max_m": 2.5,
        "landform_elongation_max": 2.2, "degradation_min": 0.75, "degradation_max": 1.00,
        "simple_depth_ratio": 0.060, "rim_height_ratio": 0.007, "rim_strength": 0.45,
        "ejecta_height_ratio": 0.0015, "ejecta_strength": 0.20, "fresh_blockiness": 0.03,
        "secondary_chain_probability": 0.01, "slope_crater_loss_strength": 0.10,
        "max_rim_irregularity": 0.04, "post_regolith_roughness_m": 0.012,
        "crater_segments": [CraterSegment(2.5, 50.0, 0.015, 2.00), CraterSegment(50.0, 250.0, 0.020, 2.00)],
    },
    "highland_old": {
        "base_relief_m": 6.0, "floor_final_blur_px": 4.0, "landform_count": 42,
        "landform_radius_min_m": 18.0, "landform_radius_max_m": 220.0,
        "landform_height_min_m": -5.0, "landform_height_max_m": 8.0,
        "landform_elongation_max": 4.0, "degradation_min": 0.65, "degradation_max": 1.00,
        "simple_depth_ratio": 0.075, "rim_height_ratio": 0.012, "rim_strength": 0.70,
        "ejecta_height_ratio": 0.0025, "ejecta_strength": 0.35, "fresh_blockiness": 0.15,
        "secondary_chain_probability": 0.05, "slope_crater_loss_strength": 0.55,
        "max_rim_irregularity": 0.08, "post_regolith_roughness_m": 0.035,
        "crater_segments": [CraterSegment(2.5, 50.0, 0.030, 2.60), CraterSegment(50.0, 250.0, 0.060, 2.10)],
    },
    "fresh_crater_field": {
        "base_relief_m": 5.0, "floor_final_blur_px": 2.5, "landform_count": 22,
        "landform_radius_min_m": 12.0, "landform_radius_max_m": 140.0,
        "landform_height_min_m": -8.0, "landform_height_max_m": 12.0,
        "landform_elongation_max": 4.0, "degradation_min": 0.00, "degradation_max": 0.45,
        "simple_depth_ratio": 0.145, "rim_height_ratio": 0.025, "rim_strength": 1.20,
        "ejecta_height_ratio": 0.007, "ejecta_strength": 1.50, "fresh_blockiness": 0.80,
        "secondary_chain_probability": 0.25, "slope_crater_loss_strength": 0.20,
        "max_rim_irregularity": 0.035, "post_regolith_roughness_m": 0.070,
        "crater_segments": [CraterSegment(2.5, 50.0, 0.080, 3.50), CraterSegment(50.0, 250.0, 0.015, 3.00)],
    },
    "custom_scientific": {
        # User-tunable highland-like terrain with a denser crater population.
        # This is an independent preset rather than an alias to highland_scientific.
        "base_relief_m": 6.5,
        "floor_final_blur_px": 3.8,
        "landform_count": 48,
        "landform_radius_min_m": 16.0,
        "landform_radius_max_m": 220.0,
        "landform_height_min_m": -5.5,
        "landform_height_max_m": 9.0,
        "landform_elongation_max": 4.2,
        "degradation_min": 0.60,
        "degradation_max": 1.00,
        "simple_depth_ratio": 0.080,
        "rim_height_ratio": 0.013,
        "rim_strength": 0.75,
        "ejecta_height_ratio": 0.0030,
        "ejecta_strength": 0.40,
        "fresh_blockiness": 0.20,
        "secondary_chain_probability": 0.08,
        "slope_crater_loss_strength": 0.50,
        "max_rim_irregularity": 0.11,
        "post_regolith_roughness_m": 0.045,
        "crater_segments": [
            CraterSegment(2.5, 50.0, 0.060, 2.60),
            CraterSegment(50.0, 250.0, 0.100, 2.10),
        ],
    },
}


@dataclass
class Crater:
    x_m: float
    y_m: float
    diameter_m: float
    degradation: float
    segment_index: int
    irregularity: float
    angle_rad: float

    @property
    def radius_m(self) -> float:
        return self.diameter_m * 0.5


# -----------------------------
# Numeric helpers
# -----------------------------

def gaussian_blur_float32(data: np.ndarray, sigma_px: float) -> np.ndarray:
    """Blur float32 data. Uses scipy when available; otherwise Pillow."""
    sigma_px = float(sigma_px)
    arr = np.asarray(data, dtype=np.float32)
    if sigma_px <= 1e-6:
        return arr.copy()

    try:
        from scipy.ndimage import gaussian_filter
        return gaussian_filter(arr, sigma=sigma_px, mode="nearest").astype(np.float32)
    except (ImportError, ModuleNotFoundError):
        img = Image.fromarray(arr, mode="F")
        img = img.filter(ImageFilter.GaussianBlur(radius=sigma_px))
        return np.asarray(img, dtype=np.float32)


def resize_float_field(field: np.ndarray, size: int) -> np.ndarray:
    img = Image.fromarray(np.asarray(field, dtype=np.float32), mode="F")
    img = img.resize((size, size), resample=Image.Resampling.BICUBIC)
    return np.asarray(img, dtype=np.float32)


def normalize_signed_percentile(data: np.ndarray, percentile: float = 99.8) -> np.ndarray:
    arr = np.asarray(data, dtype=np.float32)
    arr = arr - float(np.mean(arr))
    scale = float(np.percentile(np.abs(arr), percentile))

    if scale > 1e-8:
        arr = arr / scale

    # Soft saturation instead of hard clipping.
    # This avoids flat cut-off plateaus while still limiting extreme values.
    arr = np.tanh(arr)

    arr -= float(np.mean(arr))
    max_abs = float(np.max(np.abs(arr)))
    if max_abs > 1e-8:
        arr = arr / max_abs

    return arr.astype(np.float32)


def smoothstep(edge0: float, edge1: float, x: np.ndarray) -> np.ndarray:
    denom = max(edge1 - edge0, 1e-8)
    t = np.clip((x - edge0) / denom, 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def lerp(a: np.ndarray | float, b: np.ndarray | float, t: np.ndarray | float) -> np.ndarray | float:
    return a * (1.0 - t) + b * t


# -----------------------------
# Base lunar floor
# -----------------------------

# Preset-specific floor-noise recipes. These remain deliberately close to the
# original MoonSim recipe so the terrain keeps the same smooth visual style,
# while each terrain family receives a mildly different spatial character.
#
# Each tuple is:
#   (random grid size, layer weight, blur radius at 8129 px)
FLOOR_NOISE_PROFILES: Dict[
    str,
    tuple[tuple[int, float, float], ...],
] = {
    # Original background recipe, unchanged.
    "mare": (
        (5, 1.00, 45.0),
        (9, 0.55, 32.0),
        (17, 0.25, 22.0),
        (33, 0.08, 16.0),
    ),

    # Very close to the original, with slightly more middle-scale structure.
    "apollo17": (
        (5, 1.00, 45.0),
        (10, 0.56, 31.0),
        (18, 0.26, 21.0),
        (35, 0.08, 15.0),
    ),

    # Slightly finer than the original, without becoming noisy.
    "highland": (
        (6, 0.96, 43.0),
        (11, 0.58, 30.0),
        (20, 0.28, 20.0),
        (38, 0.09, 14.0),
    ),

    # Moderately finer, but still based on the original smooth structure.
    "fresh": (
        (6, 0.94, 42.0),
        (12, 0.60, 29.0),
        (22, 0.30, 19.0),
        (42, 0.10, 13.0),
    ),

    # Smooth fresh impact-melt endmember, close to Mare.
    "fresh_melt": (
        (5, 1.00, 47.0),
        (9, 0.52, 34.0),
        (17, 0.22, 23.0),
        (33, 0.07, 16.0),
    ),

    # Highland-like custom profile with a mild fine-scale increase.
    "custom": (
        (6, 0.95, 42.0),
        (12, 0.59, 29.0),
        (23, 0.29, 19.0),
        (43, 0.10, 13.0),
    ),
}


def canonical_floor_noise_profile(preset_name: str) -> str:
    """Map every heightmap preset name to a floor-noise family."""
    key = str(preset_name or "default").strip().lower()

    aliases = {
        "default": "mare",
        "mare_smooth": "mare",
        "mare_scientific": "mare",

        "apollo17_scientific": "apollo17",

        "highland_old": "highland",
        "highland_scientific": "highland",

        "fresh_crater_field": "fresh",
        "fresh_crater_scientific": "fresh",
        "fresh_clastic_ejecta": "fresh",
        "fresh_blocky_ejecta": "fresh",

        "fresh_impact_melt": "fresh_melt",

        "custom_scientific": "custom",
    }

    return aliases.get(key, "mare")


def floor_noise_layers_for_preset(
    preset_name: str,
) -> List[tuple[int, float, float]]:
    """Return a copy of the selected preset's floor-noise layer recipe."""
    profile = canonical_floor_noise_profile(preset_name)
    return [tuple(layer) for layer in FLOOR_NOISE_PROFILES[profile]]


def smooth_lunar_floor(settings: GeneratorSettings) -> np.ndarray:
    """Very smooth, low-frequency floor in meters."""
    rng = np.random.default_rng(settings.seed)
    size = settings.size
    height = np.zeros((size, size), dtype=np.float32)

    layers = floor_noise_layers_for_preset(settings.preset)

    # Scale blur for non-8129 maps so previews/smaller tests behave similarly.
    blur_scale = size / 8129.0
    blur_scale = max(0.20, blur_scale)

    for grid_size, weight, blur_px in layers:
        field = rng.normal(0.0, 1.0, (grid_size, grid_size)).astype(np.float32)
        layer = resize_float_field(field, size)
        layer = gaussian_blur_float32(layer, blur_px * blur_scale)
        layer = normalize_signed_percentile(layer)
        height += layer * weight

    height = normalize_signed_percentile(height)
    height *= float(settings.base_relief_m)
    height = gaussian_blur_float32(height, settings.floor_final_blur_px * blur_scale)
    height -= float(np.mean(height))
    return height.astype(np.float32)


def add_broad_landforms(height_m: np.ndarray, settings: GeneratorSettings) -> None:
    """Adds subtle elliptical hills/basins in meters. This is optional art direction."""
    if not settings.add_broad_landforms or settings.landform_count <= 0:
        return

    rng = np.random.default_rng(settings.seed + 9101)
    size = settings.size
    meters_per_pixel = settings.map_size_m / float(size - 1)

    for _ in range(settings.landform_count):
        cx_m = rng.uniform(0.0, settings.map_size_m)
        cy_m = rng.uniform(0.0, settings.map_size_m)
        radius_m = rng.uniform(settings.landform_radius_min_m, settings.landform_radius_max_m)
        elong = rng.uniform(1.0, settings.landform_elongation_max)
        angle = rng.uniform(0.0, 2.0 * math.pi)
        height = rng.uniform(settings.landform_height_min_m, settings.landform_height_max_m)

        rx_m = radius_m * elong
        ry_m = radius_m / math.sqrt(elong)
        support_m = 3.25 * max(rx_m, ry_m)

        cx_px = cx_m / meters_per_pixel
        cy_px = cy_m / meters_per_pixel
        support_px = support_m / meters_per_pixel

        min_x = max(int(math.floor(cx_px - support_px)), 0)
        max_x = min(int(math.ceil(cx_px + support_px)), size - 1)
        min_y = max(int(math.floor(cy_px - support_px)), 0)
        max_y = min(int(math.ceil(cy_px + support_px)), size - 1)
        if min_x > max_x or min_y > max_y:
            continue

        xs = (np.arange(min_x, max_x + 1, dtype=np.float32) * meters_per_pixel) - cx_m
        ys = (np.arange(min_y, max_y + 1, dtype=np.float32) * meters_per_pixel) - cy_m
        xx, yy = np.meshgrid(xs, ys)

        ca = math.cos(angle)
        sa = math.sin(angle)
        xr = ca * xx + sa * yy
        yr = -sa * xx + ca * yy
        landform = np.exp(-0.5 * ((xr / max(rx_m, 1e-6)) ** 2 + (yr / max(ry_m, 1e-6)) ** 2))
        landform = np.power(landform, rng.uniform(0.75, 1.20))
        height_m[min_y:max_y + 1, min_x:max_x + 1] += (height * landform).astype(np.float32)


def make_base_terrain(settings: GeneratorSettings) -> np.ndarray:
    height = smooth_lunar_floor(settings)
    add_broad_landforms(height, settings)
    return height.astype(np.float32)


# -----------------------------
# Crater catalog
# -----------------------------

def expected_crater_count(map_size_m: float, segment: CraterSegment) -> float:
    area_m2 = map_size_m * map_size_m
    return area_m2 * segment.K * (
        segment.min_diameter_m ** (-segment.b) - segment.max_diameter_m ** (-segment.b)
    )


def K_from_N_geq_D_per_km2(N_per_km2: float, D_ref_m: float, b: float) -> float:
    """
    Convert a cumulative crater density N(>=D_ref) in km^-2 into this script's K.

    Script model:
        N_per_m2(>=D) = K * D^-b
        expected_count = area_m2 * K * (Dmin^-b - Dmax^-b)

    Example: Giordano Bruno average N(>=10 m) ≈ 24 km^-2, b≈3.5
    gives K≈0.076, close to the fresh_crater_scientific K=0.080.
    """
    return float((N_per_km2 / 1_000_000.0) * (D_ref_m ** b))


def sample_powerlaw_diameter(rng: np.random.Generator, dmin: float, dmax: float, b: float) -> float:
    u = float(rng.random())
    a = dmin ** (-b)
    c = dmax ** (-b)
    x = a - u * (a - c)
    return float(x ** (-1.0 / b))


def maybe_add_secondary_chain(
    rng: np.random.Generator,
    craters: List[Crater],
    parent: Crater,
    settings: GeneratorSettings,
) -> None:
    """
    Add a short, aligned chain/cluster of small secondaries.

    This is a procedural nod to Apollo 17 Tycho secondaries and Giordano Bruno
    self-secondaries. It is intentionally conservative: most generated craters
    still come from the CSFD segments.
    """
    p = float(np.clip(settings.secondary_chain_probability, 0.0, 1.0))
    if p <= 0.0 or parent.diameter_m < 30.0 or rng.random() > p:
        return

    chain_count = int(rng.integers(3, 8))
    angle = float(rng.uniform(0.0, 2.0 * math.pi))
    spacing = float(parent.radius_m * rng.uniform(0.65, 1.35))
    start_dist = float(parent.radius_m * rng.uniform(1.5, 3.5))

    for i in range(chain_count):
        along = start_dist + i * spacing * rng.uniform(0.75, 1.35)
        cross = rng.normal(0.0, parent.radius_m * 0.18)
        x = parent.x_m + along * math.cos(angle) + cross * math.cos(angle + math.pi / 2.0)
        y = parent.y_m + along * math.sin(angle) + cross * math.sin(angle + math.pi / 2.0)
        if not (0.0 <= x <= settings.map_size_m and 0.0 <= y <= settings.map_size_m):
            continue

        dmax = min(22.0, max(3.0, parent.diameter_m * 0.16))
        dmin = min(8.0, max(2.5, dmax * 0.25))
        d = float(rng.uniform(dmin, dmax))
        craters.append(
            Crater(
                x_m=float(x),
                y_m=float(y),
                diameter_m=d,
                degradation=float(np.clip(parent.degradation + rng.uniform(-0.05, 0.18), 0.0, 1.0)),
                segment_index=0,
                irregularity=float(rng.uniform(0.04, settings.max_rim_irregularity)),
                angle_rad=float(angle + rng.normal(0.0, 0.2)),
            )
        )


def generate_crater_catalog(settings: GeneratorSettings) -> List[Crater]:
    if not settings.crater_segments:
        raise ValueError("settings.crater_segments is empty")

    rng = np.random.default_rng(settings.seed + 101)
    craters: List[Crater] = []

    for segment_index, segment in enumerate(settings.crater_segments):
        lam = max(0.0, expected_crater_count(settings.map_size_m, segment))
        count = int(rng.poisson(lam))

        for _ in range(count):
            d = sample_powerlaw_diameter(
                rng,
                segment.min_diameter_m,
                segment.max_diameter_m,
                segment.b,
            )
            degradation = float(rng.uniform(settings.degradation_min, settings.degradation_max))
            irregularity = float(rng.uniform(0.0, settings.max_rim_irregularity))
            crater = Crater(
                x_m=float(rng.uniform(0.0, settings.map_size_m)),
                y_m=float(rng.uniform(0.0, settings.map_size_m)),
                diameter_m=d,
                degradation=degradation,
                segment_index=segment_index,
                irregularity=irregularity,
                angle_rad=float(rng.uniform(0.0, 2.0 * math.pi)),
            )
            craters.append(crater)
            maybe_add_secondary_chain(rng, craters, crater, settings)

    # Degraded/old first; fresh craters overprint old terrain.
    craters.sort(key=lambda c: c.degradation, reverse=True)
    return craters


# -----------------------------
# Terrain-aware crater integration
# -----------------------------

def crater_rim_perturbation(theta_rad: np.ndarray, crater: Crater) -> np.ndarray:
    """
    Per-crater random rim distortion.

    The random pattern is deterministic: the same crater metadata produces
    the same rim every time, but different craters get different rim shapes.
    """
    irr = crater.irregularity * (1.0 - 0.45 * crater.degradation)

    if irr <= 1e-8:
        return np.ones_like(theta_rad, dtype=np.float32)

    # Build a stable per-crater seed from existing crater properties.
    # This avoids storing extra rim phases/amplitudes in the Crater dataclass.
    rim_seed = (
        int(crater.x_m * 1000.0) * 73856093
        ^ int(crater.y_m * 1000.0) * 19349663
        ^ int(crater.diameter_m * 1000.0) * 83492791
        ^ int(crater.angle_rad * 1_000_000.0)
    ) & 0xFFFFFFFF

    rng = np.random.default_rng(rim_seed)

    # Low harmonics control broad shape: oval, triangular, squarish, lumpy.
    # Higher harmonics add smaller rim waviness.
    harmonics = np.array([2, 3, 4, 5, 6, 7, 8, 9], dtype=np.float32)

    # Larger amplitudes for low-frequency distortion, smaller for high-frequency detail.
    base_weights = np.array([0.65, 0.38, 0.30, 0.24, 0.18, 0.14, 0.11, 0.09], dtype=np.float32)

    random_weights = rng.uniform(0.35, 1.0, size=len(harmonics)).astype(np.float32)
    random_signs = rng.choice(np.array([-1.0, 1.0], dtype=np.float32), size=len(harmonics))
    phases = rng.uniform(0.0, 2.0 * math.pi, size=len(harmonics)).astype(np.float32)

    raw = np.zeros_like(theta_rad, dtype=np.float32)

    for h, w, s, p in zip(harmonics, base_weights * random_weights, random_signs, phases):
        raw += s * w * np.sin(float(h) * theta_rad + float(p))

    # Smooth normalization keeps max_rim_irregularity intuitive.
    # The final radial deformation is roughly within +/- irr.
    max_abs = float(np.max(np.abs(raw)))
    if max_abs > 1e-8:
        raw = raw / max_abs

    perturb = 1.0 + irr * raw

    # Prevent pathological self-crossing or collapsed rims if very large
    # max_rim_irregularity values are tested.
    return np.maximum(perturb, 0.35).astype(np.float32)

def terrain_factor_for_preset(preset: str, kind: str) -> float:
    """
    Backward-compatible preset-level scaling for apparent crater relief.

    Most control now comes from GeneratorSettings values, especially
    simple_depth_ratio, rim_strength, and ejecta_strength. This function only
    supplies a mild default when older preset names are used.
    """
    table = {
        "mare_smooth": {"depth": 0.90, "rim": 0.80, "ejecta": 0.50},
        "mare_scientific": {"depth": 0.90, "rim": 0.80, "ejecta": 0.50},
        "default": {"depth": 0.90, "rim": 0.80, "ejecta": 0.50},
        "highland_old": {"depth": 1.00, "rim": 1.00, "ejecta": 0.80},
        "highland_scientific": {"depth": 1.00, "rim": 1.00, "ejecta": 0.80},
        "custom_scientific": {"depth": 1.02, "rim": 1.03, "ejecta": 0.85},
        "apollo17_scientific": {"depth": 0.90, "rim": 0.90, "ejecta": 0.70},
        "fresh_crater_field": {"depth": 1.05, "rim": 1.10, "ejecta": 1.20},
        "fresh_crater_scientific": {"depth": 1.05, "rim": 1.10, "ejecta": 1.20},
        "fresh_impact_melt": {"depth": 0.95, "rim": 0.90, "ejecta": 0.60},
        "fresh_clastic_ejecta": {"depth": 1.05, "rim": 1.15, "ejecta": 1.35},
        "fresh_blocky_ejecta": {"depth": 1.00, "rim": 1.05, "ejecta": 1.20},
    }
    return float(table.get(preset, {}).get(kind, 1.0))


def d_over_D_from_degradation(degradation: float) -> float:
    """
    Depth/diameter model from Mahanti et al. (2018), Apollo 16/17 SLCs.

    Morphological class means:
        A  = 0.15
        AB = 0.12
        B  = 0.10
        BC = 0.08
        C  = 0.06

    degradation=0 maps to class A, degradation=1 maps to class C.
    """
    g = float(np.clip(degradation, 0.0, 1.0))
    xs = np.array([0.00, 0.25, 0.50, 0.75, 1.00], dtype=np.float32)
    ys = np.array([0.15, 0.12, 0.10, 0.08, 0.06], dtype=np.float32)
    return float(np.interp(g, xs, ys))


def degraded_depth_ratio(diameter_m: float, degradation: float, settings: GeneratorSettings) -> float:
    """
    Final crater depth/diameter ratio d/D.

    This now uses the Mahanti et al. (2018) d/D degradation classes and blends
    them with the preset's simple_depth_ratio. This makes the preset value
    meaningful while keeping the result scientifically bounded.
    """
    d = max(float(diameter_m), 1e-6)
    g = float(np.clip(degradation, 0.0, 1.0))

    class_ratio = d_over_D_from_degradation(g)
    target_ratio = float(settings.simple_depth_ratio)

    # Fresh crater presets should obey the fresh target more strongly.
    if settings.degradation_max <= 0.50:
        ratio = 0.45 * class_ratio + 0.55 * target_ratio
    else:
        ratio = 0.70 * class_ratio + 0.30 * target_ratio

    ratio *= terrain_factor_for_preset(settings.preset, "depth")

    # Fresh SLCs larger than 100 m with d/D>0.17 are rare in Mahanti et al.;
    # taper very large fresh craters slightly for render-safe simple bowls.
    large_taper = 1.0 - 0.12 * float(smoothstep(100.0, 250.0, np.array(d, dtype=np.float32))) * (1.0 - g)
    ratio *= large_taper

    # Mahanti et al.: >99% of SLCs fall roughly between 0.04 and 0.17.
    return float(np.clip(ratio, 0.035, 0.18))


def rim_height_ratio_law(diameter_m: float, degradation: float, settings: GeneratorSettings) -> float:
    """
    Final rim height / diameter.

    There is no single paper-derived universal rim ratio for these exact render
    stamps, so this is a procedural law constrained by crater degradation:
    rims fade faster than crater depth and fresh crater presets get stronger rims.
    """
    d = max(float(diameter_m), 1e-6)
    g = float(np.clip(degradation, 0.0, 1.0))

    base = float(settings.rim_height_ratio)
    size_taper = (d / 50.0) ** -0.05
    degradation_survival = 1.0 - 0.82 * g ** 1.15
    ratio = base * size_taper * degradation_survival
    ratio *= terrain_factor_for_preset(settings.preset, "rim") * float(settings.rim_strength)

    return float(np.clip(ratio, 0.0005, 0.040))


def ejecta_height_ratio_law(diameter_m: float, degradation: float, settings: GeneratorSettings) -> float:
    """
    Final ejecta/apron height / diameter.

    Fresh ejecta is visible and rough; old ejecta fades fast. Plescia & Robinson
    (2019) show fresh crater ejecta/melt facies vary strongly, so the preset's
    ejecta_strength is intentionally important here.
    """
    d = max(float(diameter_m), 1e-6)
    g = float(np.clip(degradation, 0.0, 1.0))

    base = float(settings.ejecta_height_ratio)
    size_taper = (d / 50.0) ** -0.10
    degradation_survival = (1.0 - g) ** 1.85
    ratio = base * size_taper * degradation_survival
    ratio *= terrain_factor_for_preset(settings.preset, "ejecta") * float(settings.ejecta_strength)

    return float(np.clip(ratio, 0.0, 0.025))

def add_crater_floor_roughness_to_patch(
    final_patch: np.ndarray,
    r: np.ndarray,
    crater: Crater,
    settings: GeneratorSettings,
) -> np.ndarray:
    """Add subtle deterministic roughness only inside large crater floors."""
    amp_base = float(settings.crater_floor_roughness_m)

    if amp_base <= 0.0:
        return final_patch.astype(np.float32)

    if crater.diameter_m < float(settings.crater_floor_roughness_min_diameter_m):
        return final_patch.astype(np.float32)

    seed = (
        int(crater.x_m * 1000.0) * 73856093
        ^ int(crater.y_m * 1000.0) * 19349663
        ^ int(crater.diameter_m * 1000.0) * 83492791
        ^ int(crater.degradation * 100000.0)
    ) & 0xFFFFFFFF

    rng = np.random.default_rng(seed + 4242)

    field = rng.normal(0.0, 1.0, final_patch.shape).astype(np.float32)
    field = gaussian_blur_float32(field, max(0.5, settings.crater_floor_roughness_blur_px))
    field = normalize_signed_percentile(field)

    degradation = float(np.clip(crater.degradation, 0.0, 1.0))

    # Older craters are smoother, but not perfectly smooth.
    degradation_factor = 0.60 + 0.40 * (1.0 - degradation)

    # Larger craters can carry slightly more visible floor texture.
    size_factor = float(np.clip(crater.diameter_m / 70.0, 0.65, 1.35))

    amplitude_m = amp_base * degradation_factor * size_factor

    # Only affect the inner bowl/floor. Fade out before the wall/rim.
    floor_mask = (1.0 - smoothstep(0.62, 0.92, r)) * (r <= 1.0)
    floor_mask = np.clip(floor_mask, 0.0, 1.0).astype(np.float32)

    return (final_patch + field * amplitude_m * floor_mask).astype(np.float32)


def integrate_crater_into_floor(
    height_m: np.ndarray,
    crater: Crater,
    settings: GeneratorSettings,
    crater_semantic_mask: Optional[np.ndarray] = None,
    crater_instance_mask: Optional[np.ndarray] = None,
    crater_boundary_mask: Optional[np.ndarray] = None,
    crater_id: int = 0,
) -> None:
    """
    Integrate one crater into the existing terrain.

    This is intentionally not a pure additive stamp. The crater interior is built
    relative to a local reference surface extracted from the terrain underneath
    the crater, then blended back into the original terrain with smooth radial
    masks. Rim and ejecta are added gently and fade out to zero.
    """
    size = settings.size
    meters_per_pixel = settings.map_size_m / float(size - 1)

    cx_px = crater.x_m / meters_per_pixel
    cy_px = crater.y_m / meters_per_pixel
    radius_px = crater.radius_m / meters_per_pixel
    if radius_px <= 1e-6:
        return

    outer_radius_px = settings.crater_outer_radius_ratio * radius_px
    falloff_px = settings.crater_edge_falloff_m / meters_per_pixel
    support_px = outer_radius_px + falloff_px

    min_x = max(int(math.floor(cx_px - support_px)), 0)
    max_x = min(int(math.ceil(cx_px + support_px)), size - 1)
    min_y = max(int(math.floor(cy_px - support_px)), 0)
    max_y = min(int(math.ceil(cy_px + support_px)), size - 1)
    if min_x > max_x or min_y > max_y:
        return

    xs = np.arange(min_x, max_x + 1, dtype=np.float32)
    ys = np.arange(min_y, max_y + 1, dtype=np.float32)
    xx, yy = np.meshgrid(xs, ys)
    dx = xx - cx_px
    dy = yy - cy_px
    dist_px = np.sqrt(dx * dx + dy * dy)
    theta = np.arctan2(dy, dx)

    perturb = crater_rim_perturbation(theta, crater)
    r = (dist_px / perturb) / radius_px

    original = height_m[min_y:max_y + 1, min_x:max_x + 1].astype(np.float32)

    # Local terrain reference: a smoothed version of the existing terrain patch.
    # This lets the crater cut into or settle on the current floor, slopes included.
    reference_sigma = np.clip(
        radius_px * settings.local_reference_radius_ratio,
        settings.min_local_reference_blur_px,
        settings.max_local_reference_blur_px,
    )
    local_reference = gaussian_blur_float32(original, float(reference_sigma))

    degradation = float(np.clip(crater.degradation, 0.0, 1.0))

    depth = degraded_depth_ratio(
        crater.diameter_m,
        degradation,
        settings,
    ) * crater.diameter_m

    rim_height = rim_height_ratio_law(
        crater.diameter_m,
        degradation,
        settings,
    ) * crater.diameter_m

    ejecta_height = ejecta_height_ratio_law(
        crater.diameter_m,
        degradation,
        settings,
    ) * crater.diameter_m

    # Bowl: deepest at center, zero near rim. Degraded craters are partly infilled.
    bowl = np.zeros_like(r, dtype=np.float32)
    inside = r <= 1.0
    bowl_shape = -(1.0 - np.clip(r, 0.0, 1.0) ** 2.15)
    bowl[inside] = (depth * bowl_shape[inside]).astype(np.float32)

    # Smooth wall/floor transition. Old craters have softer walls.
    interior_weight = 1.0 - smoothstep(0.98, 1.18 + 0.10 * degradation, r)
    interior_weight = np.clip(interior_weight, 0.0, 1.0).astype(np.float32)

    integrated = lerp(original, local_reference + bowl, interior_weight).astype(np.float32)

    # Rim: positive Gaussian around r=1.0.
    rim_sigma = settings.rim_width_radius_ratio * (1.0 + 0.65 * degradation)
    rim = rim_height * np.exp(-0.5 * ((r - 1.0) / max(rim_sigma, 1e-6)) ** 2)
    rim *= (1.0 - smoothstep(1.25, 1.70, r))
    rim *= smoothstep(0.55, 0.92, r)

    # Ejecta/apron: low positive blanket outside the rim, decaying outward.
    ejecta = np.zeros_like(r, dtype=np.float32)
    outside = (r > 1.0) & (r <= settings.crater_outer_radius_ratio)
    if np.any(outside):
        outer = settings.crater_outer_radius_ratio
        raw = np.power(np.maximum(r[outside], 1.0), -settings.ejecta_decay_exponent)
        far = outer ** (-settings.ejecta_decay_exponent)
        ejecta[outside] = ejecta_height * np.clip((raw - far) / max(1.0 - far, 1e-8), 0.0, 1.0)

    # Fade everything smoothly at the support edge.
    edge_weight = 1.0 - smoothstep(settings.crater_outer_radius_ratio, settings.crater_outer_radius_ratio + falloff_px / radius_px, r)
    edge_weight = np.clip(edge_weight, 0.0, 1.0).astype(np.float32)

    positive_delta = (rim + ejecta).astype(np.float32) * edge_weight

    # Locally smooth degraded crater deltas, not the whole terrain.
    delta_blur = settings.max_degraded_crater_blur_px * degradation * smoothstep(12.0, 90.0, np.array(crater.diameter_m, dtype=np.float32))
    if float(delta_blur) > 1e-6:
        positive_delta = gaussian_blur_float32(positive_delta, float(delta_blur))
        # Also smooth the replacement result slightly for old large craters.
        integrated = lerp(integrated, gaussian_blur_float32(integrated, float(delta_blur)), 0.35 * degradation).astype(np.float32)

    final_patch = integrated + positive_delta

    # Preserve exact edge continuity by fading the full crater operation back to the original patch.
    total_operation_weight = np.maximum(interior_weight, edge_weight * (r <= settings.crater_outer_radius_ratio + falloff_px / radius_px))
    total_operation_weight = np.clip(total_operation_weight, 0.0, 1.0).astype(np.float32)

    # Ground-truth masks use the crater rim coordinate, not the full ejecta/falloff
    # support. This prevents the semantic mask from becoming one huge white
    # influence blob and keeps nested crater rims visible in the boundary mask.
    crater_footprint = r <= 1.0

    if crater_semantic_mask is not None:
        crater_semantic_patch = crater_semantic_mask[min_y:max_y + 1, min_x:max_x + 1]
        crater_semantic_patch[crater_footprint] = 255

    if crater_instance_mask is not None and crater_id > 0:
        crater_instance_patch = crater_instance_mask[min_y:max_y + 1, min_x:max_x + 1]
        crater_instance_patch[crater_footprint] = int(crater_id)

    if crater_boundary_mask is not None:
        crater_boundary_patch = crater_boundary_mask[min_y:max_y + 1, min_x:max_x + 1]
        boundary_half_width_r = max(1.0 / max(radius_px, 1e-6), 0.015)
        crater_boundary = np.abs(r - 1.0) <= boundary_half_width_r
        crater_boundary_patch[crater_boundary] = 255

    final_patch = lerp(original, final_patch, total_operation_weight).astype(np.float32)

    final_patch = add_crater_floor_roughness_to_patch(
        final_patch,
        r,
        crater,
        settings,
    )

    height_m[min_y:max_y + 1, min_x:max_x + 1] = final_patch



def terrain_slope_degrees(height_m: np.ndarray, settings: GeneratorSettings) -> np.ndarray:
    """Return local slope in degrees from the current base terrain."""
    meters_per_pixel = settings.map_size_m / float(settings.size - 1)
    gy, gx = np.gradient(np.asarray(height_m, dtype=np.float32), meters_per_pixel, meters_per_pixel)
    slope_rad = np.arctan(np.sqrt(gx * gx + gy * gy))
    return np.degrees(slope_rad).astype(np.float32)


def slope_survival_probability(local_slope_deg: float, diameter_m: float, settings: GeneratorSettings) -> float:
    """
    Fewer small craters survive on steep slopes.

    Mahanti et al. (2018) and polar ShadowCam work show lower small-crater
    retention on steeper slopes. This is a procedural survival filter.
    """
    strength = float(np.clip(settings.slope_crater_loss_strength, 0.0, 1.0))
    if strength <= 0.0 or diameter_m > 180.0:
        return 1.0

    slope_factor = float(smoothstep(10.0, 25.0, np.array(local_slope_deg, dtype=np.float32)))
    size_factor = 1.0 - float(smoothstep(60.0, 180.0, np.array(diameter_m, dtype=np.float32)))
    return float(np.clip(1.0 - strength * 0.75 * slope_factor * size_factor, 0.05, 1.0))


def filter_craters_by_slope(
    craters: List[Crater],
    base_height_m: np.ndarray,
    settings: GeneratorSettings,
) -> List[Crater]:
    """Drop some small craters on steep slopes before integrating them."""
    if settings.slope_crater_loss_strength <= 0.0:
        return craters

    slopes = terrain_slope_degrees(base_height_m, settings)
    meters_per_pixel = settings.map_size_m / float(settings.size - 1)
    rng = np.random.default_rng(settings.seed + 707)
    kept: List[Crater] = []

    for crater in craters:
        x = int(np.clip(round(crater.x_m / meters_per_pixel), 0, settings.size - 1))
        y = int(np.clip(round(crater.y_m / meters_per_pixel), 0, settings.size - 1))
        p = slope_survival_probability(float(slopes[y, x]), crater.diameter_m, settings)
        if rng.random() <= p:
            kept.append(crater)

    return kept


def add_gaussian_bump(
    height_m: np.ndarray,
    x_m: float,
    y_m: float,
    diameter_m: float,
    height_bump_m: float,
    settings: GeneratorSettings,
) -> None:
    """Add a small positive Gaussian rock/boulder bump."""
    size = settings.size
    meters_per_pixel = settings.map_size_m / float(size - 1)
    cx_px = x_m / meters_per_pixel
    cy_px = y_m / meters_per_pixel
    sigma_px = max(0.5, (diameter_m / meters_per_pixel) * 0.35)
    support_px = max(2.0, 3.0 * sigma_px)

    min_x = max(int(math.floor(cx_px - support_px)), 0)
    max_x = min(int(math.ceil(cx_px + support_px)), size - 1)
    min_y = max(int(math.floor(cy_px - support_px)), 0)
    max_y = min(int(math.ceil(cy_px + support_px)), size - 1)
    if min_x > max_x or min_y > max_y:
        return

    xs = np.arange(min_x, max_x + 1, dtype=np.float32) - cx_px
    ys = np.arange(min_y, max_y + 1, dtype=np.float32) - cy_px
    xx, yy = np.meshgrid(xs, ys)
    bump = height_bump_m * np.exp(-0.5 * (xx * xx + yy * yy) / max(sigma_px * sigma_px, 1e-6))
    height_m[min_y:max_y + 1, min_x:max_x + 1] += bump.astype(np.float32)


def add_boulder_field(height_m: np.ndarray, crater: Crater, settings: GeneratorSettings) -> None:
    """
    Add boulder-like positive bumps around fresh craters.

    This is based on the qualitative/quantitative result that fresh crater ejecta
    and blocky facies are boulder-rich, while older craters lose boulders over
    hundreds of Myr. The exact bump size/count is procedural.
    """
    blockiness = float(np.clip(settings.fresh_blockiness, 0.0, 2.0))
    if blockiness <= 0.0 or crater.degradation > 0.55 or crater.diameter_m < 18.0:
        return

    seed = (
        int(crater.x_m * 1000.0) * 73856093
        ^ int(crater.y_m * 1000.0) * 19349663
        ^ int(crater.diameter_m * 1000.0) * 83492791
    ) & 0xFFFFFFFF
    rng = np.random.default_rng(seed + 909)

    # Larger/fresher craters get more boulders, but cap count for speed.
    freshness = 1.0 - float(np.clip(crater.degradation, 0.0, 1.0))
    expected = blockiness * freshness * max(1.0, crater.diameter_m / 12.0)
    n = int(min(80, rng.poisson(expected)))

    for _ in range(n):
        # Most larger boulders occur close to the rim; smaller ones can be farther out.
        rr = crater.radius_m * float(rng.uniform(1.0, 4.0) ** 0.75)
        theta = float(rng.uniform(0.0, 2.0 * math.pi))
        bx = crater.x_m + rr * math.cos(theta)
        by = crater.y_m + rr * math.sin(theta)
        if not (0.0 <= bx <= settings.map_size_m and 0.0 <= by <= settings.map_size_m):
            continue

        boulder_d = float(rng.lognormal(mean=math.log(0.9), sigma=0.55))
        boulder_d *= float(np.clip((crater.diameter_m / 80.0) ** 0.35, 0.45, 2.0))
        boulder_d = float(np.clip(boulder_d, 0.35, 8.0))
        boulder_h = boulder_d * float(rng.uniform(0.25, 0.65))
        add_gaussian_bump(height_m, bx, by, boulder_d, boulder_h, settings)

def add_post_regolith_roughness(height_m: np.ndarray, settings: GeneratorSettings) -> np.ndarray:
    if settings.post_regolith_roughness_m <= 0.0:
        return height_m.astype(np.float32)

    rng = np.random.default_rng(settings.seed + 303)
    size = settings.size
    field = rng.normal(0.0, 1.0, (size, size)).astype(np.float32)
    field = gaussian_blur_float32(field, max(0.5, settings.post_regolith_blur_px))
    field = normalize_signed_percentile(field)
    return (height_m + field * settings.post_regolith_roughness_m).astype(np.float32)


def build_heightfield(
    settings: GeneratorSettings,
    return_crater_semantic_mask: bool = False,
    return_crater_instance_mask: bool = False,
    return_crater_boundary_mask: bool = False,
) -> tuple:
    height = make_base_terrain(settings)
    crater_semantic_mask = np.zeros((settings.size, settings.size), dtype=np.uint8)
    crater_instance_mask = np.zeros((settings.size, settings.size), dtype=np.uint16)
    crater_boundary_mask = np.zeros((settings.size, settings.size), dtype=np.uint8)
    craters = generate_crater_catalog(settings)
    craters = filter_craters_by_slope(craters, height, settings)

    for crater_id, crater in enumerate(craters, start=1):
        integrate_crater_into_floor(
            height,
            crater,
            settings,
            crater_semantic_mask,
            crater_instance_mask,
            crater_boundary_mask,
            crater_id,
        )
        add_boulder_field(height, crater, settings)

    height = add_post_regolith_roughness(height, settings)

    if settings.final_global_blur_px > 1e-6:
        height = gaussian_blur_float32(height, settings.final_global_blur_px)

    height -= float(np.mean(height))

    results: list = [height.astype(np.float32), craters]
    if return_crater_semantic_mask:
        results.append(crater_semantic_mask)
    if return_crater_instance_mask:
        results.append(crater_instance_mask)
    if return_crater_boundary_mask:
        results.append(crater_boundary_mask)
    return tuple(results)


def unreal_landscape_scale(map_size_m, heightmap_size, height_range_m):
    """
    Returns Unreal Landscape import scale values.

    X/Y scale are in centimeters per vertex interval.
    Z scale follows Unreal's Landscape convention:
    total height range in cm / 512.
    """
    xy_scale_cm = (float(map_size_m) * 100.0) / float(heightmap_size - 1)
    z_scale = (float(height_range_m) * 100.0) / 512.0

    return xy_scale_cm, xy_scale_cm, z_scale


# -----------------------------
# Export
# -----------------------------

def encode_height_to_uint16(data_m: np.ndarray, min_m: float, max_m: float, dither_lsb: float, seed: int) -> np.ndarray:
    arr = np.asarray(data_m, dtype=np.float32)
    norm = (arr - min_m) / max(max_m - min_m, 1e-8)
    norm = np.clip(norm, 0.0, 1.0)
    value = norm * 65535.0

    if dither_lsb > 0.0:
        rng = np.random.default_rng(seed + 404)
        value += rng.uniform(-dither_lsb, dither_lsb, size=value.shape).astype(np.float32)

    return np.clip(value, 0.0, 65535.0).astype(np.uint16)


def compute_export_range(height_m: np.ndarray, settings: GeneratorSettings, mode: str) -> tuple[float, float]:
    if mode == "actual":
        pad = max(0.5, 0.05 * float(np.nanmax(height_m) - np.nanmin(height_m)))
        return float(np.nanmin(height_m) - pad), float(np.nanmax(height_m) + pad)
    if mode == "fixed":
        half = settings.height_range_m * 0.5
        return -half, half
    raise ValueError(f"Unknown export range mode: {mode}")

def export_map_crater_json(
    path: Path,
    settings: GeneratorSettings,
    craters: List[Crater],
    meters_per_pixel: float,
    ue_xy_scale_cm: float,
    ue_z_scale: float,
    generated_at: datetime,
    coordinate_mode: str = "top_left",
) -> None:
    """
    Export the versioned crater catalogue used by analysis and rock generation.

    coordinate_mode:
        "top_left" keeps x_m/y_m in the heightmap coordinate system:
        x=0 left, y=0 top.

        "centered" writes x_m/y_m relative to the map center:
        x=0,y=0 at map center.
    """
    if coordinate_mode not in {"top_left", "centered"}:
        raise ValueError("coordinate_mode must be 'top_left' or 'centered'")

    half = settings.map_size_m * 0.5

    payload = {
        "format": "LunarSimCraterCatalog",
        "format_version": 2,
        "provenance": build_provenance(generated_at),
        "preset_name": settings.preset,
        "seed": settings.seed,
        "map_size_meters": settings.map_size_m,
        "heightmap_size": settings.size,
        "meters_per_pixel": meters_per_pixel,
        "ue_xy_scale_cm": ue_xy_scale_cm,
        "ue_z_scale": ue_z_scale,

        "coordinate_system": {
            "mode": coordinate_mode,
            "top_left": "x_m=0 is left edge, y_m=0 is top edge",
            "centered": "x_m=0,y_m=0 is map center",
        },

        "crater_density_model": {
            "equation": "N_map ~ Poisson(A * K * (Dmin^(-b) - Dmax^(-b))) per segment",
            "diameter_units": "meters",
            "area_units": "square meters",
            "segments": [asdict(s) for s in settings.crater_segments or []],
            "generated_count": len(craters),
        },

        "terrain_generation": {
            "base_relief_m": settings.base_relief_m,
            "floor_final_blur_px": settings.floor_final_blur_px,
            "add_broad_landforms": settings.add_broad_landforms,
            "landform_count": settings.landform_count,
            "landform_radius_min_m": settings.landform_radius_min_m,
            "landform_radius_max_m": settings.landform_radius_max_m,
            "landform_height_min_m": settings.landform_height_min_m,
            "landform_height_max_m": settings.landform_height_max_m,
            "landform_elongation_max": settings.landform_elongation_max,
            "post_regolith_roughness_m": settings.post_regolith_roughness_m,
            "post_regolith_blur_px": settings.post_regolith_blur_px,
        },

        "illumination_metadata": {
            "sun_elevation_deg": settings.sun_elevation_deg,
            "sun_azimuth_deg": settings.sun_azimuth_deg,
        },

        "craters": [],
    }

    for crater_id, crater in enumerate(craters):
        if coordinate_mode == "centered":
            x_m = crater.x_m - half
            y_m = crater.y_m - half
        else:
            x_m = crater.x_m
            y_m = crater.y_m

        payload["craters"].append(
            {
                "crater_id": crater_id,
                "x_m": float(x_m),
                "y_m": float(y_m),
                "diameter_m": float(crater.diameter_m),
                "degradation": float(crater.degradation),
                "morphology": "NORMAL",

                # Compatibility aliases retained for existing Unreal readers.
                "degrade": float(crater.degradation),
                "morph": "NORMAL",
                "center_meters": {
                    "x": float(x_m),
                    "y": float(y_m),
                },
                "DiameterMeters": float(crater.diameter_m),
                "Degrade": float(crater.degradation),
                "Morph": "NORMAL",
            }
        )

    atomic_write_json(path, payload)


def save_outputs(
    out_dir: Path,
    settings: GeneratorSettings,
    height_m: np.ndarray,
    craters: List[Crater],
    range_mode: str,
    overwrite: bool = False,
) -> tuple[Dict[str, str], str]:
    """
    Save the compact heightmap package used by LunarSim-PG.

    Four timestamped, seed-qualified files are written: heightmap, metadata,
    crater catalogue, and a human-readable generation summary.
    """
    validate_settings(settings)
    out_dir.mkdir(parents=True, exist_ok=True)
    generated_at = utc_now()
    prefix = (
        f"{utc_file_timestamp(generated_at)}_"
        f"{safe_name(settings.preset)}_seed{settings.seed}"
    )

    min_m, max_m = compute_export_range(height_m, settings, range_mode)
    encoded = encode_height_to_uint16(
        height_m,
        min_m,
        max_m,
        settings.dither_lsb,
        settings.seed,
    )

    heightmap_path = out_dir / f"{prefix}_heightmap.png"
    metadata_path = out_dir / f"{prefix}_metadata.json"
    craters_path = out_dir / f"{prefix}_craters.json"
    summary_path = out_dir / f"{prefix}_generation_summary.txt"
    output_paths = (heightmap_path, metadata_path, craters_path, summary_path)
    existing = [path for path in output_paths if path.exists()]
    if existing and not overwrite:
        raise FileExistsError(
            "Refusing to overwrite existing output files: "
            + ", ".join(str(path) for path in existing)
        )

    atomic_save_png16(heightmap_path, encoded)

    meters_per_pixel = settings.map_size_m / float(settings.size - 1)
    ue_xy_scale_cm = meters_per_pixel * 100.0
    ue_z_scale = (max_m - min_m) * 100.0 / 512.0
    actual_min_m = float(np.nanmin(height_m))
    actual_max_m = float(np.nanmax(height_m))

    # The centered coordinate convention is shared with the offline rock-field
    # generator and is also understood by the heightmap analysis workflow.
    export_map_crater_json(
        craters_path,
        settings,
        craters,
        meters_per_pixel,
        ue_xy_scale_cm,
        ue_z_scale,
        generated_at,
        coordinate_mode="centered",
    )

    metadata = {
        "format": "LunarSimHeightmapPackage",
        "format_version": 2,
        "provenance": build_provenance(generated_at),
        "units": "meters until 16-bit export",
        "path_base": "this_json_directory",
        "preset": settings.preset,
        "seed": settings.seed,
        "heightmap_size_px": settings.size,
        "map_size_m": settings.map_size_m,
        "meters_per_pixel": meters_per_pixel,
        "coordinate_system": {
            "heightmap": "x_m=0 is left edge, y_m=0 is top edge",
            "craters": "centered coordinates, x/y in -map_size_m/2..+map_size_m/2",
        },
        "unreal_import": {
            "x_scale_cm": ue_xy_scale_cm,
            "y_scale_cm": ue_xy_scale_cm,
            "z_scale": ue_z_scale,
            "encoded_min_m": min_m,
            "encoded_max_m": max_m,
            "encoded_height_range_m": max_m - min_m,
        },
        "actual_height_stats_m": {
            "min": actual_min_m,
            "max": actual_max_m,
            "mean": float(np.nanmean(height_m)),
            "std": float(np.nanstd(height_m)),
            "p01": float(np.nanpercentile(height_m, 1)),
            "p99": float(np.nanpercentile(height_m, 99)),
        },
        "crater_density_model": {
            "equation": "N_map ~ Poisson(A * K * (Dmin^(-b) - Dmax^(-b))) per segment",
            "diameter_units": "meters",
            "area_units": "square meters",
            "segments": [asdict(s) for s in settings.crater_segments or []],
            "generated_count": len(craters),
        },
        "settings": settings_to_jsonable(settings),
        "output_files": {
            "heightmap": heightmap_path.name,
            "metadata": metadata_path.name,
            "craters": craters_path.name,
            "generation_summary": summary_path.name,
        },
        "checksums_sha256": {
            heightmap_path.name: sha256_file(heightmap_path),
            craters_path.name: sha256_file(craters_path),
        },
    }

    atomic_write_json(metadata_path, metadata)

    summary_text = (
        "Generated terrain-aware lunar heightmap\n"
        f"  preset: {settings.preset}\n"
        f"  seed: {settings.seed}\n"
        f"  size: {settings.size} x {settings.size}\n"
        f"  map size: {settings.map_size_m:.3f} m\n"
        f"  meters per pixel: {meters_per_pixel:.6f}\n"
        "Unreal Landscape import scale:\n"
        f"  X Scale = {ue_xy_scale_cm:.6f}\n"
        f"  Y Scale = {ue_xy_scale_cm:.6f}\n"
        f"  Z Scale = {ue_z_scale:.6f}\n"
        f"  craters: {len(craters)}\n"
        f"  actual height min/max: {actual_min_m:.3f} m / {actual_max_m:.3f} m\n"
    )

    atomic_write_text(summary_path, summary_text)

    return {
        "heightmap": str(heightmap_path),
        "metadata": str(metadata_path),
        "craters": str(craters_path),
        "generation_summary": str(summary_path),
    }, summary_text


def validate_settings(settings: GeneratorSettings) -> None:
    errors: List[str] = []
    if settings.size < 2:
        errors.append("size must be at least 2")
    if settings.map_size_m <= 0:
        errors.append("map_size_m must be positive")
    if settings.height_range_m <= 0:
        errors.append("height_range_m must be positive")
    if settings.base_relief_m < 0:
        errors.append("base_relief_m cannot be negative")
    if settings.floor_final_blur_px < 0:
        errors.append("floor_final_blur_px cannot be negative")
    if settings.landform_count < 0:
        errors.append("landform_count cannot be negative")
    if settings.landform_radius_min_m <= 0:
        errors.append("landform_radius_min_m must be positive")
    if settings.landform_radius_max_m < settings.landform_radius_min_m:
        errors.append("landform_radius_max_m must be >= landform_radius_min_m")
    if not 0.0 <= settings.degradation_min <= settings.degradation_max <= 1.0:
        errors.append("degradation range must satisfy 0 <= min <= max <= 1")
    if settings.rim_width_radius_ratio <= 0:
        errors.append("rim_width_radius_ratio must be positive")
    if settings.ejecta_decay_exponent <= 0:
        errors.append("ejecta_decay_exponent must be positive")
    if settings.crater_outer_radius_ratio <= 0:
        errors.append("crater_outer_radius_ratio must be positive")
    for name in (
        "min_local_reference_blur_px", "max_local_reference_blur_px",
        "max_degraded_crater_blur_px", "post_regolith_blur_px",
        "final_global_blur_px", "crater_floor_roughness_blur_px",
    ):
        if float(getattr(settings, name)) < 0:
            errors.append(f"{name} cannot be negative")
    if settings.max_local_reference_blur_px < settings.min_local_reference_blur_px:
        errors.append("max_local_reference_blur_px must be >= min_local_reference_blur_px")
    for index, segment in enumerate(settings.crater_segments or []):
        if segment.min_diameter_m <= 0:
            errors.append(f"crater segment {index} minimum diameter must be positive")
        if segment.max_diameter_m <= segment.min_diameter_m:
            errors.append(f"crater segment {index} maximum diameter must exceed minimum")
        if segment.K < 0:
            errors.append(f"crater segment {index} K cannot be negative")
        if segment.b <= 0:
            errors.append(f"crater segment {index} b must be positive")
    if errors:
        raise ValueError("Invalid heightmap settings: " + "; ".join(errors))


def settings_to_jsonable(settings: GeneratorSettings) -> Dict:
    data = asdict(settings)
    if settings.crater_segments is not None:
        data["crater_segments"] = [asdict(s) for s in settings.crater_segments]
    return data


def apply_preset(settings: GeneratorSettings, preset_name: str) -> GeneratorSettings:
    if preset_name not in PRESETS:
        raise ValueError(f"Unknown preset '{preset_name}'. Available: {', '.join(PRESETS)}")
    settings.preset = preset_name
    for key, value in PRESETS[preset_name].items():
        setattr(settings, key, value)
    return settings


# -----------------------------
# CLI
# -----------------------------

def parse_segments_json(path: Optional[str]) -> Optional[List[CraterSegment]]:
    if not path:
        return None
    with open(path, "r", encoding="utf-8") as f:
        raw = json.load(f)
    return [CraterSegment(**item) for item in raw]


def main() -> None:
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    parser = argparse.ArgumentParser(description="Generate terrain-aware lunar heightmaps for Unreal Engine.")
    parser.add_argument("--out", required=True, help="Output directory")
    parser.add_argument("--preset", choices=list(PRESETS.keys()), default="mare_smooth")
    parser.add_argument("--seed", type=int, default=12345)
    parser.add_argument("--size", type=int, default=1009, help="Heightmap resolution, e.g. 1009, 2017, 4033, 8129")
    parser.add_argument("--map-size-m", type=float, default=500.0)
    parser.add_argument("--height-range-m", type=float, default=30.0, help="Fixed encoded vertical range if --range-mode fixed")
    parser.add_argument("--range-mode", choices=["fixed", "actual"], default="fixed")
    parser.add_argument("--segments-json", default=None, help="Optional JSON list overriding crater CSFD segments")
    parser.add_argument("--no-landforms", action="store_true", help="Disable broad hills/basins")
    parser.add_argument("--post-roughness-m", type=float, default=None, help="Override post-crater regolith roughness amplitude")
    parser.add_argument("--crater-floor-roughness-m", type=float, default=None, help="Add mild roughness only inside larger crater floors")
    parser.add_argument("--crater-floor-roughness-blur-px", type=float, default=None, help="Blur radius for crater-floor roughness")
    parser.add_argument("--crater-floor-roughness-min-diameter-m", type=float, default=None, help="Only add crater-floor roughness above this crater diameter")
    parser.add_argument("--overwrite", action="store_true", help="Allow replacement of an identical timestamped output name")
    args = parser.parse_args()

    settings = apply_preset(GeneratorSettings(), args.preset)
    settings.seed = args.seed
    settings.size = args.size
    settings.map_size_m = args.map_size_m
    settings.height_range_m = args.height_range_m
    if args.no_landforms:
        settings.add_broad_landforms = False
    if args.post_roughness_m is not None:
        settings.post_regolith_roughness_m = args.post_roughness_m
    if args.crater_floor_roughness_m is not None:
        settings.crater_floor_roughness_m = args.crater_floor_roughness_m

    if args.crater_floor_roughness_blur_px is not None:
        settings.crater_floor_roughness_blur_px = args.crater_floor_roughness_blur_px

    if args.crater_floor_roughness_min_diameter_m is not None:
        settings.crater_floor_roughness_min_diameter_m = args.crater_floor_roughness_min_diameter_m
    custom_segments = parse_segments_json(args.segments_json)
    if custom_segments is not None:
        settings.crater_segments = custom_segments

    validate_settings(settings)
    height_m, craters = build_heightfield(settings)
    paths, summary_text = save_outputs(
        Path(args.out),
        settings,
        height_m,
        craters,
        args.range_mode,
        overwrite=args.overwrite,
    )

    print(summary_text, end="" if summary_text.endswith("\n") else "\n")
    print("Output files:")
    for label, output_path in paths.items():
        print(f"  {label}: {output_path}")



if __name__ == "__main__":
    main()