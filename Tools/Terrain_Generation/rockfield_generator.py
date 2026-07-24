#!/usr/bin/env python3
"""
Offline MoonSim rock-field generator.

This is a Python port of the crater-first logic in RockFieldGenerator.cpp,
including the scientific rock-placement presets used by the Unreal baker.
It reads one or more terrain crater JSON files, usually:

    Heightmaps/out/rockfield_json/<terrain>_rockfield_craters.json

and writes one complete rock-field JSON for each terrain:

    <out-root>/<terrain>/<terrain>_unreal_rockfield.json

It intentionally does NOT reproduce Unreal-only placement data such as mesh name,
world Z, ground-normal alignment, burial, or HISM instance transforms. Those need
an Unreal World, mesh bounds, and line traces. This script generates the offline
candidate rock positions and metadata that the analysis/paper scripts can use.

This balanced paper version keeps the Unreal crater-first generation structure:
crater-owned rocks are generated first and store dominant_crater_index, crater_zone,
normalized_crater_radius, and source_type. It also keeps the scientific preset
size, distance, zone, freshness, and material laws.

The default background cap is intentionally set to "maxrocks" rather than the
strict Unreal cap, because the strict Unreal cap can suppress almost all background
rocks when a scientifically old/degraded profile generates few crater-owned rocks.
Use --background-cap-mode unreal when you want to reproduce the exact Unreal cap.
Use the default/maxrocks mode when you want paper-ready offline fields with both
regional background abundance and crater-owned metadata.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import sys
from collections import Counter
from dataclasses import asdict, dataclass, fields
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Tuple

PI = math.pi
KINDA_SMALL_NUMBER = 1.0e-4


# -----------------------------------------------------------------------------
# Unreal-style math / RNG helpers
# -----------------------------------------------------------------------------


def clamp(x: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, x))


def round_to_int(x: float) -> int:
    """Good match for positive FMath::RoundToInt inputs used here."""
    if x >= 0.0:
        return int(math.floor(x + 0.5))
    return int(math.ceil(x - 0.5))


class FRandomStreamLike:
    """
    Small Python version of Unreal's deterministic FRandomStream fraction stream.

    This keeps outputs stable across runs and closer to the C++ generator than
    Python's default random.Random. It is still an offline port, not a compiled
    Unreal call.
    """

    def __init__(self, seed: int) -> None:
        self.seed = int(seed) & 0xFFFFFFFF

    def _mutate_seed(self) -> None:
        self.seed = (self.seed * 196314165 + 907633515) & 0xFFFFFFFF

    def frand(self) -> float:
        self._mutate_seed()
        return ((self.seed >> 16) & 0x7FFF) / 32767.0

    def frand_range(self, a: float, b: float) -> float:
        return float(a) + (float(b) - float(a)) * self.frand()


def sample_power_law_diameter(rng: FRandomStreamLike, min_d: float, max_d: float, beta: float) -> float:
    d_min = max(1.0e-6, float(min_d))
    d_max = max(d_min + 1.0e-6, float(max_d))
    b = max(0.1, float(beta))
    u = clamp(rng.frand(), 1.0e-9, 1.0 - 1.0e-9)
    a = d_min ** (-b)
    c = d_max ** (-b)
    x = a - (a - c) * u
    d = x ** (-1.0 / b)
    return clamp(d, d_min, d_max)


def safe_power(x: float, p: float) -> float:
    return max(1.0e-12, float(x)) ** float(p)


def sample_gaussian(rng: FRandomStreamLike, mean: float, sigma: float) -> float:
    u1 = clamp(rng.frand(), 1.0e-6, 1.0 - 1.0e-6)
    u2 = clamp(rng.frand(), 1.0e-6, 1.0 - 1.0e-6)
    z0 = math.sqrt(-2.0 * math.log(u1)) * math.cos(2.0 * PI * u2)
    return float(mean) + float(sigma) * z0


def sample_poisson(rng: FRandomStreamLike, mean: float) -> int:
    lam = max(0.001, float(mean))
    l_val = math.exp(-lam)
    k = 0
    p = 1.0
    while True:
        k += 1
        p *= clamp(rng.frand(), 1.0e-6, 1.0)
        if not (p > l_val and k < 10000):
            break
    return max(0, k - 1)


def distance(a: Tuple[float, float], b: Tuple[float, float]) -> float:
    dx = a[0] - b[0]
    dy = a[1] - b[1]
    return math.sqrt(dx * dx + dy * dy)


# -----------------------------------------------------------------------------
# Data model matching the C++ structs
# -----------------------------------------------------------------------------


@dataclass
class CraterInfo:
    x_m: float
    y_m: float
    diameter_m: float
    degrade: float = 1.0
    morph: str = "NORMAL"

    @property
    def center(self) -> Tuple[float, float]:
        return (self.x_m, self.y_m)

    @property
    def radius_m(self) -> float:
        return 0.5 * max(0.001, self.diameter_m)


@dataclass
class RockInstance:
    instance_id: int
    x_m: float
    y_m: float
    diameter_m: float
    size_class: str
    material_type: str
    crater_zone: str
    dominant_crater_index: int
    distance_to_dominant_crater_center_m: float
    normalized_crater_radius: float
    local_slope_deg: float
    local_density_per_m2: float
    acceptance_probability: float
    source_type: str
    clump_id: int
    yaw_degrees: float = 0.0
    tilt_degrees: float = 0.0
    tilt_axis_degrees: float = 0.0
    burial_fraction: float = 0.0


@dataclass
class RockGenSettings:
    # General
    seed: int = 1337
    map_size_meters_x: float = 500.0
    map_size_meters_y: float = 500.0
    max_rocks: int = 50000

    # Background regolith rocks
    background_density_per_m2: float = 0.0002
    background_fraction_cap: float = 0.03
    background_clump_fraction: float = 0.02
    background_cluster_sigma_m: float = 3.0
    # Match Unreal RockFieldGenerator.cpp by default: background rocks are capped
    # relative to already generated crater-owned rocks. Use "maxrocks" only for
    # analysis experiments that intentionally want a background-only field.
    background_cap_mode: str = "maxrocks"  # "unreal" or "maxrocks"

    # Rock diameter distribution
    min_rock_diameter: float = 0.10
    max_rock_diameter_cap: float = 6.0
    power_law_exponent: float = 3.8

    # Size classes
    pebble_max_diameter: float = 0.10
    small_max_diameter: float = 0.30
    medium_max_diameter: float = 0.75
    large_max_diameter: float = 1.50

    # Brial settings
    max_random_tilt_degrees: float = 12.0
    min_burial_fraction: float = 0.05
    max_burial_fraction: float = 0.25

    # Crater filtering
    min_source_crater_diameter_meters: float = 3.0
    max_source_crater_degrade: float = 1.0

    # Bart/Melosh-style scaling
    bm_max_diameter_a: float = 0.40
    bm_max_diameter_b: float = 0.65
    bm_median_diameter_a: float = 0.078
    bm_median_diameter_b: float = 0.62
    bm_max_distance_a: float = 0.024
    bm_max_distance_b: float = 0.66
    bm_median_distance_a: float = 0.0023
    bm_median_distance_b: float = 0.86
    boulder_diameter_scale: float = 0.045
    boulder_distance_scale: float = 1.2

    # Crater source abundance
    crater_boulder_density_scale: float = 0.08
    crater_count_exponent: float = 1.35
    freshness_gamma: float = 2.0
    freshness_floor: float = 0.01
    max_rocks_per_crater: int = 5200

    # Zone fractions
    interior_fraction: float = 0.04
    rim_fraction: float = 0.42
    proximal_fraction: float = 0.43
    distal_fraction: float = 0.11

    # Zone bounds in normalized crater radius r/R
    interior_r_min: float = 0.25
    interior_r_max: float = 0.85
    rim_r_min: float = 0.85
    rim_r_max: float = 1.20
    proximal_r_min: float = 1.20
    proximal_r_max: float = 2.50
    distal_r_min: float = 2.50
    distal_r_max: float = 6.00

    # Distance behavior
    rim_distance_power: float = 1.7
    proximal_distance_power: float = 2.0
    distal_distance_power: float = 2.6
    interior_distance_power: float = 1.2

    # Size-distance relationship
    distance_size_decay: float = 1.5
    distal_size_multiplier: float = 0.35
    proximal_size_multiplier: float = 0.85
    rim_size_multiplier: float = 1.20
    interior_size_multiplier: float = 0.30

    # Crater-owned clumping
    crater_clump_fraction: float = 0.65
    mean_cluster_size: float = 8.0
    cluster_sigma_m: float = 1.6
    cluster_zone_bias: str = "rim_proximal"  # rim, proximal, rim_proximal, all_ejecta

    # Random big-rock clumps
    enable_random_big_rock_clumps: bool = False
    random_big_rock_clump_count: int = 8
    random_big_rock_mean_rocks_per_clump: float = 5.0
    random_big_rock_clump_sigma_m: float = 5.0
    random_big_rock_min_diameter_meters: float = 1.5
    random_big_rock_max_diameter_meters: float = 6.0
    random_big_rock_exclude_crater_min_diameter_meters: float = 60.0
    random_big_rock_exclude_crater_radius_multiplier: float = 1.0

    # Scientific preset selection.
    # Accepted profile names include: default, mare, mare_scientific, highland_scientific,
    # polar_highlands, apollo17_scientific, apollo_17, fresh_crater_scientific,
    # new_fresh_zone, custom, and custom_scientific. Heightmap preset names are
    # mapped automatically.
    use_scientific_preset_values: bool = True
    rock_profile: str = "default"

    # Preset-driven material labels. These mirror the updated C++ generator.
    background_material: str = "RegolithCovered"
    floor_material: str = "RegolithCovered"
    old_ejecta_material: str = "MixedBreccia"
    fresh_ejecta_material: str = "FreshEjecta"



# -----------------------------------------------------------------------------
# Scientific rock presets matching the updated Unreal baker
# -----------------------------------------------------------------------------


def canonical_rock_profile(profile: Optional[str]) -> str:
    """Map heightmap/Unreal preset names to the rock preset families."""
    raw = normalize_key(str(profile or "default"))
    aliases = {
        "": "default",
        "default": "default",
        "generic": "default",
        "moon": "default",
        "mare": "mare",
        "mare_smooth": "mare",
        "mare_scientific": "mare",
        "mare_basalt": "mare",
        "highland": "polar_highlands",
        "highlands": "polar_highlands",
        "highland_old": "polar_highlands",
        "highland_scientific": "polar_highlands",
        "polar_highland": "polar_highlands",
        "polar_highlands": "polar_highlands",
        "apollo17": "apollo_17",
        "apollo_17": "apollo_17",
        "apollo17_scientific": "apollo_17",
        "apollo_17_scientific": "apollo_17",
        "taurus_littrow": "apollo_17",
        "fresh": "new_fresh_zone",
        "fresh_crater": "new_fresh_zone",
        "fresh_crater_field": "new_fresh_zone",
        "fresh_crater_scientific": "new_fresh_zone",
        "fresh_impact_melt": "new_fresh_zone",
        "fresh_clastic_ejecta": "new_fresh_zone",
        "fresh_blocky_ejecta": "new_fresh_zone",
        "new_fresh_zone": "new_fresh_zone",
        "newfreshzone": "new_fresh_zone",
        "custom": "custom",
        "custom_scientific": "custom",
        "custom_rock": "custom",
    }
    return aliases.get(raw, raw)


def apply_scientific_rock_preset(settings: RockGenSettings, profile: Optional[str] = None) -> None:
    """
    Apply the same paper-backed values used by the updated Unreal rock baker.

    Paper-backed parts:
      - Bart & Melosh 2010: boulder diameter/distance scaling laws.
      - Watkins et al. 2019: boulder threshold, largest boulders, slopes, counts, radial trends.
      - Li & Wu 2018: rock-abundance examples; conversion to count density is procedural.

    Values marked procedural in comments below are not directly measured in the papers;
    they are generator controls tuned to express the paper constraints at game-map scale.
    """
    p = canonical_rock_profile(profile or settings.rock_profile)
    settings.rock_profile = p

    # Shared Bart & Melosh 2010 scaling laws.
    settings.bm_max_diameter_a = 0.40              # Bart & Melosh 2010: max boulder diameter = 0.40 * craterD^0.65.
    settings.bm_max_diameter_b = 0.65              # Bart & Melosh 2010.
    settings.bm_median_diameter_a = 0.078          # Bart & Melosh 2010: median boulder diameter = 0.078 * craterD^0.62.
    settings.bm_median_diameter_b = 0.62           # Bart & Melosh 2010.
    settings.bm_max_distance_a = 0.024             # Bart & Melosh 2010: max boulder distance coefficient.
    settings.bm_max_distance_b = 0.66              # Bart & Melosh 2010.
    settings.bm_median_distance_a = 0.0023         # Bart & Melosh 2010: median boulder distance coefficient.
    settings.bm_median_distance_b = 0.86           # Bart & Melosh 2010.
    settings.boulder_distance_scale = 1000.0       # Unit/tuning scale: Bart & Melosh distance fit behaves like km when D is meters.

    # Size-class labels. These bins are not direct paper measurements.
    settings.pebble_max_diameter = 0.10            # Not a paper value; label bin only.
    settings.small_max_diameter = 0.30             # Not a paper value; close to 25.6 cm boulder threshold.
    settings.medium_max_diameter = 0.75            # Not a paper value; label bin only.
    settings.large_max_diameter = 1.50             # Not a paper value; label bin only.

    if p == "mare":
        settings.max_rocks = 50000                 # Engine/performance cap; not a paper value.
        settings.min_rock_diameter = 0.26          # Watkins et al. 2019: boulder >25.6 cm.
        settings.max_rock_diameter_cap = 3.5       # Watkins et al. 2019: Surveyor mare crater largest boulder 3.1 m.
        settings.power_law_exponent = 4.7          # Watkins et al. 2019: Surveyor crater boulder slope b=4.7.
        settings.min_source_crater_diameter_meters = 3.0  # Procedural filter; old mare tiny craters should not all make boulder fields.
        settings.max_source_crater_degrade = 1.0  # Watkins retention trend; degrade mapping is procedural.
        settings.background_density_per_m2 = 0.045 # Li & Wu 2018: Oceanus/global k~0.0037-0.004; count conversion procedural.
        settings.background_fraction_cap = 0.60    # Procedural cap.
        settings.background_clump_fraction = 0.02  # Procedural; smooth mare background.
        settings.background_cluster_sigma_m = 4.0  # Procedural.
        settings.boulder_diameter_scale = 0.25     # Tuned from Watkins Surveyor 200 m crater, largest boulder 3.1 m.
        settings.crater_boulder_density_scale = 0.035   # Count-calibrated for visible crater-owned rocks at 500 m map scale; spatial/size laws unchanged.
        settings.crater_count_exponent = 1.25      # Procedural crater-size count scaling.
        settings.freshness_gamma = 2.0             # Procedural stronger loss for old/degraded mare.
        settings.freshness_floor = 0.001           # Procedural.
        settings.max_rocks_per_crater = 150        # Watkins et al. 2019: Surveyor crater 111 boulders.
        settings.interior_fraction = 0.03          # Bart & Melosh 2010: mare has fewer interior boulders than highlands.
        settings.rim_fraction = 0.45               # Watkins et al. 2019: largest boulders close to rim.
        settings.proximal_fraction = 0.44          # Watkins radial trend; fraction procedural.
        settings.distal_fraction = 0.08            # Watkins radial trend; old mare fewer distal surviving boulders.
        settings.interior_r_min = 0.30; settings.interior_r_max = 0.80 # Procedural crater-floor zone.
        settings.rim_r_min = 0.85; settings.rim_r_max = 1.15           # Procedural rim zone.
        settings.proximal_r_min = 1.15; settings.proximal_r_max = 2.25 # Watkins: large boulders mostly within ~2-4 radii.
        settings.distal_r_min = 2.25; settings.distal_r_max = 4.00     # Practical old-mare limit; Surveyor max distance ~5.7 radii.
        settings.interior_distance_power = 1.3; settings.rim_distance_power = 1.9; settings.proximal_distance_power = 2.2; settings.distal_distance_power = 3.0 # Procedural sampling curves.
        settings.distance_size_decay = 1.7         # Watkins/Bart trend: size decreases outward; exact exponent procedural.
        settings.interior_size_multiplier = 0.25; settings.rim_size_multiplier = 1.10; settings.proximal_size_multiplier = 0.75; settings.distal_size_multiplier = 0.25 # Procedural zone scaling.
        settings.crater_clump_fraction = 0.40; settings.mean_cluster_size = 5.0; settings.cluster_sigma_m = 2.0; settings.cluster_zone_bias = "rim_proximal" # Procedural.
        settings.enable_random_big_rock_clumps = False # Procedural; disabled for smooth mare.
        settings.random_big_rock_clump_count = 2; settings.random_big_rock_mean_rocks_per_clump = 3.0; settings.random_big_rock_clump_sigma_m = 5.0 # Procedural.
        settings.random_big_rock_min_diameter_meters = 1.0; settings.random_big_rock_max_diameter_meters = 3.5 # Watkins Surveyor largest 3.1 m.
        settings.random_big_rock_exclude_crater_min_diameter_meters = 60.0; settings.random_big_rock_exclude_crater_radius_multiplier = 1.0 # Procedural.
        settings.background_material = "MareBasalt"; settings.floor_material = "RegolithCovered"; settings.old_ejecta_material = "MixedBreccia"; settings.fresh_ejecta_material = "FreshEjecta"

    elif p == "polar_highlands":
        settings.max_rocks = 70000                 # Engine/performance cap; not a paper value.
        settings.min_rock_diameter = 0.26          # Watkins et al. 2019: boulder >25.6 cm.
        settings.max_rock_diameter_cap = 20.0      # Watkins highland/fresh craters can have 8-22 m boulders; conservative cap.
        settings.power_law_exponent = 3.8          # Watkins et al. 2019: North Ray slope b=3.8.
        settings.min_source_crater_diameter_meters = 6.0 # Procedural filter.
        settings.max_source_crater_degrade = 1.0  # Watkins retention trend; degrade mapping procedural.
        settings.background_density_per_m2 = 0.10  # Inferred from Li & Wu low/moderate k plus highland behavior; conversion procedural.
        settings.background_fraction_cap = 0.50    # Procedural cap.
        settings.background_clump_fraction = 0.06  # Procedural highland clumping.
        settings.background_cluster_sigma_m = 3.5  # Procedural.
        settings.boulder_diameter_scale = 0.50     # Tuned from Watkins North Ray 22.1 m and South Ray 14.2 m.
        settings.crater_boulder_density_scale = 0.060   # Count-calibrated for visible crater-owned rocks at 500 m map scale; Bart/Melosh spatial/size laws unchanged.
        settings.crater_count_exponent = 1.35      # Procedural.
        settings.freshness_gamma = 1.2             # Procedural.
        settings.freshness_floor = 0.005           # Procedural.
        settings.max_rocks_per_crater = 3000       # Watkins: South Ray 3109; North Ray much higher.
        settings.interior_fraction = 0.12          # Bart & Melosh 2010: highland craters have more interior boulders.
        settings.rim_fraction = 0.38; settings.proximal_fraction = 0.38; settings.distal_fraction = 0.12 # Watkins radial trend, fractions procedural.
        settings.interior_r_min = 0.20; settings.interior_r_max = 0.90; settings.rim_r_min = 0.85; settings.rim_r_max = 1.20 # Procedural zones.
        settings.proximal_r_min = 1.20; settings.proximal_r_max = 3.00; settings.distal_r_min = 3.00; settings.distal_r_max = 6.00 # Watkins: large boulders mostly ~2-4 radii; some farther.
        settings.interior_distance_power = 1.1; settings.rim_distance_power = 1.6; settings.proximal_distance_power = 2.0; settings.distal_distance_power = 2.5 # Procedural.
        settings.distance_size_decay = 1.4         # Watkins/Bart size decreases outward; exact exponent procedural.
        settings.interior_size_multiplier = 0.45; settings.rim_size_multiplier = 1.25; settings.proximal_size_multiplier = 0.90; settings.distal_size_multiplier = 0.35 # Procedural.
        settings.crater_clump_fraction = 0.65; settings.mean_cluster_size = 8.0; settings.cluster_sigma_m = 2.2; settings.cluster_zone_bias = "rim_proximal" # Procedural.
        settings.enable_random_big_rock_clumps = False # Procedural fractured-block pass.
        settings.random_big_rock_clump_count = 8; settings.random_big_rock_mean_rocks_per_clump = 5.0; settings.random_big_rock_clump_sigma_m = 5.0 # Procedural.
        settings.random_big_rock_min_diameter_meters = 1.5; settings.random_big_rock_max_diameter_meters = 12.0 # Watkins highland young craters up to 22.1 m; conservative.
        settings.random_big_rock_exclude_crater_min_diameter_meters = 60.0; settings.random_big_rock_exclude_crater_radius_multiplier = 1.0 # Procedural.
        settings.background_material = "HighlandAnorthosite"; settings.floor_material = "RegolithCovered"; settings.old_ejecta_material = "MixedBreccia"; settings.fresh_ejecta_material = "FreshEjecta"

    elif p == "custom":
        # Independent highland-like profile for the GUI Custom terrain. These
        # values deliberately increase crater-owned abundance and add many
        # large background clumps; they are procedural controls rather than a
        # separate measured lunar terrain class.
        settings.max_rocks = 100000
        settings.min_rock_diameter = 0.26
        settings.max_rock_diameter_cap = 20.0
        settings.power_law_exponent = 3.8
        settings.min_source_crater_diameter_meters = 6.0
        settings.max_source_crater_degrade = 1.0
        settings.background_density_per_m2 = 0.012
        settings.background_fraction_cap = 0.55
        settings.background_clump_fraction = 0.08
        settings.background_cluster_sigma_m = 3.5
        settings.boulder_diameter_scale = 0.55
        settings.crater_boulder_density_scale = 0.080
        settings.crater_count_exponent = 1.35
        settings.freshness_gamma = 1.2
        settings.freshness_floor = 0.005
        settings.max_rocks_per_crater = 3500
        settings.interior_fraction = 0.12
        settings.rim_fraction = 0.38
        settings.proximal_fraction = 0.38
        settings.distal_fraction = 0.12
        settings.interior_r_min = 0.20; settings.interior_r_max = 0.90
        settings.rim_r_min = 0.85; settings.rim_r_max = 1.20
        settings.proximal_r_min = 1.20; settings.proximal_r_max = 3.00
        settings.distal_r_min = 3.00; settings.distal_r_max = 6.00
        settings.interior_distance_power = 1.1
        settings.rim_distance_power = 1.6
        settings.proximal_distance_power = 2.0
        settings.distal_distance_power = 2.5
        settings.distance_size_decay = 1.4
        settings.interior_size_multiplier = 0.45
        settings.rim_size_multiplier = 1.25
        settings.proximal_size_multiplier = 0.90
        settings.distal_size_multiplier = 0.35
        settings.crater_clump_fraction = 0.70
        settings.mean_cluster_size = 10.0
        settings.cluster_sigma_m = 5.0
        settings.cluster_zone_bias = "rim_proximal"
        settings.enable_random_big_rock_clumps = True
        settings.random_big_rock_clump_count = 40
        settings.random_big_rock_mean_rocks_per_clump = 12.0
        settings.random_big_rock_clump_sigma_m = 6.0
        settings.random_big_rock_min_diameter_meters = 1.5
        settings.random_big_rock_max_diameter_meters = 20.0
        settings.random_big_rock_exclude_crater_min_diameter_meters = 60.0
        settings.random_big_rock_exclude_crater_radius_multiplier = 1.0
        settings.background_material = "HighlandAnorthosite"
        settings.floor_material = "RegolithCovered"
        settings.old_ejecta_material = "MixedBreccia"
        settings.fresh_ejecta_material = "FreshEjecta"

    elif p == "apollo_17":
        settings.max_rocks = 60000                 # Engine/performance cap; not a paper value.
        settings.min_rock_diameter = 0.26          # Watkins et al. 2019: boulder >25.6 cm.
        settings.max_rock_diameter_cap = 8.6       # Watkins et al. 2019: Camelot largest boulder 8.6 m.
        settings.power_law_exponent = 6.8          # Watkins et al. 2019: Camelot slope b=6.8.
        settings.min_source_crater_diameter_meters = 3.0 # Procedural filter.
        settings.max_source_crater_degrade = 1.0  # Watkins: Camelot ~105 Ma still has boulders; degrade mapping procedural.
        settings.background_density_per_m2 = 0.12  # Apollo 17 analog from Li & Wu low landing-site k~1-1.5%; conversion procedural.
        settings.background_fraction_cap = 0.45    # Procedural cap.
        settings.background_clump_fraction = 0.08  # Procedural patchiness for Taurus-Littrow/Central Cluster.
        settings.background_cluster_sigma_m = 3.0  # Procedural.
        settings.boulder_diameter_scale = 0.33     # Tuned from Camelot 605 m crater, largest boulder 8.6 m.
        settings.crater_boulder_density_scale = 0.040   # Count-calibrated for visible crater-owned rocks at 500 m map scale; Camelot radial/size behavior unchanged.
        settings.crater_count_exponent = 1.30      # Procedural.
        settings.freshness_gamma = 1.4             # Procedural Tycho-reworked/degraded surface mapping.
        settings.freshness_floor = 0.005           # Procedural.
        settings.max_rocks_per_crater = 400        # Watkins et al. 2019: Camelot 351 boulders.
        settings.interior_fraction = 0.08; settings.rim_fraction = 0.55; settings.proximal_fraction = 0.32; settings.distal_fraction = 0.05 # Watkins Camelot compact count; fractions procedural.
        settings.interior_r_min = 0.25; settings.interior_r_max = 0.85; settings.rim_r_min = 0.85; settings.rim_r_max = 1.20 # Procedural.
        settings.proximal_r_min = 1.20; settings.proximal_r_max = 2.00; settings.distal_r_min = 2.00; settings.distal_r_max = 3.00 # Camelot kept compact due Central Cluster contamination.
        settings.interior_distance_power = 1.2; settings.rim_distance_power = 1.5; settings.proximal_distance_power = 2.1; settings.distal_distance_power = 3.2 # Procedural.
        settings.distance_size_decay = 1.8         # Watkins trend; exact exponent procedural.
        settings.interior_size_multiplier = 0.35; settings.rim_size_multiplier = 1.25; settings.proximal_size_multiplier = 0.75; settings.distal_size_multiplier = 0.20 # Procedural.
        settings.crater_clump_fraction = 0.70; settings.mean_cluster_size = 8.0; settings.cluster_sigma_m = 1.8; settings.cluster_zone_bias = "rim_proximal" # Procedural.
        settings.enable_random_big_rock_clumps = False # Procedural Central Cluster/local boulder patches.
        settings.random_big_rock_clump_count = 6; settings.random_big_rock_mean_rocks_per_clump = 4.0; settings.random_big_rock_clump_sigma_m = 4.0 # Procedural.
        settings.random_big_rock_min_diameter_meters = 1.5; settings.random_big_rock_max_diameter_meters = 8.6 # Watkins Camelot largest 8.6 m.
        settings.random_big_rock_exclude_crater_min_diameter_meters = 60.0; settings.random_big_rock_exclude_crater_radius_multiplier = 1.0 # Procedural.
        settings.background_material = "MixedBreccia"; settings.floor_material = "RegolithCovered"; settings.old_ejecta_material = "MixedBreccia"; settings.fresh_ejecta_material = "FreshEjecta"

    elif p == "new_fresh_zone":
        settings.max_rocks = 100000                # Engine/performance cap; not a paper value.
        settings.min_rock_diameter = 0.26          # Watkins et al. 2019: boulder >25.6 cm.
        settings.max_rock_diameter_cap = 14.2      # Watkins: South Ray largest 14.2 m; North Ray 22.1 m extreme.
        settings.power_law_exponent = 5.3          # Watkins et al. 2019: South Ray slope b=5.3.
        settings.min_source_crater_diameter_meters = 3.0 # Procedural; fresh terrain lets smaller craters retain blocks.
        settings.max_source_crater_degrade = 0.45  # Watkins young/fresh retention; degrade mapping procedural.
        settings.background_density_per_m2 = 0.70  # Derived from Li & Wu fresh crater k~0.058-0.061; count conversion procedural.
        settings.background_fraction_cap = 0.25    # Procedural cap.
        settings.background_clump_fraction = 0.15  # Procedural patchy fresh ejecta.
        settings.background_cluster_sigma_m = 2.0  # Procedural.
        settings.boulder_diameter_scale = 0.55     # Tuned from Watkins South Ray/North Ray largest boulders.
        settings.crater_boulder_density_scale = 0.0100   # Count-calibrated for visible crater-owned rocks at 500 m map scale; South Ray/Cone radial/size behavior unchanged.
        settings.crater_count_exponent = 1.35      # Procedural.
        settings.freshness_gamma = 1.5             # Procedural; fresh terrain preserves rocks even with some degradation.
        settings.freshness_floor = 0.05            # Procedural.
        settings.max_rocks_per_crater = 5200       # Watkins: South Ray 3109, Cone 2102; below North Ray 18173 for performance.
        settings.interior_fraction = 0.04; settings.rim_fraction = 0.38; settings.proximal_fraction = 0.45; settings.distal_fraction = 0.13 # Watkins radial trends, fractions procedural.
        settings.interior_r_min = 0.20; settings.interior_r_max = 0.85; settings.rim_r_min = 0.85; settings.rim_r_max = 1.20 # Procedural.
        settings.proximal_r_min = 1.20; settings.proximal_r_max = 3.00; settings.distal_r_min = 3.00; settings.distal_r_max = 8.00 # Watkins: ~2-4 radii large boulders; some farther.
        settings.interior_distance_power = 1.2; settings.rim_distance_power = 1.4; settings.proximal_distance_power = 1.8; settings.distal_distance_power = 2.4 # Procedural.
        settings.distance_size_decay = 1.4         # Watkins/Bart trend; exact exponent procedural.
        settings.interior_size_multiplier = 0.30; settings.rim_size_multiplier = 1.35; settings.proximal_size_multiplier = 0.95; settings.distal_size_multiplier = 0.35 # Procedural.
        settings.crater_clump_fraction = 0.75; settings.mean_cluster_size = 10.0; settings.cluster_sigma_m = 2.5; settings.cluster_zone_bias = "all_ejecta" # Procedural fresh blocky ejecta patchiness.
        settings.enable_random_big_rock_clumps = False # Procedural fresh blocky ejecta patches.
        settings.random_big_rock_clump_count = 14; settings.random_big_rock_mean_rocks_per_clump = 6.0; settings.random_big_rock_clump_sigma_m = 6.0 # Procedural.
        settings.random_big_rock_min_diameter_meters = 1.5; settings.random_big_rock_max_diameter_meters = 14.2 # Watkins South Ray largest 14.2 m; North Ray 22.1 m extreme.
        settings.random_big_rock_exclude_crater_min_diameter_meters = 60.0; settings.random_big_rock_exclude_crater_radius_multiplier = 1.0 # Procedural.
        settings.background_material = "FreshEjecta"; settings.floor_material = "FreshEjecta"; settings.old_ejecta_material = "MixedBreccia"; settings.fresh_ejecta_material = "FreshEjecta"

    else:  # default / generic lunar rock placement
        settings.rock_profile = "default"
        settings.max_rocks = 50000                 # Engine/performance cap; not a paper value.
        settings.min_rock_diameter = 0.26          # Watkins et al. 2019: boulder >25.6 cm.
        settings.max_rock_diameter_cap = 6.0       # Practical default; paper values vary by crater.
        settings.power_law_exponent = 4.7          # Watkins et al. 2019: Surveyor slope b=4.7 generic default.
        settings.min_source_crater_diameter_meters = 10.0 # Procedural filter.
        settings.max_source_crater_degrade = 0.70  # Watkins age/retention trend; degrade mapping procedural.
        settings.background_density_per_m2 = 0.05  # Li & Wu 2018 k~0.004 converted procedurally to count density.
        settings.background_fraction_cap = 0.50    # Procedural cap.
        settings.background_clump_fraction = 0.04; settings.background_cluster_sigma_m = 3.0 # Procedural.
        settings.boulder_diameter_scale = 0.25     # Tuned scale on Bart & Melosh 2010 raw law.
        settings.crater_boulder_density_scale = 0.0040   # Count-calibrated for visible crater-owned rocks at 500 m map scale; Watkins/Bart laws unchanged.
        settings.crater_count_exponent = 1.35; settings.freshness_gamma = 2.0; settings.freshness_floor = 0.01 # Procedural.
        settings.max_rocks_per_crater = 500        # Generic cap.
        settings.interior_fraction = 0.05; settings.rim_fraction = 0.42; settings.proximal_fraction = 0.43; settings.distal_fraction = 0.10 # Watkins radial trend, fractions procedural.
        settings.interior_r_min = 0.25; settings.interior_r_max = 0.85; settings.rim_r_min = 0.85; settings.rim_r_max = 1.20 # Procedural.
        settings.proximal_r_min = 1.20; settings.proximal_r_max = 2.50; settings.distal_r_min = 2.50; settings.distal_r_max = 6.00 # Watkins/Bart trend, practical defaults.
        settings.interior_distance_power = 1.2; settings.rim_distance_power = 1.7; settings.proximal_distance_power = 2.0; settings.distal_distance_power = 2.6 # Procedural.
        settings.distance_size_decay = 1.5; settings.interior_size_multiplier = 0.30; settings.rim_size_multiplier = 1.20; settings.proximal_size_multiplier = 0.85; settings.distal_size_multiplier = 0.35 # Procedural.
        settings.crater_clump_fraction = 0.55; settings.mean_cluster_size = 7.0; settings.cluster_sigma_m = 1.8; settings.cluster_zone_bias = "rim_proximal" # Procedural.
        settings.enable_random_big_rock_clumps = False # Procedural.
        settings.random_big_rock_clump_count = 4; settings.random_big_rock_mean_rocks_per_clump = 4.0; settings.random_big_rock_clump_sigma_m = 4.0 # Procedural.
        settings.random_big_rock_min_diameter_meters = 1.5; settings.random_big_rock_max_diameter_meters = 6.0 # Procedural default.
        settings.random_big_rock_exclude_crater_min_diameter_meters = 60.0; settings.random_big_rock_exclude_crater_radius_multiplier = 1.0 # Procedural.
        settings.background_material = "RegolithCovered"; settings.floor_material = "RegolithCovered"; settings.old_ejecta_material = "MixedBreccia"; settings.fresh_ejecta_material = "FreshEjecta"


# -----------------------------------------------------------------------------
# Rock generator, ported from FRockFieldGenerator
# -----------------------------------------------------------------------------


class RockFieldGenerator:
    def __init__(self, settings: RockGenSettings, craters: List[CraterInfo]) -> None:
        self.s = settings
        self.craters = craters
        self.rng = FRandomStreamLike(settings.seed)
        self.warnings: List[str] = []

    def region(self) -> Tuple[float, float, float, float]:
        sx = self.s.map_size_meters_x
        sy = self.s.map_size_meters_y
        return (-0.5 * sx, -0.5 * sy, 0.5 * sx, 0.5 * sy)

    def generate_rocks_uniform(self) -> List[RockInstance]:
        rocks: List[RockInstance] = []
        self.generate_crater_owned_rocks(rocks)
        self.generate_background_rocks(rocks)
        self.generate_random_big_rock_clumps(rocks)
        for i, rock in enumerate(rocks):
            rock.instance_id = i
        return rocks

    def should_use_crater(self, crater: CraterInfo) -> bool:
        return (
            crater.diameter_m >= max(0.0, self.s.min_source_crater_diameter_meters)
            and clamp(crater.degrade, 0.0, 1.0) <= clamp(self.s.max_source_crater_degrade, 0.0, 1.0)
        )

    def compute_freshness_factor(self, crater: CraterInfo) -> float:
        freshness = 1.0 - clamp(crater.degrade, 0.0, 1.0)
        return max(self.s.freshness_floor, freshness ** max(0.01, self.s.freshness_gamma))

    def bm_max_boulder_diameter(self, crater: CraterInfo) -> float:
        raw = self.s.bm_max_diameter_a * safe_power(crater.diameter_m, self.s.bm_max_diameter_b)
        return clamp(raw * self.s.boulder_diameter_scale, self.s.min_rock_diameter, self.s.max_rock_diameter_cap)

    def bm_median_boulder_diameter(self, crater: CraterInfo) -> float:
        raw = self.s.bm_median_diameter_a * safe_power(crater.diameter_m, self.s.bm_median_diameter_b)
        return clamp(raw * self.s.boulder_diameter_scale, self.s.min_rock_diameter, self.s.max_rock_diameter_cap)

    def bm_max_boulder_distance(self, crater: CraterInfo) -> float:
        raw = self.s.bm_max_distance_a * safe_power(crater.diameter_m, self.s.bm_max_distance_b)
        return max(self.s.rim_r_max * crater.radius_m, raw * self.s.boulder_distance_scale)

    def distance_limited_outer_norm(self, crater: CraterInfo, requested_outer_norm: float) -> float:
        r = max(0.001, crater.radius_m)
        max_norm_from_scaling = self.bm_max_boulder_distance(crater) / r
        return max(1.05, min(min(requested_outer_norm, max_norm_from_scaling), self.s.distal_r_max))

    def estimate_crater_rock_count(self, crater: CraterInfo) -> int:
        if not self.should_use_crater(crater):
            return 0
        fresh = self.compute_freshness_factor(crater)
        r = crater.radius_m
        ejecta_outer = self.distance_limited_outer_norm(crater, self.s.distal_r_max)
        area = PI * max(0.0, (ejecta_outer * r) ** 2 - (0.85 * r) ** 2)
        count = (
            max(0.0, self.s.crater_boulder_density_scale)
            * area
            * fresh
            * safe_power(max(0.1, crater.diameter_m / 50.0), self.s.crater_count_exponent - 1.0)
        )
        return int(clamp(round_to_int(count), 0, max(0, self.s.max_rocks_per_crater)))

    def classify_size(self, diameter_m: float) -> str:
        d = max(0.0, diameter_m)
        if d < self.s.pebble_max_diameter:
            return "Pebble"
        if d < self.s.small_max_diameter:
            return "Small"
        if d < self.s.medium_max_diameter:
            return "Medium"
        if d < self.s.large_max_diameter:
            return "Large"
        return "Boulder"

    def choose_material(self, zone: str, crater: Optional[CraterInfo]) -> str:
        # Preset-specific material choices mirror the updated C++ generator.
        if zone == "Background" or crater is None:
            return self.s.background_material
        if zone == "CraterFloor":
            return self.s.floor_material
        if zone == "DistalEjecta":
            return self.s.old_ejecta_material
        if zone in {"Rim", "ProximalEjecta"}:
            return self.s.fresh_ejecta_material if crater.degrade < 0.35 else self.s.old_ejecta_material
        return self.s.background_material

    def choose_zone(self) -> str:
        w0 = max(0.0, self.s.interior_fraction)
        w1 = max(0.0, self.s.rim_fraction)
        w2 = max(0.0, self.s.proximal_fraction)
        w3 = max(0.0, self.s.distal_fraction)
        total = w0 + w1 + w2 + w3
        if total <= KINDA_SMALL_NUMBER:
            return "ProximalEjecta"
        u = self.rng.frand_range(0.0, total)
        if u < w0:
            return "CraterFloor"
        if u < w0 + w1:
            return "Rim"
        if u < w0 + w1 + w2:
            return "ProximalEjecta"
        return "DistalEjecta"

    def choose_cluster_zone(self) -> str:
        mode = str(self.s.cluster_zone_bias).lower()
        if mode == "rim":
            return "Rim"
        if mode == "proximal":
            return "ProximalEjecta"
        if mode == "all_ejecta":
            u = self.rng.frand()
            if u < 0.35:
                return "Rim"
            if u < 0.80:
                return "ProximalEjecta"
            return "DistalEjecta"
        return "Rim" if self.rng.frand() < 0.55 else "ProximalEjecta"

    def zone_bounds(self, crater: CraterInfo, zone: str) -> Tuple[float, float, float]:
        if zone == "CraterFloor":
            return self.s.interior_r_min, self.s.interior_r_max, self.s.interior_distance_power
        if zone == "Rim":
            return self.s.rim_r_min, self.s.rim_r_max, self.s.rim_distance_power
        if zone == "ProximalEjecta":
            rmax = max(self.s.proximal_r_min + 0.05, self.distance_limited_outer_norm(crater, self.s.proximal_r_max))
            return self.s.proximal_r_min, rmax, self.s.proximal_distance_power
        if zone == "DistalEjecta":
            rmax = max(self.s.distal_r_min + 0.05, self.distance_limited_outer_norm(crater, self.s.distal_r_max))
            return self.s.distal_r_min, rmax, self.s.distal_distance_power
        return 1.0, 1.0, 1.0

    def sample_normalized_radius_in_zone(self, crater: CraterInfo, zone: str) -> float:
        r0, r1, power = self.zone_bounds(crater, zone)
        if r1 <= r0:
            r1 = r0 + 0.05
        u = self.rng.frand()
        return r0 + (r1 - r0) * (u ** max(0.05, power))

    @staticmethod
    def point_from_crater_polar(crater: CraterInfo, r_norm: float, theta: float) -> Tuple[float, float]:
        r = crater.radius_m
        return (crater.x_m + r_norm * r * math.cos(theta), crater.y_m + r_norm * r * math.sin(theta))

    def is_inside_map(self, pos: Tuple[float, float]) -> bool:
        return (
            -0.5 * self.s.map_size_meters_x <= pos[0] <= 0.5 * self.s.map_size_meters_x
            and -0.5 * self.s.map_size_meters_y <= pos[1] <= 0.5 * self.s.map_size_meters_y
        )

    def is_inside_excluded_big_crater(self, pos: Tuple[float, float]) -> bool:
        min_excluded_d = max(0.0, self.s.random_big_rock_exclude_crater_min_diameter_meters)
        radius_multiplier = max(0.0, self.s.random_big_rock_exclude_crater_radius_multiplier)
        if min_excluded_d <= 0.0 or radius_multiplier <= 0.0:
            return False
        for crater in self.craters:
            if crater.diameter_m < min_excluded_d:
                continue
            if distance(pos, crater.center) <= crater.radius_m * radius_multiplier:
                return True
        return False

    def zone_size_multiplier(self, zone: str) -> float:
        if zone == "Rim":
            return self.s.rim_size_multiplier
        if zone == "ProximalEjecta":
            return self.s.proximal_size_multiplier
        if zone == "DistalEjecta":
            return self.s.distal_size_multiplier
        if zone == "CraterFloor":
            return self.s.interior_size_multiplier
        return 0.35

    def local_diameter_limits(self, crater: CraterInfo, zone: str, r_norm: float) -> Tuple[float, float]:
        dmax_crater = self.bm_max_boulder_diameter(crater)
        dmed_crater = self.bm_median_boulder_diameter(crater)
        decay = max(0.08, max(1.0e-6, r_norm) ** (-max(0.0, self.s.distance_size_decay)))
        dmax = dmax_crater * decay * self.zone_size_multiplier(zone)
        dmax *= 0.35 + 0.65 * self.compute_freshness_factor(crater)
        dmin = self.s.min_rock_diameter
        out_dmax = clamp(dmax, dmin * 1.05, self.s.max_rock_diameter_cap)
        if zone in {"Rim", "ProximalEjecta"}:
            out_dmax = max(out_dmax, min(self.s.max_rock_diameter_cap, dmed_crater))
        return dmin, out_dmax

    def local_slope_proxy_for_offline(self, r_norm: float, zone: str, crater: Optional[CraterInfo]) -> float:
        if crater is None:
            return 0.0
        freshness = 1.0 - clamp(crater.degrade, 0.0, 1.0)
        scale = (0.4 + 0.6 * freshness) * min(1.0, crater.diameter_m / 80.0)
        if zone == "Rim":
            return 18.0 * scale
        if zone == "CraterFloor":
            return 5.0 * scale
        if zone == "ProximalEjecta":
            return 8.0 * scale * math.exp(-0.5 * max(0.0, r_norm - 1.0))
        if zone == "DistalEjecta":
            return 3.0 * scale
        return 0.0

    def zone_from_normalized_radius(self, r_norm: float) -> str:
        if r_norm < self.s.interior_r_max:
            return "CraterFloor"
        if r_norm < self.s.rim_r_max:
            return "Rim"
        if r_norm < self.s.proximal_r_max:
            return "ProximalEjecta"
        if r_norm < self.s.distal_r_max:
            return "DistalEjecta"
        return "Background"

    def make_rock(
        self,
        pos: Tuple[float, float],
        diameter_m: float,
        crater: Optional[CraterInfo],
        crater_index: int,
        zone: str,
        r_norm: float,
        source_type: str,
        clump_id: int,
        local_density_per_m2: float,
        acceptance_probability: float,
        instance_id: int,
    ) -> RockInstance:
        # Use a separate deterministic stream for permanent Unreal placement
        # values so adding yaw, tilt, and burial does not alter rock generation.
        placement_seed = (
            int(self.s.seed)
            ^ ((int(instance_id) + 1) * 0x9E3779B9)
        ) & 0xFFFFFFFF
        placement_rng = FRandomStreamLike(placement_seed)

        max_tilt_degrees = clamp(
            float(self.s.max_random_tilt_degrees),
            0.0,
            90.0,
        )
        burial_min = clamp(
            min(
                float(self.s.min_burial_fraction),
                float(self.s.max_burial_fraction),
            ),
            0.0,
            1.0,
        )
        burial_max = clamp(
            max(
                float(self.s.min_burial_fraction),
                float(self.s.max_burial_fraction),
            ),
            0.0,
            1.0,
        )

        return RockInstance(
            instance_id=instance_id,
            x_m=pos[0],
            y_m=pos[1],
            diameter_m=diameter_m,
            size_class=self.classify_size(diameter_m),
            material_type=self.choose_material(zone, crater),
            crater_zone=zone,
            dominant_crater_index=(
                crater_index if crater is not None else -1
            ),
            distance_to_dominant_crater_center_m=(
                distance(pos, crater.center)
                if crater is not None
                else -1.0
            ),
            normalized_crater_radius=(
                r_norm if crater is not None else -1.0
            ),
            local_slope_deg=self.local_slope_proxy_for_offline(
                r_norm,
                zone,
                crater,
            ),
            local_density_per_m2=local_density_per_m2,
            acceptance_probability=acceptance_probability,
            source_type=source_type,
            clump_id=clump_id,
            yaw_degrees=placement_rng.frand_range(0.0, 360.0),
            tilt_degrees=placement_rng.frand_range(
                0.0,
                max_tilt_degrees,
            ),
            tilt_axis_degrees=placement_rng.frand_range(0.0, 360.0),
            burial_fraction=placement_rng.frand_range(
                burial_min,
                burial_max,
            ),
        )

    def generate_crater_owned_rocks(self, rocks: List[RockInstance]) -> None:
        next_clump_id = 0
        for crater_index, crater in enumerate(self.craters):
            if len(rocks) >= self.s.max_rocks:
                break
            n_crater = self.estimate_crater_rock_count(crater)
            if n_crater <= 0:
                continue

            n_cluster_rocks = round_to_int(n_crater * clamp(self.s.crater_clump_fraction, 0.0, 0.95))
            n_single = max(0, n_crater - n_cluster_rocks)

            for _ in range(n_single):
                if len(rocks) >= self.s.max_rocks:
                    break
                zone = self.choose_zone()
                r_norm = self.sample_normalized_radius_in_zone(crater, zone)
                theta = self.rng.frand_range(0.0, 2.0 * PI)
                pos = self.point_from_crater_polar(crater, r_norm, theta)
                if not self.is_inside_map(pos):
                    continue
                dmin, dmax = self.local_diameter_limits(crater, zone, r_norm)
                diameter_m = sample_power_law_diameter(self.rng, dmin, dmax, self.s.power_law_exponent)
                density_proxy = self.s.crater_boulder_density_scale * self.compute_freshness_factor(crater)
                rocks.append(
                    self.make_rock(pos, diameter_m, crater, crater_index, zone, r_norm, "crater_ejecta_single", -1, density_proxy, 1.0, len(rocks))
                )

            cluster_rocks_added = 0
            attempts = 0
            max_attempts = max(1000, n_cluster_rocks * 200)
            while cluster_rocks_added < n_cluster_rocks and len(rocks) < self.s.max_rocks and attempts < max_attempts:
                attempts += 1
                parent_zone = self.choose_cluster_zone()
                parent_r_norm = self.sample_normalized_radius_in_zone(crater, parent_zone)
                parent_theta = self.rng.frand_range(0.0, 2.0 * PI)
                parent_pos = self.point_from_crater_polar(crater, parent_r_norm, parent_theta)
                if not self.is_inside_map(parent_pos):
                    continue

                num_children = max(1, sample_poisson(self.rng, max(0.1, self.s.mean_cluster_size)))
                clump_id = next_clump_id
                next_clump_id += 1

                for _child in range(num_children):
                    if cluster_rocks_added >= n_cluster_rocks or len(rocks) >= self.s.max_rocks:
                        break
                    pos = (
                        parent_pos[0] + sample_gaussian(self.rng, 0.0, self.s.cluster_sigma_m),
                        parent_pos[1] + sample_gaussian(self.rng, 0.0, self.s.cluster_sigma_m),
                    )
                    if not self.is_inside_map(pos):
                        continue
                    dist = distance(pos, crater.center)
                    r_norm = dist / max(0.001, crater.radius_m)
                    child_zone = self.zone_from_normalized_radius(r_norm)
                    if child_zone == "Background":
                        continue
                    dmin, dmax = self.local_diameter_limits(crater, child_zone, r_norm)
                    diameter_m = sample_power_law_diameter(self.rng, dmin, dmax, self.s.power_law_exponent)
                    density_proxy = self.s.crater_boulder_density_scale * self.compute_freshness_factor(crater) * 1.5
                    rocks.append(
                        self.make_rock(pos, diameter_m, crater, crater_index, child_zone, r_norm, "crater_ejecta_clump", clump_id, density_proxy, 1.0, len(rocks))
                    )
                    cluster_rocks_added += 1

            if cluster_rocks_added < n_cluster_rocks:
                self.warnings.append(
                    f"Crater {crater_index}: only placed {cluster_rocks_added}/{n_cluster_rocks} requested clumped rocks before attempt guard."
                )

    def generate_background_rocks(self, rocks: List[RockInstance]) -> None:
        xmin, ymin, xmax, ymax = self.region()
        existing_count = len(rocks)
        region_area = max(0.0, xmax - xmin) * max(0.0, ymax - ymin)
        requested = round_to_int(region_area * max(0.0, self.s.background_density_per_m2))
        mode = str(getattr(self.s, "background_cap_mode", "unreal")).strip().lower()
        if mode == "maxrocks":
            # Analysis-only behavior: allows background rocks even when no crater-owned
            # rocks were produced.
            cap = round_to_int(max(0, self.s.max_rocks) * clamp(self.s.background_fraction_cap, 0.0, 1.0))
        else:
            # Unreal RockFieldGenerator.cpp behavior:
            # Cap = RoundToInt(Max(1, ExistingCount) * BackgroundFractionCap).
            # This keeps the field crater-first instead of allowing a background-only
            # map to hide the crater-coupled model.
            cap = round_to_int(max(1, existing_count) * clamp(self.s.background_fraction_cap, 0.0, 1.0))
        n_background = min(min(requested, cap), max(0, self.s.max_rocks - existing_count))
        if n_background <= 0:
            return

        n_cluster = round_to_int(n_background * clamp(self.s.background_clump_fraction, 0.0, 0.8))
        n_single = max(0, n_background - n_cluster)

        for _ in range(n_single):
            if len(rocks) >= self.s.max_rocks:
                break
            pos = (self.rng.frand_range(xmin, xmax), self.rng.frand_range(ymin, ymax))
            diameter_m = sample_power_law_diameter(self.rng, self.s.min_rock_diameter, min(0.75, self.s.max_rock_diameter_cap), self.s.power_law_exponent)
            rocks.append(self.make_rock(pos, diameter_m, None, -1, "Background", -1.0, "background_regolith", -1, self.s.background_density_per_m2, 1.0, len(rocks)))

        cluster_added = 0
        clump_id = 100000
        attempts = 0
        max_attempts = max(1000, n_cluster * 200)
        while cluster_added < n_cluster and len(rocks) < self.s.max_rocks and attempts < max_attempts:
            attempts += 1
            parent_pos = (self.rng.frand_range(xmin, xmax), self.rng.frand_range(ymin, ymax))
            num_children = max(1, sample_poisson(self.rng, max(0.1, self.s.mean_cluster_size * 0.5)))
            this_clump_id = clump_id
            clump_id += 1
            for _child in range(num_children):
                if cluster_added >= n_cluster or len(rocks) >= self.s.max_rocks:
                    break
                pos = (
                    parent_pos[0] + sample_gaussian(self.rng, 0.0, self.s.background_cluster_sigma_m),
                    parent_pos[1] + sample_gaussian(self.rng, 0.0, self.s.background_cluster_sigma_m),
                )
                if not self.is_inside_map(pos):
                    continue
                diameter_m = sample_power_law_diameter(self.rng, self.s.min_rock_diameter, min(0.75, self.s.max_rock_diameter_cap), self.s.power_law_exponent)
                rocks.append(self.make_rock(pos, diameter_m, None, -1, "Background", -1.0, "background_clump", this_clump_id, self.s.background_density_per_m2, 1.0, len(rocks)))
                cluster_added += 1

        if cluster_added < n_cluster:
            self.warnings.append(f"Background: only placed {cluster_added}/{n_cluster} requested clumped rocks before attempt guard.")

    def generate_random_big_rock_clumps(self, rocks: List[RockInstance]) -> None:
        if not self.s.enable_random_big_rock_clumps or len(rocks) >= self.s.max_rocks:
            return
        xmin, ymin, xmax, ymax = self.region()
        clump_count = max(0, int(self.s.random_big_rock_clump_count))
        if clump_count <= 0:
            return
        mean_rocks_per_clump = max(0.1, self.s.random_big_rock_mean_rocks_per_clump)
        cluster_sigma = max(0.0, self.s.random_big_rock_clump_sigma_m)
        dmin = max(self.s.min_rock_diameter, self.s.random_big_rock_min_diameter_meters)
        dmax = max(dmin + 0.001, min(self.s.random_big_rock_max_diameter_meters, self.s.max_rock_diameter_cap))
        max_parent_attempts = max(32, clump_count * 64)
        parent_attempts = 0
        clumps_created = 0
        next_clump_id = 200000

        while clumps_created < clump_count and len(rocks) < self.s.max_rocks and parent_attempts < max_parent_attempts:
            parent_attempts += 1
            parent_pos = (self.rng.frand_range(xmin, xmax), self.rng.frand_range(ymin, ymax))
            if self.is_inside_excluded_big_crater(parent_pos):
                continue
            num_children = max(1, sample_poisson(self.rng, mean_rocks_per_clump))
            this_clump_id = next_clump_id
            next_clump_id += 1
            added_this_clump = 0
            for _child in range(num_children):
                if len(rocks) >= self.s.max_rocks:
                    break
                pos = (
                    parent_pos[0] + sample_gaussian(self.rng, 0.0, cluster_sigma),
                    parent_pos[1] + sample_gaussian(self.rng, 0.0, cluster_sigma),
                )
                if not self.is_inside_map(pos) or self.is_inside_excluded_big_crater(pos):
                    continue
                diameter_m = sample_power_law_diameter(self.rng, dmin, dmax, self.s.power_law_exponent)
                rocks.append(self.make_rock(pos, diameter_m, None, -1, "Background", -1.0, "random_big_rock_clump", this_clump_id, 0.0, 1.0, len(rocks)))
                added_this_clump += 1
            if added_this_clump > 0:
                clumps_created += 1

        if clumps_created < clump_count:
            self.warnings.append(f"Random big-rock clumps: created {clumps_created}/{clump_count} before attempt limit.")


# -----------------------------------------------------------------------------
# Input / output helpers
# -----------------------------------------------------------------------------


def terrain_name_from_crater_json(path: Path) -> str:
    if path.name == "craters.json":
        return path.parent.name
    stem = path.stem
    suffix = "_rockfield_craters"
    return stem[:-len(suffix)] if stem.endswith(suffix) else stem


def read_json(path: Path) -> Dict[str, Any]:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def load_metadata(path: Optional[Path]) -> Dict[str, Any]:
    if path is None or not path.exists():
        return {}
    try:
        return read_json(path)
    except Exception as exc:
        print(f"Warning: could not read metadata {path}: {exc}", file=sys.stderr)
        return {}


def apply_metadata_to_settings(settings: RockGenSettings, metadata: Dict[str, Any]) -> None:
    if not metadata:
        return
    if "seed" in metadata:
        try:
            settings.seed = int(metadata["seed"])
        except Exception:
            pass
    # Most of your generator metadata uses map_size_m for square maps.
    if "map_size_m" in metadata:
        try:
            size = float(metadata["map_size_m"])
            settings.map_size_meters_x = size
            settings.map_size_meters_y = size
        except Exception:
            pass
    if "map_size_x_m" in metadata:
        try:
            settings.map_size_meters_x = float(metadata["map_size_x_m"])
        except Exception:
            pass
    if "map_size_y_m" in metadata:
        try:
            settings.map_size_meters_y = float(metadata["map_size_y_m"])
        except Exception:
            pass
    if "preset" in metadata and metadata["preset"]:
        settings.rock_profile = str(metadata["preset"])


def normalize_key(key: str) -> str:
    out = []
    for ch in key.strip():
        if ch.isupper():
            if out:
                out.append("_")
            out.append(ch.lower())
        elif ch in {"-", " ", ".", "|"}:
            out.append("_")
        else:
            out.append(ch.lower())
    return "".join(out).replace("__", "_")


def apply_settings_json(settings: RockGenSettings, path: Optional[Path]) -> None:
    if path is None:
        return
    data = read_json(path)
    if "settings" in data and isinstance(data["settings"], dict):
        data = data["settings"]
    valid = {f.name for f in fields(settings)}
    aliases = {
        "seed": "seed",
        "map_size_meters_x": "map_size_meters_x",
        "map_size_meters_y": "map_size_meters_y",
        "map_size_x_m": "map_size_meters_x",
        "map_size_y_m": "map_size_meters_y",
        "map_size_m": "map_size_meters_x",  # y handled below
        "max_rocks": "max_rocks",
        "min_rock_diameter_meters": "min_rock_diameter",
        "max_rock_diameter_meters": "max_rock_diameter_cap",
        "enable_random_big_rock_clumps": "enable_random_big_rock_clumps",
    }
    for raw_key, value in data.items():
        key = normalize_key(raw_key)
        target = aliases.get(key, key)
        if key == "map_size_m":
            try:
                settings.map_size_meters_x = float(value)
                settings.map_size_meters_y = float(value)
            except Exception:
                pass
            continue
        if target in valid:
            current = getattr(settings, target)
            try:
                if isinstance(current, bool):
                    if isinstance(value, str):
                        value = value.strip().lower() in {"1", "true", "yes", "on"}
                    else:
                        value = bool(value)
                elif isinstance(current, int) and not isinstance(current, bool):
                    value = int(value)
                elif isinstance(current, float):
                    value = float(value)
                else:
                    value = str(value)
                setattr(settings, target, value)
            except Exception:
                print(f"Warning: ignored invalid settings value {raw_key}={value!r}", file=sys.stderr)


def load_craters_from_json(path: Path, map_size_x: float, map_size_y: float, coordinates_are_centered: bool) -> List[CraterInfo]:
    root = read_json(path)
    raw_craters = root.get("craters")
    if not isinstance(raw_craters, list):
        raise ValueError(f"Crater JSON missing 'craters' array: {path}")

    craters: List[CraterInfo] = []
    for obj in raw_craters:
        if not isinstance(obj, dict):
            continue
        try:
            x = float(obj["x_m"])
            y = float(obj["y_m"])
            d = float(obj["diameter_m"])
        except Exception:
            continue
        if not coordinates_are_centered:
            x -= 0.5 * map_size_x
            y -= 0.5 * map_size_y
        degrade = obj.get("degrade", obj.get("degradation", 0.0))
        try:
            degrade = clamp(float(degrade), 0.0, 1.0)
        except Exception:
            degrade = 0.0
        morph = str(obj.get("morph", "NORMAL") or "NORMAL")
        craters.append(CraterInfo(x_m=x, y_m=y, diameter_m=d, degrade=degrade, morph=morph))
    return craters


#def rock_to_position_json(rock: RockInstance) -> Dict[str, Any]:
#    return {
#        "instance_id": rock.instance_id,
#        "x_m": rock.x_m,
#        "y_m": rock.y_m,
#        "diameter_m": rock.diameter_m,
#        "dominant_crater_index": rock.dominant_crater_index,
#    }


def rock_to_full_json(rock: RockInstance) -> Dict[str, Any]:
    return {
        "instance_id": rock.instance_id,
        "x_m": rock.x_m,
        "y_m": rock.y_m,
        "diameter_m": rock.diameter_m,
        "size_class": rock.size_class,
        "material_type": rock.material_type,
        "crater_zone": rock.crater_zone,
        "dominant_crater_index": rock.dominant_crater_index,
        "distance_to_dominant_crater_center_m": rock.distance_to_dominant_crater_center_m,
        "normalized_crater_radius": rock.normalized_crater_radius,
        "local_slope_deg": rock.local_slope_deg,
        "local_density_per_m2": rock.local_density_per_m2,
        "acceptance_probability": rock.acceptance_probability,
        "source_type": rock.source_type,
        "clump_id": rock.clump_id,
        "yaw_degrees": rock.yaw_degrees,
        "tilt_degrees": rock.tilt_degrees,
        "tilt_axis_degrees": rock.tilt_axis_degrees,
        "burial_fraction": rock.burial_fraction,
    }


def write_outputs(
    out_dir: Path,
    terrain_name: str,
    crater_json: Path,
    metadata_path: Optional[Path],
    settings: RockGenSettings,
    craters: List[CraterInfo],
    rocks: List[RockInstance],
    warnings: Iterable[str],
    write_offline_instances: bool = False,
) -> None:
    del craters
    del warnings
    del write_offline_instances

    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    rockfield_path = out_dir / f"{terrain_name}_unreal_rockfield.json"

    with rockfield_path.open("w", encoding="utf-8") as f:
        json.dump(
            {
                "format": "MoonSimOfflineRockField",
                "version": 1,
                "units": "meters",
                "coordinate_frame": "centered_map_meters",
                "terrain_name": terrain_name,
                "source_crater_json": str(crater_json),
                "source_metadata_json": (
                    str(metadata_path) if metadata_path else None
                ),
                "map_size_x_m": settings.map_size_meters_x,
                "map_size_y_m": settings.map_size_meters_y,
                "rock_count": len(rocks),
                "rocks": [rock_to_full_json(r) for r in rocks],
            },
            f,
            indent=2,
        )

    print(f"Wrote Unreal rockfield: {rockfield_path}")

def generate_one(
    crater_json: Path,
    out_dir: Path,
    base_settings: RockGenSettings,
    metadata_path: Optional[Path],
    coords_centered: bool,
    write_offline_instances: bool,
    cli_overrides: Optional[Dict[str, Any]] = None,
) -> Dict[str, Any]:
    terrain_name = terrain_name_from_crater_json(crater_json)
    settings = RockGenSettings(**asdict(base_settings))
    metadata = load_metadata(metadata_path)
    apply_metadata_to_settings(settings, metadata)

    # CLI profile/use-scientific flags choose the preset and should beat metadata.
    overrides = cli_overrides or {}
    if "rock_profile" in overrides:
        settings.rock_profile = str(overrides["rock_profile"])
    if "use_scientific_preset_values" in overrides:
        settings.use_scientific_preset_values = bool(overrides["use_scientific_preset_values"])

    if settings.use_scientific_preset_values:
        apply_scientific_rock_preset(settings, settings.rock_profile)

    # Apply all CLI overrides after the preset so command-line caps/densities still win.
    apply_cli_overrides(settings, overrides)
    settings.rock_profile = canonical_rock_profile(settings.rock_profile) if settings.use_scientific_preset_values else settings.rock_profile

    craters = load_craters_from_json(crater_json, settings.map_size_meters_x, settings.map_size_meters_y, coords_centered)
    gen = RockFieldGenerator(settings, craters)
    rocks = gen.generate_rocks_uniform()
    write_outputs(out_dir, terrain_name, crater_json, metadata_path, settings, craters, rocks, gen.warnings, write_offline_instances)
    return {
        "terrain_name": terrain_name,
        "out_dir": str(out_dir),
        "crater_json": str(crater_json),
        "metadata_json": str(metadata_path) if metadata_path else None,
        "rock_count": len(rocks),
        "crater_count": len(craters),
        "warnings": gen.warnings,
    }


def collect_crater_jsons(args: argparse.Namespace) -> List[Path]:
    if args.crater_json:
        return [Path(args.crater_json)]
    if not args.crater_json_dir:
        raise SystemExit("Provide either --crater-json or --crater-json-dir")
    root = Path(args.crater_json_dir)
    paths = sorted(root.glob(args.pattern))
    if not paths:
        raise SystemExit(f"No crater JSONs found in {root} matching {args.pattern!r}")
    return paths


def metadata_for(crater_json: Path, args: argparse.Namespace) -> Optional[Path]:
    if args.metadata:
        return Path(args.metadata)
    if not args.metadata_dir:
        return None
    base = terrain_name_from_crater_json(crater_json)
    candidate = Path(args.metadata_dir) / f"{base}.json"
    return candidate if candidate.exists() else None


def build_base_settings(args: argparse.Namespace) -> RockGenSettings:
    settings = RockGenSettings()
    if args.settings_json:
        apply_settings_json(settings, Path(args.settings_json))

    # Command-line flags override settings JSON.
    if args.seed is not None:
        settings.seed = int(args.seed)
    if args.map_size_m is not None:
        settings.map_size_meters_x = float(args.map_size_m)
        settings.map_size_meters_y = float(args.map_size_m)
    if args.map_size_x_m is not None:
        settings.map_size_meters_x = float(args.map_size_x_m)
    if args.map_size_y_m is not None:
        settings.map_size_meters_y = float(args.map_size_y_m)
    if args.max_rocks is not None:
        settings.max_rocks = int(args.max_rocks)
    if args.profile is not None:
        settings.rock_profile = args.profile
    if args.use_scientific_presets:
        settings.use_scientific_preset_values = True
    if args.no_scientific_presets:
        settings.use_scientific_preset_values = False
    if args.crater_boulder_density_scale is not None:
        settings.crater_boulder_density_scale = float(args.crater_boulder_density_scale)
    if getattr(args, "max_source_crater_degrade", None) is not None:
        settings.max_source_crater_degrade = float(args.max_source_crater_degrade)
    if getattr(args, "min_source_crater_diameter_meters", None) is not None:
        settings.min_source_crater_diameter_meters = float(args.min_source_crater_diameter_meters)
    if args.background_density_per_m2 is not None:
        settings.background_density_per_m2 = float(args.background_density_per_m2)
    if getattr(args, "background_cap_mode", None) is not None:
        settings.background_cap_mode = str(args.background_cap_mode)
    if args.enable_random_big_rock_clumps:
        settings.enable_random_big_rock_clumps = True
    if args.disable_random_big_rock_clumps:
        settings.enable_random_big_rock_clumps = False
    return settings




def build_cli_overrides(args: argparse.Namespace) -> Dict[str, Any]:
    """Collect command-line settings that should win over metadata and presets."""
    overrides: Dict[str, Any] = {}
    if args.seed is not None:
        overrides["seed"] = int(args.seed)
    if args.map_size_m is not None:
        overrides["map_size_meters_x"] = float(args.map_size_m)
        overrides["map_size_meters_y"] = float(args.map_size_m)
    if args.map_size_x_m is not None:
        overrides["map_size_meters_x"] = float(args.map_size_x_m)
    if args.map_size_y_m is not None:
        overrides["map_size_meters_y"] = float(args.map_size_y_m)
    if args.max_rocks is not None:
        overrides["max_rocks"] = int(args.max_rocks)
    if args.profile is not None:
        overrides["rock_profile"] = args.profile
    if args.use_scientific_presets:
        overrides["use_scientific_preset_values"] = True
    if args.no_scientific_presets:
        overrides["use_scientific_preset_values"] = False
    if args.crater_boulder_density_scale is not None:
        overrides["crater_boulder_density_scale"] = float(args.crater_boulder_density_scale)
    if getattr(args, "max_source_crater_degrade", None) is not None:
        overrides["max_source_crater_degrade"] = float(args.max_source_crater_degrade)
    if getattr(args, "min_source_crater_diameter_meters", None) is not None:
        overrides["min_source_crater_diameter_meters"] = float(args.min_source_crater_diameter_meters)
    if args.background_density_per_m2 is not None:
        overrides["background_density_per_m2"] = float(args.background_density_per_m2)
    if getattr(args, "background_cap_mode", None) is not None:
        overrides["background_cap_mode"] = str(args.background_cap_mode)
    if args.enable_random_big_rock_clumps:
        overrides["enable_random_big_rock_clumps"] = True
    if args.disable_random_big_rock_clumps:
        overrides["enable_random_big_rock_clumps"] = False
    return overrides


def apply_cli_overrides(settings: RockGenSettings, overrides: Dict[str, Any]) -> None:
    valid = {f.name for f in fields(settings)}
    for key, value in overrides.items():
        if key in valid:
            setattr(settings, key, value)


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate one complete MoonSim Unreal rockfield JSON per terrain.")

    inputs = parser.add_argument_group("inputs")
    inputs.add_argument("--crater-json", help="Single <terrain>_rockfield_craters.json file")
    inputs.add_argument("--crater-json-dir", help="Directory containing *_rockfield_craters.json files")
    inputs.add_argument("--pattern", default="*_rockfield_craters.json", help="Glob pattern used with --crater-json-dir")
    inputs.add_argument("--metadata", help="Metadata JSON for a single terrain; used for seed/map_size/preset when present")
    inputs.add_argument("--metadata-dir", help="Directory containing <terrain>.json metadata files")
    inputs.add_argument("--settings-json", help="Optional JSON file overriding generator settings")

    outputs = parser.add_argument_group("outputs")
    outputs.add_argument("--out-dir", help="Output directory for a single terrain")
    outputs.add_argument("--out-root", default="out/rockfields", help="Output root for batch mode; each terrain gets a subfolder")
    outputs.add_argument("--write-offline-instances", action="store_true", help=argparse.SUPPRESS)

    coords = parser.add_mutually_exclusive_group()
    coords.add_argument("--coords-centered", dest="coords_centered", action="store_true", help="Crater x_m/y_m are already centered around 0,0; default")
    coords.add_argument("--coords-top-left", dest="coords_centered", action="store_false", help="Crater x_m/y_m are 0..map_size and should be converted to centered coordinates")
    parser.set_defaults(coords_centered=True)

    settings = parser.add_argument_group("common settings overrides")
    settings.add_argument("--seed", type=int, help="Override seed. If omitted, metadata seed is used when available, otherwise 1337")
    settings.add_argument("--map-size-m", type=float, help="Square map size override in meters")
    settings.add_argument("--map-size-x-m", type=float, help="Map width override in meters")
    settings.add_argument("--map-size-y-m", type=float, help="Map height override in meters")
    settings.add_argument("--max-rocks", type=int, help="Maximum rocks to generate")
    settings.add_argument("--profile", help="Scientific rock preset/profile, e.g. default, mare_scientific, highland_scientific, apollo17_scientific, fresh_crater_scientific, custom_scientific")
    settings.add_argument("--use-scientific-presets", action="store_true", help="Apply paper-backed preset values for the selected profile; this is the default unless disabled in settings JSON")
    settings.add_argument("--no-scientific-presets", action="store_true", help="Do not apply scientific preset values; use dataclass/settings-json/CLI values directly")
    settings.add_argument("--crater-boulder-density-scale", type=float, help="Override crater boulder density scale")
    settings.add_argument("--max-source-crater-degrade", type=float, help="Override maximum crater degradation allowed to produce crater-owned rocks")
    settings.add_argument("--min-source-crater-diameter-meters", type=float, help="Override minimum crater diameter allowed to produce crater-owned rocks")
    settings.add_argument("--background-density-per-m2", type=float, help="Override background density")
    settings.add_argument("--background-cap-mode", choices=["unreal", "maxrocks"], default=None, help="Background cap behavior. Default in this balanced script is 'maxrocks' for dense paper-ready background; use 'unreal' for exact Unreal cap.")
    settings.add_argument("--enable-random-big-rock-clumps", action="store_true", help="Enable optional big-rock clump pass")
    settings.add_argument("--disable-random-big-rock-clumps", action="store_true", help="Disable optional big-rock clump pass")

    args = parser.parse_args()

    crater_jsons = collect_crater_jsons(args)
    base_settings = build_base_settings(args)
    cli_overrides = build_cli_overrides(args)

    manifest = []
    for crater_json in crater_jsons:
        if not crater_json.exists():
            raise SystemExit(f"Crater JSON not found: {crater_json}")
        terrain_name = terrain_name_from_crater_json(crater_json)
        if args.out_dir:
            out_dir = Path(args.out_dir)
            if len(crater_jsons) > 1:
                out_dir = out_dir / terrain_name
        else:
            out_dir = Path(args.out_root) / terrain_name
        meta = metadata_for(crater_json, args)
        result = generate_one(crater_json, out_dir, base_settings, meta, args.coords_centered, args.write_offline_instances, cli_overrides)
        manifest.append(result)
        print(f"Generated {result['rock_count']} rocks for {terrain_name} -> {out_dir}")
        if result["warnings"]:
            for warning in result["warnings"][:5]:
                print(f"  warning: {warning}", file=sys.stderr)
            if len(result["warnings"]) > 5:
                print(f"  warning: {len(result['warnings']) - 5} more warnings suppressed", file=sys.stderr)



if __name__ == "__main__":
    main()