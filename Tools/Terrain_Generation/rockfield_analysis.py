#!/usr/bin/env python3
"""
MoonSim rockfield analysis.

This script analyzes one generated MoonSim rockfield. It is intended for the
asset-generator GUI and for direct command-line use.

Required inputs
---------------
1. Matching heightmap metadata JSON, used for physical map dimensions.
2. Matching generated crater catalog ending in *_rockfield_craters.json.
3. At least one rockfield input:
   - a folder containing rock_candidates.json, rock_positions.json,
     rock_instances.json and/or rock_metadata.csv; or
   - an explicit generic/offline/Unreal rock JSON; or
   - explicit paths to the individual files.

Optional inputs
---------------
- Matching heightmap PNG/R16/RAW. When supplied, hillshade is used behind the
  ejecta-zone, density, and large-rock maps.
- Matching rock-settings JSON. When supplied, the exact crater-interior, rim,
  proximal-ejecta, and distal-ejecta bounds are used. Otherwise, preset-based
  defaults are inferred from the metadata and filenames.

Main outputs
------------
01_rockfield_overview.png
    Rock size distribution, crater-owned rocks by zone, nearest-neighbor
    distribution, and rock size versus normalized source-crater radius.

02_crater_ejecta_zones.png
    Source craters and dominant crater/ejecta placement zones.

03_rocks_grouped_by_source_crater.png
    Rocks grouped by stored dominant_crater_index for source craters with
    diameter D >= 5 m by default.

04_rock_density_field.png
    Smoothed rock-density field in rocks per square metre.

05_large_rock_map.png
    Large rocks drawn as true-diameter circles over the map.

rockfield_analysis.json
rockfield_metrics.csv
merged_rocks.csv
rockfield_analysis_summary.txt

Typical call
------------
python3 analyze_rockfield.py \
  --rockfield-dir "/path/to/generated/rockfield_folder" \
  --metadata "/path/to/heightmap_metadata.json" \
  --crater-json "/path/to/map_rockfield_craters.json" \
  --heightmap "/path/to/heightmap.png" \
  --rock-settings "/path/to/run_rock_settings.json" \
  --out-dir "/path/to/analysis_results"

Dependencies
------------
Python 3, numpy, Pillow, matplotlib, scipy
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import re
import sys
from collections import Counter
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Sequence, Tuple

import numpy as np
from PIL import Image

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.colors import BoundaryNorm, ListedColormap
from matplotlib.patches import Circle, Patch

try:
    from scipy.ndimage import gaussian_filter
    from scipy.spatial import cKDTree
except ImportError as exc:
    raise SystemExit(
        "This script requires scipy. Install dependencies with:\n"
        "  python3 -m pip install numpy pillow matplotlib scipy"
    ) from exc


# -----------------------------------------------------------------------------
# Display constants
# -----------------------------------------------------------------------------

ZONE_ORDER = (
    "Background",
    "CraterFloor",
    "Rim",
    "ProximalEjecta",
    "DistalEjecta",
    "Unknown",
)

ZONE_DISPLAY = {
    "Background": "Background",
    "CraterFloor": "Crater floor",
    "Rim": "Rim",
    "ProximalEjecta": "Proximal ejecta",
    "DistalEjecta": "Distal ejecta",
    "Unknown": "Unknown",
}

ZONE_COLORS = {
    "Background": "#d9d9d9",
    "CraterFloor": "#2b8cbe",
    "Rim": "#e34a33",
    "ProximalEjecta": "#fdbb84",
    "DistalEjecta": "#31a354",
    "Unknown": "#756bb1",
}

ZONE_LABEL_BY_CODE = {
    0: "Background",
    1: "Crater floor",
    2: "Rim",
    3: "Proximal ejecta",
    4: "Distal ejecta",
}

ZONE_CODE_CMAP = ListedColormap([
    ZONE_COLORS["Background"],
    ZONE_COLORS["CraterFloor"],
    ZONE_COLORS["Rim"],
    ZONE_COLORS["ProximalEjecta"],
    ZONE_COLORS["DistalEjecta"],
])
ZONE_CODE_NORM = BoundaryNorm(
    [-0.5, 0.5, 1.5, 2.5, 3.5, 4.5],
    ZONE_CODE_CMAP.N,
)

PRESET_ZONE_DEFAULTS: Dict[str, Dict[str, float]] = {
    "mare": {
        "interior_r_min": 0.30,
        "interior_r_max": 0.80,
        "rim_r_min": 0.85,
        "rim_r_max": 1.15,
        "proximal_r_min": 1.15,
        "proximal_r_max": 2.25,
        "distal_r_min": 2.25,
        "distal_r_max": 4.00,
        "min_source_crater_diameter_meters": 3.0,
    },
    "apollo17": {
        "interior_r_min": 0.25,
        "interior_r_max": 0.85,
        "rim_r_min": 0.85,
        "rim_r_max": 1.20,
        "proximal_r_min": 1.20,
        "proximal_r_max": 2.00,
        "distal_r_min": 2.00,
        "distal_r_max": 3.00,
        "min_source_crater_diameter_meters": 3.0,
    },
    "highland": {
        "interior_r_min": 0.20,
        "interior_r_max": 0.90,
        "rim_r_min": 0.85,
        "rim_r_max": 1.20,
        "proximal_r_min": 1.20,
        "proximal_r_max": 3.00,
        "distal_r_min": 3.00,
        "distal_r_max": 6.00,
        "min_source_crater_diameter_meters": 6.0,
    },
    "fresh": {
        "interior_r_min": 0.20,
        "interior_r_max": 0.85,
        "rim_r_min": 0.85,
        "rim_r_max": 1.20,
        "proximal_r_min": 1.20,
        "proximal_r_max": 3.00,
        "distal_r_min": 3.00,
        "distal_r_max": 8.00,
        "min_source_crater_diameter_meters": 3.0,
    },
}

GENERIC_ZONE_DEFAULTS = {
    "interior_r_min": 0.25,
    "interior_r_max": 0.85,
    "rim_r_min": 0.85,
    "rim_r_max": 1.20,
    "proximal_r_min": 1.20,
    "proximal_r_max": 2.50,
    "distal_r_min": 2.50,
    "distal_r_max": 4.00,
    "min_source_crater_diameter_meters": 3.0,
}


# -----------------------------------------------------------------------------
# General helpers
# -----------------------------------------------------------------------------

def ensure_dir(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)


def safe_float(value: Any, default: float = float("nan")) -> float:
    try:
        if value is None or value == "":
            return default
        return float(value)
    except (TypeError, ValueError):
        return default


def safe_int(value: Any, default: int = -1) -> int:
    try:
        if value is None or value == "":
            return default
        return int(float(value))
    except (TypeError, ValueError):
        return default


def read_json(path: Path) -> Dict[str, Any]:
    with path.open("r", encoding="utf-8") as file:
        data = json.load(file)
    if not isinstance(data, dict):
        raise ValueError(f"JSON root must be an object: {path}")
    return data


def clean_name(path_or_name: str | Path) -> str:
    text = Path(path_or_name).stem
    text = re.sub(r"_rockfield_craters$", "", text)
    text = re.sub(r"[^0-9A-Za-z_.-]+", "_", text)
    return text.strip("_") or "rockfield"


def finite_values(values: np.ndarray) -> np.ndarray:
    array = np.asarray(values, dtype=np.float64)
    return array[np.isfinite(array)]


def finite_positive(values: np.ndarray) -> np.ndarray:
    array = np.asarray(values, dtype=np.float64)
    return array[np.isfinite(array) & (array > 0)]


def array_statistics(values: np.ndarray) -> Dict[str, float]:
    array = finite_values(values)
    if not array.size:
        return {
            key: float("nan")
            for key in (
                "min",
                "max",
                "range",
                "mean",
                "median",
                "std",
                "p05",
                "p25",
                "p75",
                "p95",
                "p99",
            )
        }
    minimum = float(np.min(array))
    maximum = float(np.max(array))
    return {
        "min": minimum,
        "max": maximum,
        "range": maximum - minimum,
        "mean": float(np.mean(array)),
        "median": float(np.median(array)),
        "std": float(np.std(array)),
        "p05": float(np.percentile(array, 5)),
        "p25": float(np.percentile(array, 25)),
        "p75": float(np.percentile(array, 75)),
        "p95": float(np.percentile(array, 95)),
        "p99": float(np.percentile(array, 99)),
    }


def normalize01(
    values: np.ndarray,
    low_percentile: float = 0.5,
    high_percentile: float = 99.5,
) -> np.ndarray:
    array = np.asarray(values, dtype=np.float32)
    finite = array[np.isfinite(array)]
    if not finite.size:
        return np.zeros_like(array, dtype=np.float32)
    low = float(np.percentile(finite, low_percentile))
    high = float(np.percentile(finite, high_percentile))
    if high <= low:
        return np.zeros_like(array, dtype=np.float32)
    return np.clip((array - low) / (high - low), 0.0, 1.0).astype(np.float32)


def downsample_float_image(values: np.ndarray, max_size: int) -> np.ndarray:
    array = np.asarray(values, dtype=np.float32)
    height, width = array.shape
    if max(height, width) <= max_size:
        return array
    scale = max_size / float(max(height, width))
    new_width = max(1, int(round(width * scale)))
    new_height = max(1, int(round(height * scale)))
    image = Image.fromarray(array, mode="F")
    image = image.resize(
        (new_width, new_height),
        resample=Image.Resampling.BILINEAR,
    )
    return np.asarray(image, dtype=np.float32)


def json_safe(value: Any) -> Any:
    if isinstance(value, dict):
        return {str(key): json_safe(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [json_safe(item) for item in value]
    if isinstance(value, np.generic):
        value = value.item()
    if isinstance(value, float) and not np.isfinite(value):
        return None
    return value


def flatten_dict(
    data: Dict[str, Any],
    prefix: str = "",
) -> Iterable[Tuple[str, Any]]:
    for key, value in data.items():
        full_key = f"{prefix}.{key}" if prefix else key
        if isinstance(value, dict):
            yield from flatten_dict(value, full_key)
        elif isinstance(value, (str, int, float, bool)) or value is None:
            yield full_key, value


def configure_plot_style() -> None:
    plt.rcParams.update({
        "figure.dpi": 120,
        "savefig.dpi": 220,
        "font.size": 9,
        "axes.titlesize": 10,
        "axes.labelsize": 9,
        "xtick.labelsize": 8,
        "ytick.labelsize": 8,
        "legend.fontsize": 8,
        "axes.linewidth": 0.7,
    })


def set_map_axes(
    axis: plt.Axes,
    map_size_m: float,
    centered: bool = False,
) -> None:
    if centered:
        half = 0.5 * map_size_m
        axis.set_xlim(-half, half)
        axis.set_ylim(-half, half)
    else:
        axis.set_xlim(0.0, map_size_m)
        axis.set_ylim(map_size_m, 0.0)
    axis.set_aspect("equal", adjustable="box")
    axis.set_xlabel("x (m)")
    axis.set_ylabel("y (m)")


# -----------------------------------------------------------------------------
# Metadata and optional terrain background
# -----------------------------------------------------------------------------

def nested_number(
    data: Dict[str, Any],
    paths: Sequence[Sequence[str]],
) -> Optional[float]:
    for path in paths:
        current: Any = data
        for key in path:
            if not isinstance(current, dict) or key not in current:
                break
            current = current[key]
        else:
            try:
                return float(current)
            except (TypeError, ValueError):
                pass
    return None


def map_size_from_metadata(metadata: Dict[str, Any]) -> float:
    value = nested_number(metadata, [
        ("map_size_m",),
        ("terrain_size_m",),
        ("size_m",),
        ("settings", "map_size_m"),
        ("settings", "terrain_size_m"),
        ("settings", "size_m"),
        ("generation", "map_size_m"),
    ])
    if value is None or value <= 0:
        raise KeyError(
            "Metadata must contain a positive map_size_m, either at the root "
            "or under settings/generation."
        )
    return float(value)


def expected_heightmap_size(metadata: Dict[str, Any]) -> Optional[int]:
    value = nested_number(metadata, [
        ("heightmap_size_px",),
        ("size_px",),
        ("settings", "heightmap_size"),
        ("settings", "size"),
    ])
    if value is None:
        return None
    size = int(round(value))
    return size if size > 0 else None


def encoded_range_from_metadata(
    metadata: Dict[str, Any],
) -> Tuple[float, float]:
    minimum = nested_number(metadata, [
        ("unreal_import", "encoded_min_m"),
        ("encoded_min_m",),
        ("height_encoding", "min_m"),
    ])
    maximum = nested_number(metadata, [
        ("unreal_import", "encoded_max_m"),
        ("encoded_max_m",),
        ("height_encoding", "max_m"),
    ])
    if minimum is None or maximum is None or maximum <= minimum:
        raise KeyError(
            "Metadata must contain a valid encoded elevation range."
        )
    return float(minimum), float(maximum)


def load_heightmap(
    path: Path,
    metadata: Dict[str, Any],
) -> np.ndarray:
    encoded_min_m, encoded_max_m = encoded_range_from_metadata(metadata)
    suffix = path.suffix.lower()

    if suffix == ".png":
        with Image.open(path) as image:
            raw = np.asarray(image)
        if raw.ndim != 2:
            raise ValueError(
                f"Heightmap must be single-channel, got shape {raw.shape}"
            )
        if raw.dtype != np.uint16:
            if (
                np.issubdtype(raw.dtype, np.integer)
                and int(np.min(raw)) >= 0
                and int(np.max(raw)) <= 65535
            ):
                raw = raw.astype(np.uint16)
            else:
                raise ValueError(
                    f"Heightmap PNG must contain 16-bit integer samples; "
                    f"got {raw.dtype}"
                )
    elif suffix in {".r16", ".raw"}:
        size = expected_heightmap_size(metadata)
        raw_1d = np.fromfile(path, dtype="<u2")
        if size is None:
            size = int(round(math.sqrt(raw_1d.size)))
        if size * size != raw_1d.size:
            raise ValueError(
                f"Could not determine square R16/RAW dimensions for {path}"
            )
        raw = raw_1d.reshape((size, size))
    else:
        raise ValueError(
            f"Unsupported heightmap format {path.suffix}; "
            "use PNG, R16, or RAW."
        )

    raw_float = raw.astype(np.float32)
    return (
        encoded_min_m
        + (raw_float / 65535.0) * (encoded_max_m - encoded_min_m)
    ).astype(np.float32)


def compute_hillshade(
    height_m: np.ndarray,
    map_size_m: float,
    sun_azimuth_deg: float,
    sun_elevation_deg: float,
) -> np.ndarray:
    height, width = height_m.shape
    dx_m = map_size_m / float(max(width - 1, 1))
    dy_m = map_size_m / float(max(height - 1, 1))
    dz_dy, dz_dx = np.gradient(height_m, dy_m, dx_m)
    slope = np.arctan(np.sqrt(dz_dx * dz_dx + dz_dy * dz_dy))
    aspect = np.arctan2(-dz_dx, dz_dy)
    azimuth = np.deg2rad(float(sun_azimuth_deg))
    elevation = np.deg2rad(float(sun_elevation_deg))
    illumination = (
        np.sin(elevation) * np.cos(slope)
        + np.cos(elevation)
        * np.sin(slope)
        * np.cos(azimuth - aspect)
    )
    return normalize01(illumination)


# -----------------------------------------------------------------------------
# Crater catalog
# -----------------------------------------------------------------------------

def read_craters(path: Path) -> Tuple[List[Dict[str, Any]], Dict[str, Any]]:
    root = read_json(path)
    crater_items = root.get("craters")
    if not isinstance(crater_items, list):
        raise ValueError(
            f"Crater JSON must contain a 'craters' list: {path}"
        )

    craters: List[Dict[str, Any]] = []
    for list_index, crater in enumerate(crater_items):
        if not isinstance(crater, dict):
            continue
        craters.append({
            "list_index": list_index,
            "crater_index": safe_int(
                crater.get(
                    "crater_index",
                    crater.get("index", list_index),
                ),
                list_index,
            ),
            "x_m": safe_float(
                crater.get("x_m", crater.get("X_Meters"))
            ),
            "y_m": safe_float(
                crater.get("y_m", crater.get("Y_Meters"))
            ),
            "diameter_m": safe_float(
                crater.get(
                    "diameter_m",
                    crater.get("DiameterMeters"),
                )
            ),
            "degradation": safe_float(
                crater.get(
                    "degradation",
                    crater.get(
                        "degrade",
                        crater.get("Degrade"),
                    ),
                )
            ),
        })

    if not craters:
        raise ValueError(f"No craters were found in {path}")
    return craters, root


def coordinates_are_centered(
    x_values: np.ndarray,
    y_values: np.ndarray,
) -> bool:
    finite = np.isfinite(x_values) & np.isfinite(y_values)
    if not np.any(finite):
        return False
    return (
        float(np.min(x_values[finite])) < 0.0
        or float(np.min(y_values[finite])) < 0.0
    )


def to_top_left_coordinates(
    x_values: np.ndarray,
    y_values: np.ndarray,
    map_size_m: float,
) -> Tuple[np.ndarray, np.ndarray]:
    x = np.asarray(x_values, dtype=np.float64).copy()
    y = np.asarray(y_values, dtype=np.float64).copy()
    if coordinates_are_centered(x, y):
        x += 0.5 * map_size_m
        y += 0.5 * map_size_m
    return x, y


def crater_arrays(
    craters: Sequence[Dict[str, Any]],
    map_size_m: float,
) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    x = np.array(
        [safe_float(crater.get("x_m")) for crater in craters],
        dtype=np.float64,
    )
    y = np.array(
        [safe_float(crater.get("y_m")) for crater in craters],
        dtype=np.float64,
    )
    diameter = np.array(
        [safe_float(crater.get("diameter_m")) for crater in craters],
        dtype=np.float64,
    )
    x, y = to_top_left_coordinates(x, y, map_size_m)
    return x, y, diameter


def crater_index_lookup(
    craters: Sequence[Dict[str, Any]],
) -> Dict[int, int]:
    lookup: Dict[int, int] = {}
    for list_index, crater in enumerate(craters):
        lookup[list_index] = list_index
        explicit = safe_int(crater.get("crater_index"), list_index)
        lookup[explicit] = list_index
    return lookup


# -----------------------------------------------------------------------------
# Rockfield loading and safe merge
# -----------------------------------------------------------------------------

def canonical_rock_record(
    rock: Dict[str, Any],
    default_index: int,
    source_file: str,
) -> Dict[str, Any]:
    transform = (
        rock.get("world_transform", {})
        if isinstance(rock.get("world_transform"), dict)
        else {}
    )
    location = (
        transform.get("location", {})
        if isinstance(transform.get("location"), dict)
        else {}
    )

    return {
        "instance_id": safe_int(
            rock.get(
                "instance_id",
                rock.get("id", default_index),
            ),
            default_index,
        ),
        "x_m": safe_float(
            rock.get(
                "x_m",
                rock.get("X_Meters"),
            )
        ),
        "y_m": safe_float(
            rock.get(
                "y_m",
                rock.get("Y_Meters"),
            )
        ),
        "diameter_m": safe_float(
            rock.get(
                "diameter_m",
                rock.get(
                    "DiameterMeters",
                    rock.get("size_m"),
                ),
            )
        ),
        "size_class": str(rock.get("size_class", "") or ""),
        "material_type": str(rock.get("material_type", "") or ""),
        "crater_zone": str(
            rock.get(
                "crater_zone",
                rock.get("zone", ""),
            )
            or ""
        ),
        "dominant_crater_index": safe_int(
            rock.get(
                "dominant_crater_index",
                rock.get("source_crater_index"),
            )
        ),
        "distance_to_dominant_crater_center_m": safe_float(
            rock.get("distance_to_dominant_crater_center_m")
        ),
        "normalized_crater_radius": safe_float(
            rock.get(
                "normalized_crater_radius",
                rock.get("rnorm"),
            )
        ),
        "local_slope_deg": safe_float(rock.get("local_slope_deg")),
        "local_density_per_m2": safe_float(
            rock.get("local_density_per_m2")
        ),
        "acceptance_probability": safe_float(
            rock.get("acceptance_probability")
        ),
        "source_type": str(rock.get("source_type", "") or ""),
        "clump_id": safe_int(rock.get("clump_id")),
        "mesh_name": str(rock.get("mesh_name", "") or ""),
        "world_x_cm": safe_float(location.get("x_cm")),
        "world_y_cm": safe_float(location.get("y_cm")),
        "world_z_cm": safe_float(location.get("z_cm")),
        "source_file": source_file,
    }


def read_rocks_json(path: Optional[Path]) -> List[Dict[str, Any]]:
    if path is None or not path.is_file():
        return []

    root = read_json(path)
    items: Any = None
    for key in ("rocks", "instances", "rock_instances", "candidates"):
        if isinstance(root.get(key), list):
            items = root[key]
            break

    if not isinstance(items, list):
        raise ValueError(
            f"Rock JSON must contain a list under rocks, instances, "
            f"rock_instances, or candidates: {path}"
        )

    return [
        canonical_rock_record(item, index, path.name)
        for index, item in enumerate(items)
        if isinstance(item, dict)
    ]


def read_rocks_csv(path: Optional[Path]) -> List[Dict[str, Any]]:
    if path is None or not path.is_file():
        return []

    rows: List[Dict[str, Any]] = []
    with path.open("r", encoding="utf-8", newline="") as file:
        reader = csv.DictReader(file)
        for index, row in enumerate(reader):
            rows.append(
                canonical_rock_record(
                    dict(row),
                    index,
                    path.name,
                )
            )
    return rows


def value_is_missing(key: str, value: Any) -> bool:
    if value is None:
        return True
    if isinstance(value, float) and not np.isfinite(value):
        return True
    if isinstance(value, str):
        stripped = value.strip()
        if not stripped:
            return True
        if (
            key not in {"source_file", "mesh_name"}
            and stripped.lower() in {"none", "nan", "unknown"}
        ):
            return True
    if key in {"dominant_crater_index", "clump_id"}:
        try:
            return int(value) < 0
        except (TypeError, ValueError):
            return True
    if key in {
        "normalized_crater_radius",
        "distance_to_dominant_crater_center_m",
    }:
        try:
            numeric = float(value)
            return not np.isfinite(numeric) or numeric < 0
        except (TypeError, ValueError):
            return True
    return False


def merge_rock_rows(
    *groups: Sequence[Dict[str, Any]],
) -> List[Dict[str, Any]]:
    merged: Dict[int, Dict[str, Any]] = {}

    for group in groups:
        for row in group:
            instance_id = safe_int(
                row.get("instance_id"),
                len(merged),
            )
            if instance_id not in merged:
                merged[instance_id] = {
                    "instance_id": instance_id,
                    "source_files": [],
                }

            destination = merged[instance_id]
            source_file = row.get("source_file")
            if (
                source_file
                and source_file
                not in destination.setdefault("source_files", [])
            ):
                destination["source_files"].append(source_file)

            for key, value in row.items():
                if key == "instance_id":
                    continue
                if key == "source_file":
                    destination[key] = value
                    continue

                old_value = destination.get(key)
                if (
                    value_is_missing(key, value)
                    and not value_is_missing(key, old_value)
                ):
                    continue
                destination[key] = value

            destination["instance_id"] = instance_id

    return [merged[key] for key in sorted(merged)]


def discover_rock_files(
    rockfield_dir: Optional[Path],
) -> Dict[str, Optional[Path]]:
    discovered: Dict[str, Optional[Path]] = {
        "candidates": None,
        "positions": None,
        "instances": None,
        "metadata_csv": None,
        "generic_json": None,
    }
    if rockfield_dir is None or not rockfield_dir.is_dir():
        return discovered

    known = {
        "candidates": rockfield_dir / "rock_candidates.json",
        "positions": rockfield_dir / "rock_positions.json",
        "instances": rockfield_dir / "rock_instances.json",
        "metadata_csv": rockfield_dir / "rock_metadata.csv",
    }
    for key, path in known.items():
        if path.is_file():
            discovered[key] = path

    if not any(
        discovered[key]
        for key in ("candidates", "positions", "instances")
    ):
        generic_candidates = sorted(
            path
            for path in rockfield_dir.glob("*.json")
            if "crater" not in path.name.lower()
            and "settings" not in path.name.lower()
            and "manifest" not in path.name.lower()
        )
        if generic_candidates:
            discovered["generic_json"] = generic_candidates[0]

    return discovered


def load_rocks(args: argparse.Namespace) -> Tuple[List[Dict[str, Any]], Dict[str, Any]]:
    rockfield_dir = (
        Path(args.rockfield_dir).expanduser().resolve()
        if args.rockfield_dir
        else None
    )
    discovered = discover_rock_files(rockfield_dir)

    explicit_or_discovered = {
        "candidates": (
            Path(args.candidates_json).expanduser().resolve()
            if args.candidates_json
            else discovered["candidates"]
        ),
        "positions": (
            Path(args.positions_json).expanduser().resolve()
            if args.positions_json
            else discovered["positions"]
        ),
        "instances": (
            Path(args.instances_json).expanduser().resolve()
            if args.instances_json
            else discovered["instances"]
        ),
        "metadata_csv": (
            Path(args.rock_metadata_csv).expanduser().resolve()
            if args.rock_metadata_csv
            else discovered["metadata_csv"]
        ),
        "generic_json": (
            Path(args.rock_json).expanduser().resolve()
            if args.rock_json
            else discovered["generic_json"]
        ),
    }

    groups = [
        read_rocks_json(explicit_or_discovered["positions"]),
        read_rocks_json(explicit_or_discovered["candidates"]),
        read_rocks_csv(explicit_or_discovered["metadata_csv"]),
        read_rocks_json(explicit_or_discovered["instances"]),
        read_rocks_json(explicit_or_discovered["generic_json"]),
    ]

    rocks = merge_rock_rows(*groups)
    input_files = {
        key: str(path) if path is not None else None
        for key, path in explicit_or_discovered.items()
    }
    input_files["rockfield_dir"] = (
        str(rockfield_dir) if rockfield_dir is not None else None
    )

    if not rocks:
        raise ValueError(
            "No rocks were loaded. Supply --rockfield-dir or at least one "
            "explicit rock JSON/CSV input."
        )

    return rocks, input_files


def rock_arrays_all(
    rocks: Sequence[Dict[str, Any]],
    map_size_m: float,
) -> Tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    x = np.array(
        [safe_float(rock.get("x_m")) for rock in rocks],
        dtype=np.float64,
    )
    y = np.array(
        [safe_float(rock.get("y_m")) for rock in rocks],
        dtype=np.float64,
    )
    diameter = np.array(
        [safe_float(rock.get("diameter_m")) for rock in rocks],
        dtype=np.float64,
    )
    crater_id = np.array(
        [safe_int(rock.get("dominant_crater_index")) for rock in rocks],
        dtype=np.int64,
    )
    x, y = to_top_left_coordinates(x, y, map_size_m)
    return x, y, diameter, crater_id


def is_random_big_rock_clump(rock: Dict[str, Any]) -> bool:
    source_type = str(rock.get("source_type", "") or "").lower()
    if "random_big_rock" in source_type:
        return True
    return safe_int(rock.get("clump_id"), -1) >= 200000


def is_crater_owned_rock(rock: Dict[str, Any]) -> bool:
    return (
        safe_int(rock.get("dominant_crater_index"), -1) >= 0
        and not is_random_big_rock_clump(rock)
    )


# -----------------------------------------------------------------------------
# Exact or inferred crater/ejecta-zone settings
# -----------------------------------------------------------------------------

def preset_text(
    metadata: Dict[str, Any],
    *names: str,
) -> str:
    values = list(names)
    for key in (
        "preset",
        "preset_name",
        "heightmap_preset",
        "rock_profile",
    ):
        if metadata.get(key):
            values.append(str(metadata[key]))

    settings = metadata.get("settings")
    if isinstance(settings, dict):
        for key in (
            "preset",
            "preset_name",
            "heightmap_preset",
            "rock_profile",
        ):
            if settings.get(key):
                values.append(str(settings[key]))

    return " ".join(values).lower()


def infer_zone_settings(
    metadata: Dict[str, Any],
    *names: str,
) -> Tuple[Dict[str, float], str]:
    text = preset_text(metadata, *names)
    if "apollo17" in text or "apollo_17" in text:
        return (
            dict(PRESET_ZONE_DEFAULTS["apollo17"]),
            "inferred Apollo 17 preset defaults",
        )
    if "highland" in text or "polar" in text:
        return (
            dict(PRESET_ZONE_DEFAULTS["highland"]),
            "inferred highland preset defaults",
        )
    if "fresh" in text:
        return (
            dict(PRESET_ZONE_DEFAULTS["fresh"]),
            "inferred fresh-crater preset defaults",
        )
    if "mare" in text:
        return (
            dict(PRESET_ZONE_DEFAULTS["mare"]),
            "inferred mare preset defaults",
        )
    return dict(GENERIC_ZONE_DEFAULTS), "generic nominal defaults"


def load_zone_settings(
    rock_settings_path: Optional[Path],
    metadata: Dict[str, Any],
    *names: str,
) -> Tuple[Dict[str, float], str]:
    settings, source = infer_zone_settings(metadata, *names)

    if rock_settings_path is None:
        validate_zone_settings(settings)
        return settings, source

    root = read_json(rock_settings_path)
    settings_root = root.get("settings", root)
    if not isinstance(settings_root, dict):
        raise ValueError(
            f"Rock-settings JSON must contain an object: "
            f"{rock_settings_path}"
        )

    keys = (
        "interior_r_min",
        "interior_r_max",
        "rim_r_min",
        "rim_r_max",
        "proximal_r_min",
        "proximal_r_max",
        "distal_r_min",
        "distal_r_max",
        "min_source_crater_diameter_meters",
    )
    for key in keys:
        if key in settings_root:
            settings[key] = float(settings_root[key])

    validate_zone_settings(settings)
    return settings, f"rock settings: {rock_settings_path.name}"


def validate_zone_settings(settings: Dict[str, float]) -> None:
    for zone in ("interior", "rim", "proximal", "distal"):
        lower = float(settings[f"{zone}_r_min"])
        upper = float(settings[f"{zone}_r_max"])
        if lower < 0 or upper <= lower:
            raise ValueError(
                f"Invalid {zone} zone bounds: {lower:g} to {upper:g} R"
            )
    if settings["min_source_crater_diameter_meters"] < 0:
        raise ValueError(
            "min_source_crater_diameter_meters cannot be negative."
        )


def canonical_zone_name(value: str) -> str:
    normalized = re.sub(r"[^a-z0-9]+", "", str(value).lower())
    mapping = {
        "background": "Background",
        "none": "Background",
        "craterfloor": "CraterFloor",
        "craterinterior": "CraterFloor",
        "interior": "CraterFloor",
        "floor": "CraterFloor",
        "rim": "Rim",
        "proximalejecta": "ProximalEjecta",
        "proximal": "ProximalEjecta",
        "distalejecta": "DistalEjecta",
        "distal": "DistalEjecta",
    }
    return mapping.get(normalized, "Unknown")


def zone_from_rnorm(
    rnorm: float,
    settings: Dict[str, float],
) -> str:
    if not np.isfinite(rnorm) or rnorm < 0:
        return "Background"

    if (
        settings["rim_r_min"]
        <= rnorm
        < settings["rim_r_max"]
    ):
        return "Rim"
    if (
        settings["interior_r_min"]
        <= rnorm
        < settings["interior_r_max"]
    ):
        return "CraterFloor"
    if (
        settings["proximal_r_min"]
        <= rnorm
        < settings["proximal_r_max"]
    ):
        return "ProximalEjecta"
    if (
        settings["distal_r_min"]
        <= rnorm
        <= settings["distal_r_max"]
    ):
        return "DistalEjecta"
    return "Background"


def source_rnorms(
    rocks: Sequence[Dict[str, Any]],
    craters: Sequence[Dict[str, Any]],
    map_size_m: float,
) -> np.ndarray:
    rnorm = np.array(
        [
            safe_float(rock.get("normalized_crater_radius"))
            for rock in rocks
        ],
        dtype=np.float64,
    )

    crater_x, crater_y, crater_diameter = crater_arrays(
        craters,
        map_size_m,
    )
    lookup = crater_index_lookup(craters)
    rock_x, rock_y, _rock_diameter, rock_crater_id = rock_arrays_all(
        rocks,
        map_size_m,
    )

    missing = ~np.isfinite(rnorm) | (rnorm < 0)
    for index in np.where(missing)[0]:
        crater_list_index = lookup.get(int(rock_crater_id[index]))
        if crater_list_index is None:
            continue
        radius = 0.5 * crater_diameter[crater_list_index]
        if (
            not np.isfinite(radius)
            or radius <= 0
            or not np.isfinite(rock_x[index])
            or not np.isfinite(rock_y[index])
        ):
            continue
        rnorm[index] = (
            math.hypot(
                rock_x[index] - crater_x[crater_list_index],
                rock_y[index] - crater_y[crater_list_index],
            )
            / radius
        )

    return rnorm


def crater_owned_zone_labels(
    rocks: Sequence[Dict[str, Any]],
    rnorm: np.ndarray,
    settings: Dict[str, float],
) -> np.ndarray:
    labels = np.full(len(rocks), "Background", dtype=object)

    for index, rock in enumerate(rocks):
        if not is_crater_owned_rock(rock):
            continue

        explicit = canonical_zone_name(
            str(rock.get("crater_zone", "") or "")
        )
        if explicit not in {"Unknown", "Background"}:
            labels[index] = explicit
        else:
            labels[index] = zone_from_rnorm(
                float(rnorm[index]),
                settings,
            )

    return labels


def classify_zone_codes(
    rnorm: np.ndarray,
    settings: Dict[str, float],
) -> np.ndarray:
    codes = np.zeros(rnorm.shape, dtype=np.uint8)

    distal = (
        (rnorm >= settings["distal_r_min"])
        & (rnorm <= settings["distal_r_max"])
    )
    proximal = (
        (rnorm >= settings["proximal_r_min"])
        & (rnorm < settings["proximal_r_max"])
    )
    interior = (
        (rnorm >= settings["interior_r_min"])
        & (rnorm < settings["interior_r_max"])
    )
    rim = (
        (rnorm >= settings["rim_r_min"])
        & (rnorm < settings["rim_r_max"])
    )

    codes[distal] = 4
    codes[proximal] = 3
    codes[interior] = 1
    codes[rim] = 2
    return codes


def build_crater_zone_map(
    craters: Sequence[Dict[str, Any]],
    map_size_m: float,
    settings: Dict[str, float],
    resolution: int,
) -> np.ndarray:
    resolution = max(128, int(resolution))
    zone_map = np.zeros(
        (resolution, resolution),
        dtype=np.uint8,
    )
    best_rnorm = np.full(
        (resolution, resolution),
        np.inf,
        dtype=np.float32,
    )

    crater_x, crater_y, crater_diameter = crater_arrays(
        craters,
        map_size_m,
    )
    minimum_diameter = settings[
        "min_source_crater_diameter_meters"
    ]
    valid = (
        np.isfinite(crater_x)
        & np.isfinite(crater_y)
        & np.isfinite(crater_diameter)
        & (crater_diameter >= minimum_diameter)
        & (crater_diameter > 0)
    )
    if not np.any(valid):
        return zone_map

    cell_size_m = map_size_m / float(resolution)
    outer_multiplier = max(
        settings["interior_r_max"],
        settings["rim_r_max"],
        settings["proximal_r_max"],
        settings["distal_r_max"],
    )

    valid_indices = np.where(valid)[0]
    valid_indices = valid_indices[
        np.argsort(crater_diameter[valid_indices])[::-1]
    ]

    for crater_index in valid_indices:
        x_m = crater_x[crater_index]
        y_m = crater_y[crater_index]
        radius_m = 0.5 * crater_diameter[crater_index]
        support_m = outer_multiplier * radius_m

        x0 = max(
            0,
            int(math.floor((x_m - support_m) / cell_size_m)),
        )
        x1 = min(
            resolution - 1,
            int(math.ceil((x_m + support_m) / cell_size_m)),
        )
        y0 = max(
            0,
            int(math.floor((y_m - support_m) / cell_size_m)),
        )
        y1 = min(
            resolution - 1,
            int(math.ceil((y_m + support_m) / cell_size_m)),
        )
        if x0 > x1 or y0 > y1:
            continue

        x_centres = (
            np.arange(x0, x1 + 1, dtype=np.float32) + 0.5
        ) * cell_size_m
        y_centres = (
            np.arange(y0, y1 + 1, dtype=np.float32) + 0.5
        ) * cell_size_m
        xx, yy = np.meshgrid(x_centres, y_centres)

        rnorm = (
            np.sqrt((xx - x_m) ** 2 + (yy - y_m) ** 2)
            / max(radius_m, 1e-9)
        )
        codes = classify_zone_codes(rnorm, settings)
        inside = codes > 0
        if not np.any(inside):
            continue

        best_patch = best_rnorm[y0:y1 + 1, x0:x1 + 1]
        update = inside & (rnorm < best_patch)
        if not np.any(update):
            continue

        zone_patch = zone_map[y0:y1 + 1, x0:x1 + 1]
        zone_patch[update] = codes[update]
        best_patch[update] = rnorm[update]

    return zone_map


# -----------------------------------------------------------------------------
# Spatial statistics
# -----------------------------------------------------------------------------

def nearest_neighbor_distances(
    x_values: np.ndarray,
    y_values: np.ndarray,
) -> np.ndarray:
    valid = np.isfinite(x_values) & np.isfinite(y_values)
    points = np.column_stack([
        x_values[valid],
        y_values[valid],
    ])
    if points.shape[0] < 2:
        return np.array([], dtype=np.float64)

    tree = cKDTree(points)
    distances, _indices = tree.query(points, k=2)
    return distances[:, 1].astype(np.float64)


def quadrat_counts(
    x_values: np.ndarray,
    y_values: np.ndarray,
    map_size_m: float,
    bins: int,
) -> np.ndarray:
    valid = np.isfinite(x_values) & np.isfinite(y_values)
    if not np.any(valid):
        return np.zeros((bins, bins), dtype=np.float64)

    counts, _x_edges, _y_edges = np.histogram2d(
        x_values[valid],
        y_values[valid],
        bins=bins,
        range=[[0.0, map_size_m], [0.0, map_size_m]],
    )
    return counts


def index_of_dispersion(counts: np.ndarray) -> float:
    values = counts.ravel().astype(np.float64)
    mean = float(np.mean(values))
    if values.size < 2 or mean <= 1e-12:
        return float("nan")
    return float(np.var(values, ddof=1) / mean)


def rock_density_grid(
    x_values: np.ndarray,
    y_values: np.ndarray,
    map_size_m: float,
    bins: int,
    smoothing_sigma_px: float,
) -> np.ndarray:
    valid = np.isfinite(x_values) & np.isfinite(y_values)
    if not np.any(valid):
        return np.zeros((bins, bins), dtype=np.float32)

    counts, _x_edges, _y_edges = np.histogram2d(
        x_values[valid],
        y_values[valid],
        bins=bins,
        range=[[0.0, map_size_m], [0.0, map_size_m]],
    )
    cell_area_m2 = (map_size_m / float(bins)) ** 2
    density = counts.T / max(cell_area_m2, 1e-12)
    if smoothing_sigma_px > 0:
        density = gaussian_filter(
            density,
            sigma=float(smoothing_sigma_px),
            mode="nearest",
        )
    return density.astype(np.float32)


# -----------------------------------------------------------------------------
# Metrics and reports
# -----------------------------------------------------------------------------

def build_metrics(
    terrain_name: str,
    rocks: Sequence[Dict[str, Any]],
    craters: Sequence[Dict[str, Any]],
    map_size_m: float,
    rnorm: np.ndarray,
    zone_labels: np.ndarray,
    nearest_neighbor: np.ndarray,
    quadrat: np.ndarray,
    zone_settings: Dict[str, float],
    zone_settings_source: str,
    input_files: Dict[str, Any],
) -> Dict[str, Any]:
    rock_x, rock_y, rock_diameter, crater_ids = rock_arrays_all(
        rocks,
        map_size_m,
    )
    valid_positions = np.isfinite(rock_x) & np.isfinite(rock_y)
    valid_diameter = finite_positive(rock_diameter)

    owned_mask = np.array(
        [is_crater_owned_rock(rock) for rock in rocks],
        dtype=bool,
    )
    random_big_mask = np.array(
        [is_random_big_rock_clump(rock) for rock in rocks],
        dtype=bool,
    )

    area_m2 = map_size_m * map_size_m
    density_per_m2 = (
        float(np.count_nonzero(valid_positions) / area_m2)
        if area_m2 > 0
        else float("nan")
    )

    expected_poisson_nn = (
        0.5 / math.sqrt(density_per_m2)
        if density_per_m2 > 0
        else float("nan")
    )
    observed_mean_nn = (
        float(np.mean(nearest_neighbor))
        if nearest_neighbor.size
        else float("nan")
    )
    clark_evans_r = (
        observed_mean_nn / expected_poisson_nn
        if (
            np.isfinite(observed_mean_nn)
            and np.isfinite(expected_poisson_nn)
            and expected_poisson_nn > 0
        )
        else float("nan")
    )

    counts_by_zone = Counter(
        str(label)
        for label, owned in zip(zone_labels, owned_mask)
        if owned
    )
    counts_by_size_class = Counter(
        str(rock.get("size_class", "") or "Unknown")
        for rock in rocks
    )
    counts_by_material = Counter(
        str(rock.get("material_type", "") or "Unknown")
        for rock in rocks
    )
    counts_by_source_type = Counter(
        str(rock.get("source_type", "") or "Unknown")
        for rock in rocks
    )

    source_crater_counts = Counter(
        int(crater_id)
        for crater_id, owned in zip(crater_ids, owned_mask)
        if owned and crater_id >= 0
    )

    return {
        "format": "MoonSimRockfieldAnalysis",
        "version": 1,
        "terrain": terrain_name,
        "inputs": input_files,
        "map": {
            "map_size_m": float(map_size_m),
            "area_m2": float(area_m2),
            "area_km2": float(area_m2 / 1_000_000.0),
        },
        "rocks": {
            "loaded_count": int(len(rocks)),
            "valid_position_count": int(
                np.count_nonzero(valid_positions)
            ),
            "valid_diameter_count": int(valid_diameter.size),
            "density_per_m2": density_per_m2,
            "crater_owned_count": int(np.count_nonzero(owned_mask)),
            "background_or_unowned_count": int(
                len(rocks) - np.count_nonzero(owned_mask)
            ),
            "random_big_rock_clump_count": int(
                np.count_nonzero(random_big_mask)
            ),
            "diameter_m": array_statistics(valid_diameter),
            "counts_by_crater_zone": dict(counts_by_zone),
            "counts_by_size_class": dict(counts_by_size_class),
            "counts_by_material_type": dict(counts_by_material),
            "counts_by_source_type": dict(counts_by_source_type),
            "owned_rocks_by_source_crater": {
                str(key): int(value)
                for key, value in source_crater_counts.items()
            },
        },
        "spatial": {
            "nearest_neighbor_distance_m": array_statistics(
                nearest_neighbor
            ),
            "poisson_expected_mean_nearest_neighbor_m":
                expected_poisson_nn,
            "clark_evans_r": clark_evans_r,
            "clark_evans_interpretation": (
                "clustered"
                if np.isfinite(clark_evans_r) and clark_evans_r < 0.9
                else (
                    "dispersed"
                    if np.isfinite(clark_evans_r)
                    and clark_evans_r > 1.1
                    else "approximately random"
                )
            ),
            "quadrat_bins": int(quadrat.shape[0]),
            "quadrat_mean_count": float(np.mean(quadrat)),
            "quadrat_variance_count": (
                float(np.var(quadrat, ddof=1))
                if quadrat.size > 1
                else float("nan")
            ),
            "quadrat_index_of_dispersion": index_of_dispersion(
                quadrat
            ),
        },
        "source_crater_distance": {
            "normalized_radius": array_statistics(
                rnorm[np.isfinite(rnorm) & (rnorm >= 0)]
            ),
        },
        "craters": {
            "catalog_count": int(len(craters)),
        },
        "crater_zone_settings": {
            "source": zone_settings_source,
            "values": zone_settings,
        },
    }


def write_metrics_csv(
    path: Path,
    metrics: Dict[str, Any],
) -> None:
    excluded_roots = {"inputs"}
    rows = [
        (key, value)
        for key, value in flatten_dict(metrics)
        if key.split(".", 1)[0] not in excluded_roots
    ]
    with path.open("w", encoding="utf-8", newline="") as file:
        writer = csv.writer(file)
        writer.writerow(["metric", "value"])
        writer.writerows(rows)


def write_merged_rocks_csv(
    path: Path,
    rocks: Sequence[Dict[str, Any]],
    rnorm: np.ndarray,
    zone_labels: np.ndarray,
) -> None:
    preferred = [
        "instance_id",
        "x_m",
        "y_m",
        "diameter_m",
        "size_class",
        "material_type",
        "source_type",
        "crater_zone",
        "analysis_crater_zone",
        "dominant_crater_index",
        "normalized_crater_radius",
        "analysis_normalized_crater_radius",
        "distance_to_dominant_crater_center_m",
        "local_slope_deg",
        "local_density_per_m2",
        "acceptance_probability",
        "clump_id",
        "mesh_name",
        "source_file",
        "source_files",
    ]

    rows: List[Dict[str, Any]] = []
    for index, rock in enumerate(rocks):
        row = dict(rock)
        row["analysis_crater_zone"] = str(zone_labels[index])
        row["analysis_normalized_crater_radius"] = float(
            rnorm[index]
        ) if np.isfinite(rnorm[index]) else ""
        if isinstance(row.get("source_files"), list):
            row["source_files"] = ";".join(row["source_files"])
        rows.append(row)

    keys = list(preferred)
    for row in rows:
        for key in row:
            if key not in keys:
                keys.append(key)

    with path.open("w", encoding="utf-8", newline="") as file:
        writer = csv.DictWriter(
            file,
            fieldnames=keys,
            extrasaction="ignore",
        )
        writer.writeheader()
        writer.writerows(rows)


def write_summary(
    path: Path,
    metrics: Dict[str, Any],
    output_files: Sequence[Path],
) -> None:
    rocks = metrics["rocks"]
    diameter = rocks["diameter_m"]
    spatial = metrics["spatial"]
    nearest_neighbor = spatial["nearest_neighbor_distance_m"]
    zone_counts = rocks["counts_by_crater_zone"]

    lines = [
        "MoonSim rockfield analysis",
        "==========================",
        f"Terrain: {metrics['terrain']}",
        (
            f"Map: {metrics['map']['map_size_m']:.3f} m × "
            f"{metrics['map']['map_size_m']:.3f} m"
        ),
        "",
        "Rock population",
        f"  loaded rocks: {rocks['loaded_count']}",
        f"  valid positions: {rocks['valid_position_count']}",
        f"  density: {rocks['density_per_m2']:.6f} rocks/m²",
        f"  crater-owned rocks: {rocks['crater_owned_count']}",
        (
            f"  background or unowned rocks: "
            f"{rocks['background_or_unowned_count']}"
        ),
        (
            f"  random big-rock-clump rocks: "
            f"{rocks['random_big_rock_clump_count']}"
        ),
        (
            f"  diameter min / median / mean / p95 / max: "
            f"{diameter['min']:.3f} / {diameter['median']:.3f} / "
            f"{diameter['mean']:.3f} / {diameter['p95']:.3f} / "
            f"{diameter['max']:.3f} m"
        ),
        "",
        "Crater-owned rocks by zone",
    ]

    for zone in (
        "CraterFloor",
        "Rim",
        "ProximalEjecta",
        "DistalEjecta",
        "Background",
        "Unknown",
    ):
        if zone in zone_counts:
            lines.append(
                f"  {ZONE_DISPLAY.get(zone, zone)}: "
                f"{zone_counts[zone]}"
            )

    lines.extend([
        "",
        "Spatial distribution",
        (
            f"  nearest-neighbor mean / median / p95: "
            f"{nearest_neighbor['mean']:.3f} / "
            f"{nearest_neighbor['median']:.3f} / "
            f"{nearest_neighbor['p95']:.3f} m"
        ),
        (
            f"  Poisson expected mean nearest-neighbor distance: "
            f"{spatial['poisson_expected_mean_nearest_neighbor_m']:.3f} m"
        ),
        (
            f"  Clark-Evans R: {spatial['clark_evans_r']:.3f} "
            f"({spatial['clark_evans_interpretation']})"
        ),
        (
            f"  quadrat index of dispersion: "
            f"{spatial['quadrat_index_of_dispersion']:.3f}"
        ),
        "",
        "Crater/ejecta-zone settings",
        f"  source: {metrics['crater_zone_settings']['source']}",
        "",
        "Output files",
        *[f"  {output_file}" for output_file in output_files],
        "",
    ])

    path.write_text("\n".join(lines), encoding="utf-8")


# -----------------------------------------------------------------------------
# Plot helpers
# -----------------------------------------------------------------------------

def draw_crater_rims(
    axis: plt.Axes,
    craters: Sequence[Dict[str, Any]],
    map_size_m: float,
    minimum_diameter_m: float,
    color: str = "white",
    linewidth: float = 0.45,
    alpha: float = 0.70,
    centered: bool = False,
) -> None:
    crater_x, crater_y, crater_diameter = crater_arrays(
        craters,
        map_size_m,
    )

    if centered:
        crater_x = crater_x - 0.5 * map_size_m
        crater_y = 0.5 * map_size_m - crater_y

    valid_indices = np.where(
        np.isfinite(crater_x)
        & np.isfinite(crater_y)
        & np.isfinite(crater_diameter)
        & (crater_diameter >= minimum_diameter_m)
        & (crater_diameter > 0)
    )[0]
    valid_indices = valid_indices[
        np.argsort(crater_diameter[valid_indices])[::-1]
    ]

    for index in valid_indices:
        axis.add_patch(Circle(
            (crater_x[index], crater_y[index]),
            0.5 * crater_diameter[index],
            fill=False,
            edgecolor=color,
            linewidth=linewidth,
            alpha=alpha,
        ))


def show_background(
    axis: plt.Axes,
    hillshade: Optional[np.ndarray],
    map_size_m: float,
    max_plot_size: int,
) -> None:
    if hillshade is None:
        axis.set_facecolor("#eeeeee")
        return

    display = downsample_float_image(
        hillshade,
        max_plot_size,
    )
    axis.imshow(
        display,
        cmap="gray",
        origin="upper",
        extent=[0.0, map_size_m, map_size_m, 0.0],
        vmin=0.0,
        vmax=1.0,
        interpolation="bilinear",
    )


# -----------------------------------------------------------------------------
# Figures
# -----------------------------------------------------------------------------

def make_overview_figure(
    path: Path,
    terrain_name: str,
    rocks: Sequence[Dict[str, Any]],
    map_size_m: float,
    rnorm: np.ndarray,
    zone_labels: np.ndarray,
    nearest_neighbor: np.ndarray,
    settings: Dict[str, float],
    dpi: int,
) -> None:
    rock_x, rock_y, rock_diameter, _crater_ids = rock_arrays_all(
        rocks,
        map_size_m,
    )
    diameter = finite_positive(rock_diameter)
    owned_mask = np.array(
        [is_crater_owned_rock(rock) for rock in rocks],
        dtype=bool,
    )

    fig, axes = plt.subplots(
        2,
        2,
        figsize=(10.8, 7.4),
        constrained_layout=True,
    )

    # Rock size distribution and cumulative abundance.
    size_axis = axes[0, 0]
    if diameter.size:
        if float(np.max(diameter)) > float(np.min(diameter)) * 1.001:
            bin_count = int(
                np.clip(np.sqrt(diameter.size), 10, 28)
            )
            edges = np.geomspace(
                float(np.min(diameter)),
                float(np.max(diameter)) * 1.000001,
                bin_count + 1,
            )
        else:
            edges = np.array([
                0.9 * float(np.min(diameter)),
                1.1 * float(np.max(diameter)) + 1e-9,
            ])

        counts, edges = np.histogram(diameter, bins=edges)
        centres = np.sqrt(edges[:-1] * edges[1:])
        widths = edges[1:] - edges[:-1]

        size_axis.bar(
            centres,
            counts,
            width=widths,
            align="center",
            color="#C9D7E3",
            edgecolor="#8CA3B5",
            linewidth=0.35,
            alpha=0.95,
            label="diameter-bin count",
        )
        size_axis.set_xscale("log")

        cumulative_axis = size_axis.twinx()
        sorted_diameter = np.sort(diameter)
        cumulative = np.arange(
            sorted_diameter.size,
            0,
            -1,
        )
        cumulative_axis.step(
            sorted_diameter,
            cumulative,
            where="post",
            linewidth=1.5,
            label=r"cumulative $N(\geq D)$",
        )
        cumulative_axis.set_yscale("log")
        cumulative_axis.set_ylabel(r"cumulative $N(\geq D)$")

        handles_1, labels_1 = size_axis.get_legend_handles_labels()
        handles_2, labels_2 = (
            cumulative_axis.get_legend_handles_labels()
        )
        size_axis.legend(
            handles_1 + handles_2,
            labels_1 + labels_2,
            frameon=False,
        )

    size_axis.set_xlabel("rock diameter (m)")
    size_axis.set_ylabel("rocks per diameter bin")
    size_axis.set_title("Rock size distribution")
    size_axis.grid(
        True,
        which="both",
        linewidth=0.3,
        alpha=0.3,
    )

    # Crater-owned rocks by zone.
    zone_axis = axes[0, 1]
    zone_counter = Counter(
        str(label)
        for label, owned in zip(zone_labels, owned_mask)
        if owned
    )
    plotted_zones = [
        zone
        for zone in (
            "CraterFloor",
            "Rim",
            "ProximalEjecta",
            "DistalEjecta",
            "Background",
            "Unknown",
        )
        if zone_counter.get(zone, 0) > 0
    ]
    if plotted_zones:
        values = [zone_counter[zone] for zone in plotted_zones]
        bars = zone_axis.bar(
            [ZONE_DISPLAY[zone] for zone in plotted_zones],
            values,
            color=[ZONE_COLORS[zone] for zone in plotted_zones],
            edgecolor="#555555",
            linewidth=0.35,
        )
        zone_axis.bar_label(
            bars,
            labels=[f"{value:,}" for value in values],
            padding=2,
            fontsize=8,
        )
        zone_axis.tick_params(axis="x", rotation=20)
    else:
        zone_axis.text(
            0.5,
            0.5,
            "No crater-owned rocks with zone information",
            ha="center",
            va="center",
            transform=zone_axis.transAxes,
        )
    zone_axis.set_ylabel("rock count")
    zone_axis.set_title("Crater-owned rocks by zone")
    zone_axis.grid(
        True,
        axis="y",
        linewidth=0.3,
        alpha=0.3,
    )

    # Nearest-neighbor distribution.
    nn_axis = axes[1, 0]
    if nearest_neighbor.size:
        nn_axis.hist(
            nearest_neighbor,
            bins=60,
            color="#D1DCE5",
            edgecolor="#879EAE",
            linewidth=0.35,
        )
        observed_mean = float(np.mean(nearest_neighbor))
        density = (
            np.count_nonzero(
                np.isfinite(rock_x) & np.isfinite(rock_y)
            )
            / (map_size_m * map_size_m)
        )
        expected_mean = (
            0.5 / math.sqrt(density)
            if density > 0
            else float("nan")
        )

        nn_axis.axvline(
            observed_mean,
            color="#2E7D32",
            linestyle="--",
            linewidth=1.6,
            label=f"observed mean {observed_mean:.2f} m",
        )
        if np.isfinite(expected_mean):
            nn_axis.axvline(
                expected_mean,
                color="#E05A00",
                linestyle=":",
                linewidth=1.6,
                label=f"Poisson expectation {expected_mean:.2f} m",
            )
        nn_axis.legend(frameon=False)
    nn_axis.set_xlabel("nearest-neighbor distance (m)")
    nn_axis.set_ylabel("rocks")
    nn_axis.set_title("Nearest-neighbor distribution")
    nn_axis.grid(
        True,
        axis="y",
        linewidth=0.3,
        alpha=0.3,
    )

    # Rock size versus source-crater-normalized distance.
    radial_axis = axes[1, 1]
    valid_radial = (
        owned_mask
        & np.isfinite(rnorm)
        & (rnorm >= 0)
        & np.isfinite(rock_diameter)
        & (rock_diameter > 0)
    )
    if np.any(valid_radial):
        radial_axis.scatter(
            rnorm[valid_radial],
            rock_diameter[valid_radial],
            s=9,
            alpha=0.30,
            linewidths=0,
        )
        radial_axis.set_yscale("log")

        boundaries = [
            (settings["interior_r_max"], "interior"),
            (settings["rim_r_max"], "rim"),
            (settings["proximal_r_max"], "proximal"),
            (settings["distal_r_max"], "distal"),
        ]
        for value, label in boundaries:
            radial_axis.axvline(
                value,
                linestyle="--",
                linewidth=0.9,
                alpha=0.75,
            )
            radial_axis.annotate(
                label,
                xy=(value, 1.0),
                xycoords=("data", "axes fraction"),
                xytext=(3, -6),
                textcoords="offset points",
                ha="left",
                va="top",
                fontsize=7,
            )
    else:
        radial_axis.text(
            0.5,
            0.5,
            "No source-crater distance metadata",
            ha="center",
            va="center",
            transform=radial_axis.transAxes,
        )

    radial_axis.set_xlabel(
        "distance from source crater centre / crater radius"
    )
    radial_axis.set_ylabel("rock diameter (m)")
    radial_axis.set_title(
        "Rock size versus source-crater distance"
    )
    radial_axis.grid(
        True,
        which="both",
        linewidth=0.3,
        alpha=0.3,
    )

    fig.suptitle(
        f"Rockfield analysis — {terrain_name}",
        fontsize=14,
        fontweight="bold",
    )
    fig.savefig(path, dpi=dpi, bbox_inches="tight")
    plt.close(fig)


def make_ejecta_zone_figure(
    path: Path,
    terrain_name: str,
    craters: Sequence[Dict[str, Any]],
    zone_map: np.ndarray,
    settings: Dict[str, float],
    settings_source: str,
    map_size_m: float,
    hillshade: Optional[np.ndarray],
    dpi: int,
    max_plot_size: int,
) -> None:
    minimum_diameter = settings[
        "min_source_crater_diameter_meters"
    ]

    fig, axes = plt.subplots(
        1,
        2,
        figsize=(11.2, 5.1),
        constrained_layout=True,
    )

    show_background(
        axes[0],
        hillshade,
        map_size_m,
        max_plot_size,
    )
    draw_crater_rims(
        axes[0],
        craters,
        map_size_m,
        minimum_diameter,
        color="white" if hillshade is not None else "#333333",
        linewidth=0.45,
        alpha=0.72,
    )
    set_map_axes(axes[0], map_size_m)
    axes[0].set_title(
        f"Source craters (D ≥ {minimum_diameter:g} m)"
    )

    axes[1].imshow(
        zone_map,
        cmap=ZONE_CODE_CMAP,
        norm=ZONE_CODE_NORM,
        origin="upper",
        extent=[0.0, map_size_m, map_size_m, 0.0],
        interpolation="nearest",
    )
    draw_crater_rims(
        axes[1],
        craters,
        map_size_m,
        minimum_diameter,
        color="white",
        linewidth=0.40,
        alpha=0.70,
    )
    set_map_axes(axes[1], map_size_m)
    axes[1].set_title(
        "Dominant crater/ejecta placement zone"
    )

    handles = [
        Patch(
            facecolor=ZONE_CODE_CMAP(code),
            edgecolor="none",
            label=ZONE_LABEL_BY_CODE[code],
        )
        for code in range(5)
    ]
    axes[1].legend(
        handles=handles,
        loc="upper center",
        bbox_to_anchor=(0.5, -0.14),
        ncol=3,
        frameon=False,
    )

    zone_text = (
        f"Interior {settings['interior_r_min']:g}–"
        f"{settings['interior_r_max']:g} R   "
        f"Rim {settings['rim_r_min']:g}–"
        f"{settings['rim_r_max']:g} R   "
        f"Proximal {settings['proximal_r_min']:g}–"
        f"{settings['proximal_r_max']:g} R   "
        f"Distal {settings['distal_r_min']:g}–"
        f"{settings['distal_r_max']:g} R"
    )

    fig.suptitle(
        f"Crater and ejecta zones — {terrain_name}\n"
        f"{zone_text}\nSettings source: {settings_source}",
        fontsize=12,
        fontweight="bold",
    )
    fig.savefig(path, dpi=dpi, bbox_inches="tight")
    plt.close(fig)


def make_source_crater_figure(
    path: Path,
    terrain_name: str,
    rocks: Sequence[Dict[str, Any]],
    craters: Sequence[Dict[str, Any]],
    map_size_m: float,
    minimum_source_crater_diameter_m: float,
    show_background_context: bool,
    maximum_background_points: int,
    dpi: int,
) -> Dict[str, Any]:
    rock_x, rock_y, rock_diameter, rock_crater_id = (
        rock_arrays_all(rocks, map_size_m)
    )
    crater_x, crater_y, crater_diameter = crater_arrays(
        craters,
        map_size_m,
    )
    lookup = crater_index_lookup(craters)

    rock_x_centered = rock_x - 0.5 * map_size_m
    rock_y_centered = 0.5 * map_size_m - rock_y
    crater_x_centered = crater_x - 0.5 * map_size_m
    crater_y_centered = 0.5 * map_size_m - crater_y

    valid_rock = (
        np.isfinite(rock_x_centered)
        & np.isfinite(rock_y_centered)
        & np.isfinite(rock_diameter)
        & (rock_diameter > 0)
    )
    owned_mask = np.array(
        [is_crater_owned_rock(rock) for rock in rocks],
        dtype=bool,
    ) & valid_rock

    selected_source_ids: set[int] = set()
    for source_id, crater_list_index in lookup.items():
        if (
            0 <= crater_list_index < len(crater_diameter)
            and np.isfinite(crater_diameter[crater_list_index])
            and crater_diameter[crater_list_index]
            >= minimum_source_crater_diameter_m
        ):
            selected_source_ids.add(int(source_id))

    selected_mask = owned_mask & np.array([
        int(crater_id) in selected_source_ids
        for crater_id in rock_crater_id
    ])
    source_counts = Counter(
        int(crater_id)
        for crater_id in rock_crater_id[selected_mask]
    )

    fig, axis = plt.subplots(
        1,
        1,
        figsize=(7.2, 6.6),
        constrained_layout=True,
    )

    if show_background_context:
        background_indices = np.where(valid_rock & ~selected_mask)[0]
        if background_indices.size > maximum_background_points:
            random = np.random.default_rng(2026)
            background_indices = random.choice(
                background_indices,
                size=maximum_background_points,
                replace=False,
            )
        if background_indices.size:
            axis.scatter(
                rock_x_centered[background_indices],
                rock_y_centered[background_indices],
                s=3.0,
                c="#666666",
                alpha=0.24,
                linewidths=0,
                zorder=1,
            )

    selected_ids = sorted(set(
        int(value)
        for value in rock_crater_id[selected_mask]
    ))
    color_index = {
        source_id: index
        for index, source_id in enumerate(selected_ids)
    }

    if np.any(selected_mask):
        selected_colors = np.array([
            color_index[int(source_id)]
            for source_id in rock_crater_id[selected_mask]
        ])
        marker_sizes = np.clip(
            rock_diameter[selected_mask] * 10.0,
            4.0,
            95.0,
        )
        axis.scatter(
            rock_x_centered[selected_mask],
            rock_y_centered[selected_mask],
            s=marker_sizes,
            c=selected_colors,
            cmap="turbo",
            alpha=0.86,
            linewidths=0.14,
            edgecolors="white",
            zorder=3,
        )
    else:
        axis.text(
            0.5,
            0.5,
            "No crater-owned rocks matched the selected source craters.\n"
            "Check that the crater JSON and rockfield belong to the same run.",
            ha="center",
            va="center",
            transform=axis.transAxes,
            bbox={
                "facecolor": "white",
                "edgecolor": "#888888",
                "alpha": 0.92,
                "pad": 5.0,
            },
        )

    context_indices = np.where(
        np.isfinite(crater_x_centered)
        & np.isfinite(crater_y_centered)
        & np.isfinite(crater_diameter)
        & (
            crater_diameter
            >= minimum_source_crater_diameter_m
        )
    )[0]

    for crater_list_index in context_indices:
        possible_ids = [
            source_id
            for source_id, lookup_index in lookup.items()
            if lookup_index == crater_list_index
        ]
        count = max(
            (
                source_counts.get(source_id, 0)
                for source_id in possible_ids
            ),
            default=0,
        )

        axis.add_patch(Circle(
            (
                crater_x_centered[crater_list_index],
                crater_y_centered[crater_list_index],
            ),
            0.5 * crater_diameter[crater_list_index],
            fill=False,
            edgecolor="#202020" if count else "#777777",
            linewidth=1.15 if count else 0.55,
            alpha=0.90 if count else 0.45,
            zorder=4,
        ))

        if count:
            axis.text(
                crater_x_centered[crater_list_index],
                crater_y_centered[crater_list_index],
                f"{count}",
                ha="center",
                va="center",
                fontsize=6.5,
                color="black",
                bbox={
                    "facecolor": "white",
                    "edgecolor": "none",
                    "alpha": 0.65,
                    "pad": 0.7,
                },
                zorder=5,
            )

    set_map_axes(axis, map_size_m, centered=True)
    axis.grid(
        True,
        linewidth=0.25,
        alpha=0.25,
    )
    axis.set_title(
        "Rocks grouped by source crater "
        f"(D ≥ {minimum_source_crater_diameter_m:g} m)\n"
        f"selected crater-owned rocks: "
        f"{np.count_nonzero(selected_mask):,}"
    )

    fig.suptitle(
        terrain_name,
        fontsize=12,
        fontweight="bold",
    )
    fig.savefig(path, dpi=dpi, bbox_inches="tight")
    plt.close(fig)

    return {
        "minimum_source_crater_diameter_m":
            float(minimum_source_crater_diameter_m),
        "selected_crater_owned_rock_count": int(
            np.count_nonzero(selected_mask)
        ),
        "source_crater_counts": {
            str(key): int(value)
            for key, value in source_counts.items()
        },
    }


def make_density_figure(
    path: Path,
    terrain_name: str,
    density: np.ndarray,
    craters: Sequence[Dict[str, Any]],
    map_size_m: float,
    hillshade: Optional[np.ndarray],
    minimum_crater_diameter_m: float,
    dpi: int,
    max_plot_size: int,
) -> None:
    fig, axis = plt.subplots(
        1,
        1,
        figsize=(7.0, 6.0),
        constrained_layout=True,
    )

    show_background(
        axis,
        hillshade,
        map_size_m,
        max_plot_size,
    )

    positive = density[
        np.isfinite(density) & (density > 0)
    ]
    upper = (
        float(np.percentile(positive, 98))
        if positive.size
        else 1.0
    )

    image = axis.imshow(
        density,
        cmap="inferno",
        origin="upper",
        extent=[0.0, map_size_m, map_size_m, 0.0],
        alpha=0.78,
        vmin=0.0,
        vmax=max(upper, 1e-12),
        interpolation="bilinear",
    )
    colorbar = fig.colorbar(
        image,
        ax=axis,
        fraction=0.046,
        pad=0.03,
    )
    colorbar.set_label("rocks m$^{-2}$")

    draw_crater_rims(
        axis,
        craters,
        map_size_m,
        minimum_crater_diameter_m,
        color="white",
        linewidth=0.35,
        alpha=0.50,
    )
    set_map_axes(axis, map_size_m)
    axis.set_title("Rock density field")

    fig.suptitle(
        terrain_name,
        fontsize=12,
        fontweight="bold",
    )
    fig.savefig(path, dpi=dpi, bbox_inches="tight")
    plt.close(fig)


def make_large_rock_figure(
    path: Path,
    terrain_name: str,
    rocks: Sequence[Dict[str, Any]],
    craters: Sequence[Dict[str, Any]],
    map_size_m: float,
    hillshade: Optional[np.ndarray],
    minimum_rock_diameter_m: float,
    minimum_crater_diameter_m: float,
    dpi: int,
    max_plot_size: int,
) -> int:
    rock_x, rock_y, rock_diameter, _crater_id = (
        rock_arrays_all(rocks, map_size_m)
    )
    large_mask = (
        np.isfinite(rock_x)
        & np.isfinite(rock_y)
        & np.isfinite(rock_diameter)
        & (rock_diameter >= minimum_rock_diameter_m)
    )

    fig, axis = plt.subplots(
        1,
        1,
        figsize=(7.0, 6.0),
        constrained_layout=True,
    )

    show_background(
        axis,
        hillshade,
        map_size_m,
        max_plot_size,
    )
    draw_crater_rims(
        axis,
        craters,
        map_size_m,
        minimum_crater_diameter_m,
        color="white" if hillshade is not None else "#555555",
        linewidth=0.35,
        alpha=0.45,
    )

    for x_m, y_m, diameter_m in zip(
        rock_x[large_mask],
        rock_y[large_mask],
        rock_diameter[large_mask],
    ):
        axis.add_patch(Circle(
            (x_m, y_m),
            0.5 * diameter_m,
            fill=False,
            edgecolor="#E05A00",
            linewidth=0.75,
            alpha=0.88,
        ))

    set_map_axes(axis, map_size_m)
    axis.set_title(
        f"Large-rock map (D ≥ {minimum_rock_diameter_m:g} m)\n"
        f"{np.count_nonzero(large_mask):,} rocks shown"
    )
    fig.suptitle(
        terrain_name,
        fontsize=12,
        fontweight="bold",
    )
    fig.savefig(path, dpi=dpi, bbox_inches="tight")
    plt.close(fig)

    return int(np.count_nonzero(large_mask))


# -----------------------------------------------------------------------------
# Main analysis
# -----------------------------------------------------------------------------

def analyze(args: argparse.Namespace) -> Dict[str, Any]:
    metadata_path = Path(args.metadata).expanduser().resolve()
    crater_json_path = Path(args.crater_json).expanduser().resolve()
    output_dir = Path(args.out_dir).expanduser().resolve()

    heightmap_path = (
        Path(args.heightmap).expanduser().resolve()
        if args.heightmap
        else None
    )
    rock_settings_path = (
        Path(args.rock_settings).expanduser().resolve()
        if args.rock_settings
        else None
    )

    if not metadata_path.is_file():
        raise FileNotFoundError(
            f"Metadata JSON does not exist: {metadata_path}"
        )
    if not crater_json_path.is_file():
        raise FileNotFoundError(
            f"Crater JSON does not exist: {crater_json_path}"
        )
    if heightmap_path is not None and not heightmap_path.is_file():
        raise FileNotFoundError(
            f"Heightmap does not exist: {heightmap_path}"
        )
    if (
        rock_settings_path is not None
        and not rock_settings_path.is_file()
    ):
        raise FileNotFoundError(
            f"Rock-settings JSON does not exist: "
            f"{rock_settings_path}"
        )

    ensure_dir(output_dir)
    metadata = read_json(metadata_path)
    map_size_m = (
        float(args.map_size_m)
        if args.map_size_m is not None
        else map_size_from_metadata(metadata)
    )

    rocks, rock_input_files = load_rocks(args)
    if args.exclude_random_big_rock_clumps:
        rocks = [
            rock
            for rock in rocks
            if not is_random_big_rock_clump(rock)
        ]
        if not rocks:
            raise ValueError(
                "All rocks were removed by "
                "--exclude-random-big-rock-clumps."
            )

    craters, crater_root = read_craters(crater_json_path)

    terrain_name = (
        args.name
        or clean_name(
            heightmap_path
            if heightmap_path is not None
            else crater_json_path
        )
    )

    settings, settings_source = load_zone_settings(
        rock_settings_path,
        metadata,
        terrain_name,
        crater_json_path.name,
    )

    hillshade: Optional[np.ndarray] = None
    if heightmap_path is not None:
        height_m = load_heightmap(heightmap_path, metadata)
        hillshade = compute_hillshade(
            height_m,
            map_size_m,
            args.sun_azimuth_deg,
            args.sun_elevation_deg,
        )

    rock_x, rock_y, rock_diameter, _crater_ids = (
        rock_arrays_all(rocks, map_size_m)
    )
    nearest_neighbor = nearest_neighbor_distances(
        rock_x,
        rock_y,
    )
    quadrat = quadrat_counts(
        rock_x,
        rock_y,
        map_size_m,
        args.quadrat_bins,
    )
    density = rock_density_grid(
        rock_x,
        rock_y,
        map_size_m,
        args.density_bins,
        args.density_smoothing_px,
    )

    rnorm = source_rnorms(
        rocks,
        craters,
        map_size_m,
    )
    zone_labels = crater_owned_zone_labels(
        rocks,
        rnorm,
        settings,
    )
    zone_map = build_crater_zone_map(
        craters,
        map_size_m,
        settings,
        args.zone_map_resolution,
    )

    input_files: Dict[str, Any] = {
        **rock_input_files,
        "metadata": str(metadata_path),
        "crater_json": str(crater_json_path),
        "heightmap": (
            str(heightmap_path)
            if heightmap_path is not None
            else None
        ),
        "rock_settings": (
            str(rock_settings_path)
            if rock_settings_path is not None
            else None
        ),
    }

    metrics = build_metrics(
        terrain_name,
        rocks,
        craters,
        map_size_m,
        rnorm,
        zone_labels,
        nearest_neighbor,
        quadrat,
        settings,
        settings_source,
        input_files,
    )

    configure_plot_style()
    output_files: List[Path] = []

    overview_path = output_dir / "01_rockfield_overview.png"
    make_overview_figure(
        overview_path,
        terrain_name,
        rocks,
        map_size_m,
        rnorm,
        zone_labels,
        nearest_neighbor,
        settings,
        args.dpi,
    )
    output_files.append(overview_path)

    zones_path = output_dir / "02_crater_ejecta_zones.png"
    make_ejecta_zone_figure(
        zones_path,
        terrain_name,
        craters,
        zone_map,
        settings,
        settings_source,
        map_size_m,
        hillshade,
        args.dpi,
        args.max_plot_size,
    )
    output_files.append(zones_path)

    source_path = (
        output_dir / "03_rocks_grouped_by_source_crater.png"
    )
    source_metrics = make_source_crater_figure(
        source_path,
        terrain_name,
        rocks,
        craters,
        map_size_m,
        args.source_crater_min_diameter_m,
        not args.hide_background_context,
        args.max_background_points,
        args.dpi,
    )
    metrics["source_crater_figure"] = source_metrics
    output_files.append(source_path)

    density_path = output_dir / "04_rock_density_field.png"
    make_density_figure(
        density_path,
        terrain_name,
        density,
        craters,
        map_size_m,
        hillshade,
        args.source_crater_min_diameter_m,
        args.dpi,
        args.max_plot_size,
    )
    output_files.append(density_path)

    large_rock_path = output_dir / "05_large_rock_map.png"
    large_rock_count = make_large_rock_figure(
        large_rock_path,
        terrain_name,
        rocks,
        craters,
        map_size_m,
        hillshade,
        args.large_rock_min_diameter_m,
        args.source_crater_min_diameter_m,
        args.dpi,
        args.max_plot_size,
    )
    metrics["rocks"]["large_rock_threshold_m"] = float(
        args.large_rock_min_diameter_m
    )
    metrics["rocks"]["large_rock_count"] = large_rock_count
    output_files.append(large_rock_path)

    merged_csv_path = output_dir / "merged_rocks.csv"
    write_merged_rocks_csv(
        merged_csv_path,
        rocks,
        rnorm,
        zone_labels,
    )
    output_files.append(merged_csv_path)

    metrics_csv_path = output_dir / "rockfield_metrics.csv"
    write_metrics_csv(metrics_csv_path, metrics)
    output_files.append(metrics_csv_path)

    metrics["source_crater_catalog"] = crater_root
    metrics["output_files"] = [
        str(output_file)
        for output_file in output_files
    ]

    json_path = output_dir / "rockfield_analysis.json"
    with json_path.open("w", encoding="utf-8") as file:
        json.dump(
            json_safe(metrics),
            file,
            indent=2,
            allow_nan=False,
        )
    output_files.append(json_path)

    summary_path = output_dir / "rockfield_analysis_summary.txt"
    write_summary(summary_path, metrics, output_files)
    output_files.append(summary_path)

    return {
        "terrain": terrain_name,
        "output_dir": str(output_dir),
        "output_files": [str(path) for path in output_files],
        "metrics": metrics,
    }


# -----------------------------------------------------------------------------
# Command line
# -----------------------------------------------------------------------------

def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Analyze one MoonSim rockfield and its crater ownership, "
            "ejecta zones, rock sizes, and spatial distribution."
        ),
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )

    parser.add_argument(
        "--metadata",
        required=True,
        help="Matching heightmap metadata JSON.",
    )
    parser.add_argument(
        "--crater-json",
        required=True,
        help="Matching *_rockfield_craters.json catalog.",
    )
    parser.add_argument(
        "--out-dir",
        required=True,
        help="Directory where analysis products are written.",
    )

    parser.add_argument(
        "--rockfield-dir",
        default=None,
        help=(
            "Folder containing rock_candidates.json, "
            "rock_positions.json, rock_instances.json and/or "
            "rock_metadata.csv."
        ),
    )
    parser.add_argument(
        "--rock-json",
        default=None,
        help=(
            "Explicit generic/offline/Unreal rock JSON. The root must "
            "contain rocks, instances, rock_instances, or candidates."
        ),
    )
    parser.add_argument(
        "--candidates-json",
        default=None,
        help="Explicit path to rock_candidates.json.",
    )
    parser.add_argument(
        "--positions-json",
        default=None,
        help="Explicit path to rock_positions.json.",
    )
    parser.add_argument(
        "--instances-json",
        default=None,
        help="Explicit path to rock_instances.json.",
    )
    parser.add_argument(
        "--rock-metadata-csv",
        default=None,
        help="Explicit path to rock_metadata.csv.",
    )

    parser.add_argument(
        "--heightmap",
        default=None,
        help=(
            "Optional matching PNG/R16/RAW heightmap used for "
            "hillshade backgrounds."
        ),
    )
    parser.add_argument(
        "--rock-settings",
        default=None,
        help=(
            "Optional matching rock-settings JSON. Supply it for exact "
            "ejecta-zone bounds."
        ),
    )
    parser.add_argument(
        "--name",
        default=None,
        help="Optional terrain name shown in figures.",
    )
    parser.add_argument(
        "--map-size-m",
        type=float,
        default=None,
        help="Optional map-size override; normally read from metadata.",
    )

    parser.add_argument(
        "--source-crater-min-diameter-m",
        type=float,
        default=5.0,
        help=(
            "Minimum source-crater diameter used for the grouped "
            "ownership figure and crater context."
        ),
    )
    parser.add_argument(
        "--large-rock-min-diameter-m",
        type=float,
        default=2.0,
        help="Minimum rock diameter shown in the large-rock map.",
    )
    parser.add_argument(
        "--quadrat-bins",
        type=int,
        default=32,
        help="Grid resolution used for the quadrat dispersion metric.",
    )
    parser.add_argument(
        "--density-bins",
        type=int,
        default=60,
        help="Grid resolution of the rock-density field.",
    )
    parser.add_argument(
        "--density-smoothing-px",
        type=float,
        default=1.35,
        help="Gaussian smoothing sigma for the density field.",
    )
    parser.add_argument(
        "--zone-map-resolution",
        type=int,
        default=900,
        help="Raster resolution of the crater/ejecta-zone map.",
    )
    parser.add_argument(
        "--hide-background-context",
        action="store_true",
        help=(
            "Hide non-selected rocks in the source-crater ownership "
            "figure."
        ),
    )
    parser.add_argument(
        "--max-background-points",
        type=int,
        default=12000,
        help=(
            "Maximum background/context rock points in the "
            "source-crater ownership figure."
        ),
    )
    parser.add_argument(
        "--exclude-random-big-rock-clumps",
        action="store_true",
        help=(
            "Exclude optional random big-rock-clump rocks from all "
            "analysis products."
        ),
    )

    parser.add_argument(
        "--sun-azimuth-deg",
        type=float,
        default=315.0,
        help="Optional hillshade light azimuth, clockwise from north.",
    )
    parser.add_argument(
        "--sun-elevation-deg",
        type=float,
        default=20.0,
        help="Optional hillshade light elevation.",
    )
    parser.add_argument(
        "--max-plot-size",
        type=int,
        default=1600,
        help="Maximum heightmap raster dimension used in figures.",
    )
    parser.add_argument(
        "--dpi",
        type=int,
        default=220,
        help="PNG output resolution.",
    )

    return parser


def validate_args(args: argparse.Namespace) -> None:
    has_rock_input = bool(
        args.rockfield_dir
        or args.rock_json
        or args.candidates_json
        or args.positions_json
        or args.instances_json
        or args.rock_metadata_csv
    )
    if not has_rock_input:
        raise ValueError(
            "Supply --rockfield-dir or at least one explicit rock "
            "JSON/CSV input."
        )
    if args.map_size_m is not None and args.map_size_m <= 0:
        raise ValueError("--map-size-m must be positive.")
    if args.source_crater_min_diameter_m < 0:
        raise ValueError(
            "--source-crater-min-diameter-m cannot be negative."
        )
    if args.large_rock_min_diameter_m < 0:
        raise ValueError(
            "--large-rock-min-diameter-m cannot be negative."
        )
    if args.quadrat_bins < 2:
        raise ValueError("--quadrat-bins must be at least 2.")
    if args.density_bins < 2:
        raise ValueError("--density-bins must be at least 2.")
    if args.density_smoothing_px < 0:
        raise ValueError(
            "--density-smoothing-px cannot be negative."
        )
    if args.zone_map_resolution < 128:
        raise ValueError(
            "--zone-map-resolution must be at least 128."
        )
    if args.max_background_points < 0:
        raise ValueError(
            "--max-background-points cannot be negative."
        )
    if not (0.0 <= args.sun_azimuth_deg < 360.0):
        raise ValueError(
            "--sun-azimuth-deg must be in [0, 360)."
        )
    if not (0.0 < args.sun_elevation_deg <= 90.0):
        raise ValueError(
            "--sun-elevation-deg must be in (0, 90]."
        )
    if args.max_plot_size < 128:
        raise ValueError(
            "--max-plot-size must be at least 128."
        )
    if args.dpi < 72:
        raise ValueError("--dpi must be at least 72.")


def main() -> None:
    parser = build_arg_parser()
    args = parser.parse_args()

    try:
        validate_args(args)
        result = analyze(args)
    except Exception as exc:
        print(
            f"Rockfield analysis failed: {exc}",
            file=sys.stderr,
        )
        raise SystemExit(1) from exc

    metrics = result["metrics"]
    rocks = metrics["rocks"]
    spatial = metrics["spatial"]

    print("Rockfield analysis completed.")
    print(f"  terrain: {result['terrain']}")
    print(f"  output: {result['output_dir']}")
    print(f"  rocks: {rocks['loaded_count']}")
    print(
        f"  density: {rocks['density_per_m2']:.6f} rocks/m^2"
    )
    print(
        f"  crater-owned rocks: {rocks['crater_owned_count']}"
    )
    print(
        f"  diameter median/p95/max: "
        f"{rocks['diameter_m']['median']:.3f} / "
        f"{rocks['diameter_m']['p95']:.3f} / "
        f"{rocks['diameter_m']['max']:.3f} m"
    )
    print(
        f"  Clark-Evans R: {spatial['clark_evans_r']:.3f} "
        f"({spatial['clark_evans_interpretation']})"
    )
    print("  files:")
    for path in result["output_files"]:
        print(f"    - {path}")


if __name__ == "__main__":
    main()
