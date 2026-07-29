#!/usr/bin/env python3
"""
LunarSim-PG heightmap and generated-crater analysis.

The GUI-facing script reads a generated heightmap package, computes terrain and
crater metrics, and writes portable, timestamped analysis products.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import logging
import math
import os
import platform
import re
import subprocess
import sys
from datetime import datetime, timezone
from importlib import metadata as importlib_metadata
from pathlib import Path
from typing import Any, Dict, Iterable, List, Sequence, Tuple

import numpy as np
from PIL import Image

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

try:
    from scipy.ndimage import uniform_filter
except ImportError as exc:  # pragma: no cover - dependency error is user-facing
    raise SystemExit(
        "This script requires scipy. Install dependencies with:\n"
        "  python3 -m pip install numpy pillow matplotlib scipy"
    ) from exc



PROJECT_NAME = "LunarSim-PG"
SCRIPT_NAME = "heightmap_analysis"
SCRIPT_VERSION = "2.0.0"


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
        "analysis": SCRIPT_NAME,
        "analysis_version": SCRIPT_VERSION,
        "generated_utc": generated_at.isoformat().replace("+00:00", "Z"),
        "git_commit": git_commit(),
        "runtime": {
            "python": platform.python_version(),
            "numpy": np.__version__,
            "Pillow": package_version("Pillow"),
            "matplotlib": matplotlib.__version__,
            "scipy": package_version("scipy"),
        },
    }


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def portable_path(path: Path | str, base_dir: Path) -> str:
    relative = os.path.relpath(
        Path(path).expanduser().resolve(),
        start=base_dir.expanduser().resolve(),
    )
    return Path(relative).as_posix()


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


def atomic_savefig(fig: plt.Figure, path: Path, **kwargs: Any) -> None:
    temp = temporary_path(path)
    try:
        fig.savefig(temp, format=path.suffix.lstrip("."), **kwargs)
        temp.replace(path)
    finally:
        temp.unlink(missing_ok=True)


def seed_from_metadata(metadata: Dict[str, Any]) -> int:
    value: Any = metadata.get("seed")
    if value is None and isinstance(metadata.get("settings"), dict):
        value = metadata["settings"].get("seed")
    try:
        return int(value)
    except (TypeError, ValueError):
        return 0


# -----------------------------------------------------------------------------
# General helpers
# -----------------------------------------------------------------------------


def ensure_dir(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)


def read_json(path: Path) -> Dict[str, Any]:
    with path.open("r", encoding="utf-8") as f:
        data = json.load(f)
    if not isinstance(data, dict):
        raise ValueError(f"Metadata root must be a JSON object: {path}")
    return data


def clean_name(path: Path) -> str:
    text = path.stem
    text = re.sub(r"[^0-9A-Za-z_.-]+", "_", text)
    return text.strip("_") or "heightmap"


def finite_values(data: np.ndarray) -> np.ndarray:
    arr = np.asarray(data, dtype=np.float64)
    return arr[np.isfinite(arr)]


def safe_percentile(data: np.ndarray, percentile: float) -> float:
    values = finite_values(data)
    return float(np.percentile(values, percentile)) if values.size else float("nan")


def safe_mean(data: np.ndarray) -> float:
    values = finite_values(data)
    return float(np.mean(values)) if values.size else float("nan")


def safe_median(data: np.ndarray) -> float:
    values = finite_values(data)
    return float(np.median(values)) if values.size else float("nan")


def safe_std(data: np.ndarray) -> float:
    values = finite_values(data)
    return float(np.std(values)) if values.size else float("nan")


def normalize01(data: np.ndarray, low_percentile: float = 0.5, high_percentile: float = 99.5) -> np.ndarray:
    arr = np.asarray(data, dtype=np.float32)
    values = arr[np.isfinite(arr)]
    if values.size == 0:
        return np.zeros_like(arr, dtype=np.float32)
    lo = float(np.percentile(values, low_percentile))
    hi = float(np.percentile(values, high_percentile))
    if hi <= lo:
        return np.zeros_like(arr, dtype=np.float32)
    return np.clip((arr - lo) / (hi - lo), 0.0, 1.0).astype(np.float32)


def downsample_float_image(data: np.ndarray, max_size: int) -> np.ndarray:
    arr = np.asarray(data, dtype=np.float32)
    h, w = arr.shape
    if max(h, w) <= max_size:
        return arr
    scale = max_size / float(max(h, w))
    new_w = max(1, int(round(w * scale)))
    new_h = max(1, int(round(h * scale)))
    img = Image.fromarray(arr, mode="F")
    img = img.resize((new_w, new_h), resample=Image.Resampling.BILINEAR)
    return np.asarray(img, dtype=np.float32)


def parse_thresholds(text: str) -> List[float]:
    values: List[float] = []
    for item in text.split(","):
        item = item.strip()
        if not item:
            continue
        value = float(item)
        if value < 0 or value >= 90:
            raise argparse.ArgumentTypeError("Slope thresholds must be between 0 and 90 degrees.")
        values.append(value)
    if not values:
        raise argparse.ArgumentTypeError("At least one slope threshold is required.")
    return sorted(set(values))


# -----------------------------------------------------------------------------
# Generated crater catalog
# -----------------------------------------------------------------------------

def safe_float_value(value: Any, default: float = float("nan")) -> float:
    try:
        if value is None or value == "":
            return default
        return float(value)
    except (TypeError, ValueError):
        return default


def parse_positive_thresholds(text: str) -> List[float]:
    values: List[float] = []
    for item in text.split(","):
        item = item.strip()
        if not item:
            continue
        value = float(item)
        if value <= 0:
            raise argparse.ArgumentTypeError("Crater-diameter thresholds must be positive.")
        values.append(value)
    if not values:
        raise argparse.ArgumentTypeError("At least one crater-diameter threshold is required.")
    return sorted(set(values))


def read_crater_catalog(path: Path) -> Tuple[List[Dict[str, Any]], Dict[str, Any]]:
    root = read_json(path)
    crater_items = root.get("craters")
    if not isinstance(crater_items, list):
        raise ValueError(f"Crater JSON must contain a 'craters' list: {path}")

    craters: List[Dict[str, Any]] = []
    for index, crater in enumerate(crater_items):
        if not isinstance(crater, dict):
            continue
        crater_id = int(safe_float_value(
            crater.get(
                "crater_id",
                crater.get("crater_index", crater.get("index", index)),
            ),
            index,
        ))
        craters.append({
            "crater_id": crater_id,
            "crater_index": crater_id,
            "x_m": safe_float_value(crater.get("x_m", crater.get("X_Meters"))),
            "y_m": safe_float_value(crater.get("y_m", crater.get("Y_Meters"))),
            "diameter_m": safe_float_value(
                crater.get("diameter_m", crater.get("DiameterMeters"))
            ),
            "degradation": safe_float_value(
                crater.get(
                    "degradation",
                    crater.get("degrade", crater.get("Degrade")),
                )
            ),
            "morphology": str(
                crater.get("morph", crater.get("morphology", crater.get("Morph", "")))
                or ""
            ),
        })

    if not craters:
        raise ValueError(f"No crater records were found in {path}")
    return craters, root


def crater_arrays(
    craters: Sequence[Dict[str, Any]],
    map_size_m: float,
) -> Tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    xs = np.array([safe_float_value(c.get("x_m")) for c in craters], dtype=np.float64)
    ys = np.array([safe_float_value(c.get("y_m")) for c in craters], dtype=np.float64)
    diameters = np.array(
        [safe_float_value(c.get("diameter_m")) for c in craters], dtype=np.float64
    )
    degradation = np.array(
        [safe_float_value(c.get("degradation")) for c in craters], dtype=np.float64
    )

    finite_xy = np.isfinite(xs) & np.isfinite(ys)
    if np.any(finite_xy):
        # LunarSim-PG crater catalogs may use centered coordinates (-L/2 ... +L/2)
        # or top-left coordinates (0 ... L). Convert centered coordinates for maps.
        if np.nanmin(xs[finite_xy]) < 0.0 or np.nanmin(ys[finite_xy]) < 0.0:
            xs = xs + 0.5 * map_size_m
            ys = ys + 0.5 * map_size_m
    return xs, ys, diameters, degradation



# -----------------------------------------------------------------------------
# Metadata and heightmap loading
# -----------------------------------------------------------------------------


def nested_number(data: Dict[str, Any], paths: Sequence[Sequence[str]]) -> float | None:
    for path in paths:
        current: Any = data
        found = True
        for key in path:
            if not isinstance(current, dict) or key not in current:
                found = False
                break
            current = current[key]
        if found:
            try:
                return float(current)
            except (TypeError, ValueError):
                pass
    return None


def encoded_range_from_metadata(metadata: Dict[str, Any]) -> Tuple[float, float]:
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
    if minimum is None or maximum is None:
        raise KeyError(
            "Metadata must contain unreal_import.encoded_min_m and "
            "unreal_import.encoded_max_m."
        )
    if maximum <= minimum:
        raise ValueError(f"Invalid encoded elevation range: {minimum} to {maximum} m")
    return float(minimum), float(maximum)


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
            "Metadata must contain a positive map_size_m (either at the root or in settings)."
        )
    return float(value)


def expected_heightmap_size(metadata: Dict[str, Any]) -> int | None:
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


def load_png16(path: Path) -> Tuple[np.ndarray, np.ndarray]:
    with Image.open(path) as img:
        arr = np.asarray(img)
    if arr.ndim != 2:
        raise ValueError(f"Heightmap PNG must be single-channel, got shape {arr.shape}")
    if arr.dtype != np.uint16:
        # Pillow may expose 16-bit PNGs as signed/int32 depending on platform.
        if np.issubdtype(arr.dtype, np.integer) and int(np.max(arr)) <= 65535 and int(np.min(arr)) >= 0:
            arr = arr.astype(np.uint16)
        else:
            raise ValueError(
                f"Heightmap PNG must contain 16-bit integer samples; got dtype {arr.dtype}."
            )
    raw = arr.astype(np.uint16, copy=False)
    return raw, raw.astype(np.float32)


def load_r16(path: Path, expected_size: int | None) -> Tuple[np.ndarray, np.ndarray]:
    raw_1d = np.fromfile(path, dtype="<u2")
    if expected_size is None:
        inferred = int(round(math.sqrt(raw_1d.size)))
        if inferred * inferred != raw_1d.size:
            raise ValueError(
                "Could not infer square R16/RAW dimensions. Add heightmap_size_px to metadata."
            )
        expected_size = inferred
    expected_samples = expected_size * expected_size
    if raw_1d.size != expected_samples:
        raise ValueError(
            f"{path} contains {raw_1d.size} samples, expected {expected_samples} "
            f"for {expected_size}x{expected_size}."
        )
    raw = raw_1d.reshape((expected_size, expected_size))
    return raw, raw.astype(np.float32)


def load_heightmap(path: Path, metadata: Dict[str, Any]) -> Tuple[np.ndarray, np.ndarray, float, float]:
    encoded_min_m, encoded_max_m = encoded_range_from_metadata(metadata)
    suffix = path.suffix.lower()
    if suffix == ".png":
        raw_u16, raw_float = load_png16(path)
    elif suffix in {".r16", ".raw"}:
        raw_u16, raw_float = load_r16(path, expected_heightmap_size(metadata))
    else:
        raise ValueError(f"Unsupported heightmap format: {path.suffix}. Use .png, .r16, or .raw")

    height_m = encoded_min_m + (raw_float / 65535.0) * (encoded_max_m - encoded_min_m)
    return raw_u16, height_m.astype(np.float32), encoded_min_m, encoded_max_m


# -----------------------------------------------------------------------------
# Terrain products
# -----------------------------------------------------------------------------


def terrain_gradients(height_m: np.ndarray, map_size_m: float) -> Tuple[np.ndarray, np.ndarray, float, float]:
    h, w = height_m.shape
    dx_m = map_size_m / float(max(w - 1, 1))
    dy_m = map_size_m / float(max(h - 1, 1))
    dz_dy, dz_dx = np.gradient(height_m.astype(np.float32), dy_m, dx_m)
    return dz_dx.astype(np.float32), dz_dy.astype(np.float32), dx_m, dy_m


def compute_slope_aspect(height_m: np.ndarray, map_size_m: float) -> Tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray, float, float]:
    dz_dx, dz_dy, dx_m, dy_m = terrain_gradients(height_m, map_size_m)
    gradient = np.sqrt(dz_dx * dz_dx + dz_dy * dz_dy)
    slope_deg = np.rad2deg(np.arctan(gradient)).astype(np.float32)

    # Direction of steepest descent, clockwise from map north.
    downhill_east = -dz_dx
    downhill_north = dz_dy  # image rows increase toward map south
    aspect_deg = (np.rad2deg(np.arctan2(downhill_east, downhill_north)) + 360.0) % 360.0
    aspect_deg = aspect_deg.astype(np.float32)
    aspect_deg[slope_deg < 0.05] = np.nan
    return slope_deg, aspect_deg, dz_dx, dz_dy, dx_m, dy_m


def compute_hillshade(
    slope_deg: np.ndarray,
    aspect_deg: np.ndarray,
    sun_azimuth_deg: float,
    sun_elevation_deg: float,
) -> np.ndarray:
    slope = np.deg2rad(slope_deg.astype(np.float32))
    aspect = np.deg2rad(np.nan_to_num(aspect_deg, nan=0.0).astype(np.float32))
    azimuth = np.deg2rad(float(sun_azimuth_deg))
    altitude = np.deg2rad(float(sun_elevation_deg))
    illumination = (
        np.sin(altitude) * np.cos(slope)
        + np.cos(altitude) * np.sin(slope) * np.cos(azimuth - aspect)
    )
    return normalize01(illumination, 0.5, 99.5)


def compute_local_roughness(height_m: np.ndarray, window_px: int) -> np.ndarray:
    size = max(3, int(window_px))
    if size % 2 == 0:
        size += 1
    arr = height_m.astype(np.float32, copy=False)
    local_mean = uniform_filter(arr, size=size, mode="nearest", output=np.float32)
    local_mean_sq = uniform_filter(arr * arr, size=size, mode="nearest", output=np.float32)
    variance = np.maximum(local_mean_sq - local_mean * local_mean, 0.0)
    return np.sqrt(variance).astype(np.float32)



# -----------------------------------------------------------------------------
# Metrics and reports
# -----------------------------------------------------------------------------


def array_statistics(data: np.ndarray) -> Dict[str, float]:
    values = finite_values(data)
    if values.size == 0:
        return {key: float("nan") for key in (
            "min", "max", "range", "mean", "median", "std", "p05", "p25", "p75", "p95", "p99"
        )}
    minimum = float(np.min(values))
    maximum = float(np.max(values))
    return {
        "min": minimum,
        "max": maximum,
        "range": maximum - minimum,
        "mean": float(np.mean(values)),
        "median": float(np.median(values)),
        "std": float(np.std(values)),
        "p05": float(np.percentile(values, 5)),
        "p25": float(np.percentile(values, 25)),
        "p75": float(np.percentile(values, 75)),
        "p95": float(np.percentile(values, 95)),
        "p99": float(np.percentile(values, 99)),
    }


def build_crater_metrics(
    craters: Sequence[Dict[str, Any]],
    map_size_m: float,
    diameter_thresholds_m: Sequence[float],
) -> Dict[str, Any]:
    _xs, _ys, diameters, degradation = crater_arrays(craters, map_size_m)

    valid_diameters = diameters[
        np.isfinite(diameters) & (diameters > 0)
    ]
    valid_degradation = degradation[np.isfinite(degradation)]

    area_km2 = (map_size_m * map_size_m) / 1_000_000.0
    safe_area_km2 = max(area_km2, 1e-12)

    counts = {
        f"count_diameter_ge_{threshold:g}_m": int(
            np.count_nonzero(valid_diameters >= threshold)
        )
        for threshold in diameter_thresholds_m
    }

    densities = {
        f"density_diameter_ge_{threshold:g}_m_per_km2": float(
            np.count_nonzero(valid_diameters >= threshold) / safe_area_km2
        )
        for threshold in diameter_thresholds_m
    }

    return {
        "catalog_count": int(len(craters)),
        "valid_diameter_count": int(valid_diameters.size),
        "density_per_km2": float(valid_diameters.size / safe_area_km2),
        "diameter_m": array_statistics(valid_diameters),
        "degradation": array_statistics(valid_degradation),
        **counts,
        **densities,
    }



def build_metrics(
    terrain_name: str,
    raw_u16: np.ndarray,
    height_m: np.ndarray,
    slope_deg: np.ndarray,
    roughness_m: np.ndarray,
    map_size_m: float,
    encoded_min_m: float,
    encoded_max_m: float,
    dx_m: float,
    dy_m: float,
    roughness_window_px: int,
    slope_thresholds: Sequence[float],
    sun_azimuth_deg: float,
    sun_elevation_deg: float,
) -> Dict[str, Any]:
    finite_mask = np.isfinite(height_m)
    finite_count = int(np.count_nonzero(finite_mask))
    total_count = int(height_m.size)
    area_m2 = float(map_size_m * map_size_m)

    threshold_fractions = {
        f"area_fraction_slope_ge_{threshold:g}_deg": float(np.mean(slope_deg[finite_mask] >= threshold))
        if finite_count else float("nan")
        for threshold in slope_thresholds
    }

    flat_fraction = float(np.mean(slope_deg[finite_mask] < 1.0)) if finite_count else float("nan")
    raw_min = int(np.min(raw_u16))
    raw_max = int(np.max(raw_u16))

    return {
        "format": "LunarSimHeightmapAnalysis",
        "format_version": 2,
        "terrain": terrain_name,
        "inputs": {},
        "raster": {
            "width_px": int(height_m.shape[1]),
            "height_px": int(height_m.shape[0]),
            "sample_count": total_count,
            "finite_sample_count": finite_count,
            "finite_fraction": float(finite_count / max(total_count, 1)),
            "map_size_m": float(map_size_m),
            "area_m2": area_m2,
            "area_km2": area_m2 / 1_000_000.0,
            "pixel_spacing_x_m": float(dx_m),
            "pixel_spacing_y_m": float(dy_m),
        },
        "encoding": {
            "encoded_min_m": float(encoded_min_m),
            "encoded_max_m": float(encoded_max_m),
            "encoded_range_m": float(encoded_max_m - encoded_min_m),
            "vertical_quantization_step_m": float((encoded_max_m - encoded_min_m) / 65535.0),
            "raw_min_code": raw_min,
            "raw_max_code": raw_max,
            "count_at_code_0": int(np.count_nonzero(raw_u16 == 0)),
            "count_at_code_65535": int(np.count_nonzero(raw_u16 == 65535)),
            "fraction_at_code_0": float(np.mean(raw_u16 == 0)),
            "fraction_at_code_65535": float(np.mean(raw_u16 == 65535)),
        },
        "elevation_m": array_statistics(height_m),
        "slope_deg": {
            **array_statistics(slope_deg),
            "area_fraction_slope_lt_1_deg": flat_fraction,
            **threshold_fractions,
        },
        "roughness_m": {
            **array_statistics(roughness_m),
            "window_px": int(roughness_window_px),
            "window_m_approx": float(roughness_window_px * 0.5 * (dx_m + dy_m)),
            "definition": "local elevation standard deviation in a square moving window",
        },
        "hillshade": {
            "sun_azimuth_deg": float(sun_azimuth_deg),
            "sun_elevation_deg": float(sun_elevation_deg),
            "note": "visualization setting only; not an ephemeris calculation",
        },
    }


def flatten_dict(data: Dict[str, Any], prefix: str = "") -> Iterable[Tuple[str, Any]]:
    for key, value in data.items():
        name = f"{prefix}.{key}" if prefix else key
        if isinstance(value, dict):
            yield from flatten_dict(value, name)
        elif isinstance(value, (str, int, float, bool)) or value is None:
            yield name, value


def write_metrics_csv(path: Path, metrics: Dict[str, Any]) -> None:
    excluded_roots = {"inputs"}
    rows = [
        (key, value)
        for key, value in flatten_dict(metrics)
        if key.split(".", 1)[0] not in excluded_roots
    ]
    temp = temporary_path(path)
    try:
        with temp.open("w", encoding="utf-8", newline="") as file:
            writer = csv.writer(file)
            writer.writerow(["metric", "value"])
            writer.writerows(rows)
        temp.replace(path)
    finally:
        temp.unlink(missing_ok=True)


def format_percent(value: float) -> str:
    return "n/a" if not np.isfinite(value) else f"{100.0 * value:.2f}%"


def write_summary(path: Path, metrics: Dict[str, Any], output_files: Sequence[Path]) -> None:
    raster = metrics["raster"]
    elev = metrics["elevation_m"]
    slope = metrics["slope_deg"]
    rough = metrics["roughness_m"]
    encoding = metrics["encoding"]
    craters = metrics["craters"]
    crater_diameter = craters["diameter_m"]
    crater_degradation = craters["degradation"]

    threshold_lines = []
    for key, value in slope.items():
        if key.startswith("area_fraction_slope_ge_"):
            label = key.replace("area_fraction_slope_ge_", "").replace("_deg", "°")
            threshold_lines.append(
                f"  area at or above {label}: {format_percent(float(value))}"
            )

    crater_threshold_lines = []
    for key, value in craters.items():
        if key.startswith("count_diameter_ge_"):
            label = key.replace("count_diameter_ge_", "").replace("_m", " m")
            crater_threshold_lines.append(f"  craters at or above {label}: {int(value)}")

    low_count = int(encoding["count_at_code_0"])
    high_count = int(encoding["count_at_code_65535"])
    clipping_status = "none detected" if low_count == 0 and high_count == 0 else "review endpoint saturation"

    text = "\n".join([
        "LunarSim-PG heightmap and generated-crater analysis",
        "===============================================",
        f"Terrain: {metrics['terrain']}",
        f"Raster: {raster['width_px']} x {raster['height_px']} px",
        f"Map size: {raster['map_size_m']:.3f} m x {raster['map_size_m']:.3f} m",
        f"Pixel spacing: {raster['pixel_spacing_x_m']:.4f} m x {raster['pixel_spacing_y_m']:.4f} m",
        "",
        "Elevation",
        f"  min / max: {elev['min']:.3f} / {elev['max']:.3f} m",
        f"  relief: {elev['range']:.3f} m",
        f"  mean / median: {elev['mean']:.3f} / {elev['median']:.3f} m",
        f"  standard deviation: {elev['std']:.3f} m",
        "",
        "Slope",
        f"  mean / median: {slope['mean']:.3f} / {slope['median']:.3f} deg",
        f"  p95 / p99 / max: {slope['p95']:.3f} / {slope['p99']:.3f} / {slope['max']:.3f} deg",
        *threshold_lines,
        "",
        "Generated crater population",
        f"  valid crater count: {craters['valid_diameter_count']}",
        f"  crater density: {craters['density_per_km2']:.3f} per km^2",
        (
            f"  diameter min / median / max: "
            f"{crater_diameter['min']:.3f} / {crater_diameter['median']:.3f} / "
            f"{crater_diameter['max']:.3f} m"
        ),
        (
            f"  degradation mean / median: "
            f"{crater_degradation['mean']:.3f} / {crater_degradation['median']:.3f}"
        ),
        *crater_threshold_lines,
        "",
        "Roughness",
        f"  moving window: {rough['window_px']} px (~{rough['window_m_approx']:.3f} m)",
        f"  mean / p95 / max: {rough['mean']:.3f} / {rough['p95']:.3f} / {rough['max']:.3f} m",
        "",
        "Encoding checks",
        f"  vertical quantization step: {encoding['vertical_quantization_step_m']:.6f} m",
        (
            f"  pixels at encoded minimum: {low_count} "
            f"({100.0 * encoding['fraction_at_code_0']:.4f}%)"
        ),
        (
            f"  pixels at encoded maximum: {high_count} "
            f"({100.0 * encoding['fraction_at_code_65535']:.4f}%)"
        ),
        f"  potential elevation clipping: {clipping_status}",
        "",
        "Output files",
        *[f"  {p.name}" for p in output_files],
        "",
    ])
    atomic_write_text(path, text)


# -----------------------------------------------------------------------------
# Figures
# -----------------------------------------------------------------------------


def configure_plot_style() -> None:
    plt.rcParams.update({
        "figure.dpi": 120,
        "savefig.dpi": 220,
        "font.size": 9,
        "axes.titlesize": 10,
        "axes.labelsize": 9,
        "xtick.labelsize": 8,
        "ytick.labelsize": 8,
        "axes.linewidth": 0.7,
    })


def set_map_axes(ax: plt.Axes, map_size_m: float) -> None:
    ax.set_xlim(0.0, map_size_m)
    ax.set_ylim(map_size_m, 0.0)
    ax.set_aspect("equal", adjustable="box")
    ax.set_xlabel("x (m)")
    ax.set_ylabel("y (m)")


def save_single_map(
    data: np.ndarray,
    path: Path,
    title: str,
    colorbar_label: str,
    map_size_m: float,
    cmap: str,
    dpi: int,
    max_plot_size: int,
    vmin: float | None = None,
    vmax: float | None = None,
) -> None:
    display = downsample_float_image(data, max_plot_size)
    fig, ax = plt.subplots(figsize=(6.4, 5.4), constrained_layout=True)
    image = ax.imshow(
        display,
        origin="upper",
        extent=[0.0, map_size_m, map_size_m, 0.0],
        cmap=cmap,
        interpolation="bilinear",
        vmin=vmin,
        vmax=vmax,
    )
    set_map_axes(ax, map_size_m)
    ax.set_title(title)
    cbar = fig.colorbar(image, ax=ax, fraction=0.046, pad=0.03)
    cbar.set_label(colorbar_label)
    atomic_savefig(fig, path, dpi=dpi, bbox_inches="tight")
    plt.close(fig)


def make_overview_figure(
    path: Path,
    terrain_name: str,
    height_m: np.ndarray,
    hillshade: np.ndarray,
    slope_deg: np.ndarray,
    roughness_m: np.ndarray,
    metrics: Dict[str, Any],
    map_size_m: float,
    dpi: int,
    max_plot_size: int,
) -> None:
    elevation_display = downsample_float_image(height_m, max_plot_size)
    hillshade_display = downsample_float_image(hillshade, max_plot_size)
    slope_display = downsample_float_image(slope_deg, max_plot_size)
    roughness_display = downsample_float_image(roughness_m, max_plot_size)

    fig, axes = plt.subplots(2, 3, figsize=(12.2, 7.5), constrained_layout=True)
    ax = axes.ravel()

    im = ax[0].imshow(elevation_display, cmap="cividis", origin="upper", extent=[0, map_size_m, map_size_m, 0])
    set_map_axes(ax[0], map_size_m)
    ax[0].set_title("Elevation")
    fig.colorbar(im, ax=ax[0], fraction=0.046, pad=0.02, label="m")

    ax[1].imshow(hillshade_display, cmap="gray", origin="upper", extent=[0, map_size_m, map_size_m, 0], vmin=0, vmax=1)
    set_map_axes(ax[1], map_size_m)
    ax[1].set_title("Hillshade")

    slope_vmax = max(5.0, safe_percentile(slope_deg, 99.0))
    im = ax[2].imshow(slope_display, cmap="magma", origin="upper", extent=[0, map_size_m, map_size_m, 0], vmin=0, vmax=slope_vmax)
    set_map_axes(ax[2], map_size_m)
    ax[2].set_title("Slope")
    fig.colorbar(im, ax=ax[2], fraction=0.046, pad=0.02, label="deg")

    rough_vmax = max(0.001, safe_percentile(roughness_m, 99.0))
    im = ax[3].imshow(roughness_display, cmap="viridis", origin="upper", extent=[0, map_size_m, map_size_m, 0], vmin=0, vmax=rough_vmax)
    set_map_axes(ax[3], map_size_m)
    ax[3].set_title("Local elevation roughness")
    fig.colorbar(im, ax=ax[3], fraction=0.046, pad=0.02, label="m std")

    h, w = height_m.shape
    x = np.linspace(-0.5 * map_size_m, 0.5 * map_size_m, w)
    y = np.linspace(-0.5 * map_size_m, 0.5 * map_size_m, h)
    centre_row = height_m[h // 2, :].astype(np.float64)
    centre_col = height_m[:, w // 2].astype(np.float64)
    reference = float(np.nanmedian(height_m))
    ax[4].plot(x, centre_row - reference, label="west-east centreline")
    ax[4].plot(y, centre_col - reference, label="north-south centreline")
    ax[4].set_xlabel("distance from map centre (m)")
    ax[4].set_ylabel("elevation relative to median (m)")
    ax[4].set_title("Central terrain profiles")
    ax[4].grid(True, linewidth=0.35, alpha=0.35)
    ax[4].legend(frameon=False)

    raster = metrics["raster"]
    elev = metrics["elevation_m"]
    slope = metrics["slope_deg"]
    rough = metrics["roughness_m"]
    encoding = metrics["encoding"]
    summary_lines = [
        f"Raster: {raster['width_px']} × {raster['height_px']} px",
        f"Map: {raster['map_size_m']:.1f} × {raster['map_size_m']:.1f} m",
        f"Pixel spacing: {raster['pixel_spacing_x_m']:.3f} m",
        "",
        f"Elevation relief: {elev['range']:.2f} m",
        f"Elevation std: {elev['std']:.2f} m",
        "",
        f"Mean slope: {slope['mean']:.2f}°",
        f"95th percentile slope: {slope['p95']:.2f}°",
        f"Maximum slope: {slope['max']:.2f}°",
        "",
        f"Mean roughness: {rough['mean']:.3f} m",
        f"95th percentile roughness: {rough['p95']:.3f} m",
        "",
        f"16-bit vertical step: {encoding['vertical_quantization_step_m']:.5f} m",
        (
            f"Encoded minimum pixels: {encoding['count_at_code_0']} "
            f"({100*encoding['fraction_at_code_0']:.4f}%)"
        ),
        (
            f"Encoded maximum pixels: {encoding['count_at_code_65535']} "
            f"({100*encoding['fraction_at_code_65535']:.4f}%)"
        ),
        (
            "Potential clipping: none detected"
            if encoding['count_at_code_0'] == 0 and encoding['count_at_code_65535'] == 0
            else "Potential clipping: review endpoint saturation"
        ),
    ]
    ax[5].axis("off")
    ax[5].text(0.03, 0.97, "\n".join(summary_lines), transform=ax[5].transAxes, va="top", ha="left", family="monospace")
    ax[5].set_title("Summary")

    fig.suptitle(f"Heightmap analysis — {terrain_name}", fontsize=14, fontweight="bold")
    atomic_savefig(fig, path, dpi=dpi, bbox_inches="tight")
    plt.close(fig)


def make_statistics_figure(
    path: Path,
    terrain_name: str,
    height_m: np.ndarray,
    slope_deg: np.ndarray,
    craters: Sequence[Dict[str, Any]],
    map_size_m: float,
    slope_thresholds: Sequence[float],
    dpi: int,
) -> None:
    elevation = finite_values(height_m)
    slope = finite_values(slope_deg)
    _cx, _cy, crater_diameters, crater_degradation = crater_arrays(craters, map_size_m)
    crater_diameters = crater_diameters[
        np.isfinite(crater_diameters) & (crater_diameters > 0)
    ]
    area_km2 = (map_size_m * map_size_m) / 1_000_000.0

    fig, axes = plt.subplots(2, 2, figsize=(10.8, 7.4), constrained_layout=True)

    # Elevation distribution
    elevation_ax = axes[0, 0]

    elevation_median = float(np.median(elevation))
    elevation_mean = float(np.mean(elevation))

    elevation_ax.hist(
        elevation,
        bins=70,
        color="#C9D7E3",
        edgecolor="#8CA3B5",
        linewidth=0.35,
        alpha=0.95,
        zorder=1,
    )

    # Median
    elevation_ax.axvline(
        elevation_median,
        color="#2E7D32",
        linestyle="--",
        linewidth=1.8,
        zorder=4,
    )

    elevation_ax.annotate(
        f"median: {elevation_median:.2f} m",
        xy=(elevation_median, 1.0),
        xycoords=("data", "axes fraction"),
        xytext=(5, -7),
        textcoords="offset points",
        ha="left",
        va="top",
        fontsize=8,
        color="#2E7D32",
        bbox={
            "facecolor": "white",
            "edgecolor": "#2E7D32",
            "alpha": 0.90,
            "boxstyle": "round,pad=0.2",
        },
    )

    # Mean
    elevation_ax.axvline(
        elevation_mean,
        color="#E05A00",
        linestyle=":",
        linewidth=1.5,
        zorder=4,
    )

    elevation_ax.annotate(
        f"mean: {elevation_mean:.2f} m",
        xy=(elevation_mean, 0.88),
        xycoords=("data", "axes fraction"),
        xytext=(5, 0),
        textcoords="offset points",
        ha="left",
        va="top",
        fontsize=8,
        color="#E05A00",
        bbox={
            "facecolor": "white",
            "edgecolor": "#E05A00",
            "alpha": 0.88,
            "boxstyle": "round,pad=0.2",
        },
    )

    elevation_ax.set_xlabel("elevation (m)")
    elevation_ax.set_ylabel("samples")
    elevation_ax.set_title("Elevation distribution")
    elevation_ax.grid(
        True,
        axis="y",
        linewidth=0.4,
        alpha=0.28,
        zorder=0,
    )

    # Slope distribution
    slope_ax = axes[0, 1]

    maximum_slope = float(np.max(slope))
    visible_thresholds = [
        threshold
        for threshold in slope_thresholds
        if threshold <= maximum_slope
    ]

    # Prevent isolated extreme pixels from stretching the graph unnecessarily,
    # while still ensuring that every relevant threshold is visible.
    upper_slope = max(
        5.0,
        float(np.percentile(slope, 99.9)),
        max(visible_thresholds, default=0.0),
    )

    upper_slope = min(
        maximum_slope,
        upper_slope * 1.04,
    )

    slope_ax.hist(
        slope,
        bins=70,
        range=(0.0, upper_slope),
        color="#D1DCE5",
        edgecolor="#879EAE",
        linewidth=0.35,
        alpha=0.95,
        zorder=1,
    )

    threshold_palette = [
        "#2E7D32",  # green
        "#D18B00",  # amber
        "#E05A00",  # orange
        "#C62828",  # red
        "#6A1B9A",  # purple
    ]

    for index, threshold in enumerate(visible_thresholds):
        if threshold > upper_slope:
            continue

        line_color = threshold_palette[
            min(index, len(threshold_palette) - 1)
        ]

        slope_ax.axvline(
            threshold,
            color=line_color,
            linestyle="--",
            linewidth=1.6,
            zorder=4,
        )

        slope_ax.annotate(
            f"{threshold:g}°",
            xy=(threshold, 1.0),
            xycoords=("data", "axes fraction"),
            xytext=(4, -7),
            textcoords="offset points",
            ha="left",
            va="top",
            fontsize=8,
            fontweight="bold",
            color=line_color,
            bbox={
                "facecolor": "white",
                "edgecolor": line_color,
                "alpha": 0.90,
                "boxstyle": "round,pad=0.18",
            },
        )

    slope_ax.set_xlabel("slope (deg)")
    slope_ax.set_ylabel("samples")
    slope_ax.set_title("Slope distribution")
    slope_ax.grid(
        True,
        axis="y",
        linewidth=0.4,
        alpha=0.28,
        zorder=0,
    )

    # Crater differential histogram + cumulative size-frequency distribution
    crater_ax = axes[1, 0]
    if crater_diameters.size:
        d_min = float(np.min(crater_diameters))
        d_max = float(np.max(crater_diameters))
        if d_max > d_min * 1.001:
            bin_count = int(np.clip(np.sqrt(crater_diameters.size), 8, 20))
            edges = np.geomspace(d_min, d_max * 1.000001, bin_count + 1)
        else:
            edges = np.array([0.9 * d_min, 1.1 * d_max + 1e-9])

        counts, edges = np.histogram(crater_diameters, bins=edges)
        centres = np.sqrt(edges[:-1] * edges[1:])
        widths = edges[1:] - edges[:-1]
        crater_ax.bar(
            centres, counts, width=widths, align="center", alpha=0.55,
            label="diameter-bin count"
        )
        crater_ax.set_xscale("log")
        crater_ax.set_xlabel("crater diameter (m)")
        crater_ax.set_ylabel("craters per diameter bin")
        crater_ax.grid(True, which="both", linewidth=0.3, alpha=0.3)

        sorted_d = np.sort(crater_diameters)
        cumulative_density = (
            np.arange(sorted_d.size, 0, -1, dtype=np.float64)
            / max(area_km2, 1e-12)
        )
        cumulative_ax = crater_ax.twinx()
        cumulative_ax.step(
            sorted_d, cumulative_density, where="post",
            linewidth=1.4, label=r"cumulative $N(\geq D)$"
        )
        cumulative_ax.set_yscale("log")
        cumulative_ax.set_ylabel(r"cumulative density $N(\geq D)$ (km$^{-2}$)")

        handles_1, labels_1 = crater_ax.get_legend_handles_labels()
        handles_2, labels_2 = cumulative_ax.get_legend_handles_labels()
        crater_ax.legend(
            handles_1 + handles_2, labels_1 + labels_2,
            frameon=False, loc="upper right"
        )
    else:
        crater_ax.text(
            0.5, 0.5, "No valid crater diameters",
            ha="center", va="center", transform=crater_ax.transAxes
        )
    crater_ax.set_title("Crater size-frequency distribution")

    # Degradation versus crater diameter
    degradation_ax = axes[1, 1]
    _cx, _cy, all_diameters, all_degradation = crater_arrays(
        craters,
        map_size_m,
    )

    valid = (
        np.isfinite(all_diameters)
        & (all_diameters > 0)
        & np.isfinite(all_degradation)
    )

    if np.any(valid):
        d = all_diameters[valid]
        degradation = all_degradation[valid]

        # Individual craters
        degradation_ax.scatter(
            d,
            degradation,
            s=12,
            alpha=0.32,
            linewidths=0,
            zorder=1,
        )

        degradation_ax.set_xscale("log")

        # ---------------------------------------------------------
        # Smooth median trend
        # ---------------------------------------------------------

        minimum_points_per_group = 15
        maximum_groups = 10

        number_of_groups = min(
            maximum_groups,
            d.size // minimum_points_per_group,
        )

        if number_of_groups >= 3:
            order = np.argsort(d)
            sorted_diameters = d[order]
            sorted_degradation = degradation[order]

            # Equal-population groups give every median similar support.
            diameter_groups = np.array_split(
                sorted_diameters,
                number_of_groups,
            )

            degradation_groups = np.array_split(
                sorted_degradation,
                number_of_groups,
            )

            median_diameters = []
            median_degradation = []

            for diameter_group, degradation_group in zip(
                diameter_groups,
                degradation_groups,
            ):
                if degradation_group.size < minimum_points_per_group:
                    continue

                median_diameters.append(
                    float(np.median(diameter_group))
                )

                median_degradation.append(
                    float(np.median(degradation_group))
                )

            if len(median_diameters) >= 3:
                median_diameters_array = np.asarray(
                    median_diameters,
                    dtype=np.float64,
                )

                median_degradation_array = np.asarray(
                    median_degradation,
                    dtype=np.float64,
                )

                # Interpolate in logarithmic diameter space.
                median_log_diameters = np.log10(
                    median_diameters_array
                )

                dense_log_diameters = np.linspace(
                    median_log_diameters[0],
                    median_log_diameters[-1],
                    300,
                )

                interpolated_degradation = np.interp(
                    dense_log_diameters,
                    median_log_diameters,
                    median_degradation_array,
                )

                # Apply a small Gaussian smoothing kernel.
                smoothing_sigma = 7.0
                kernel_radius = int(3 * smoothing_sigma)

                kernel_positions = np.arange(
                    -kernel_radius,
                    kernel_radius + 1,
                    dtype=np.float64,
                )

                smoothing_kernel = np.exp(
                    -0.5
                    * (kernel_positions / smoothing_sigma) ** 2
                )

                smoothing_kernel /= np.sum(smoothing_kernel)

                padded_degradation = np.pad(
                    interpolated_degradation,
                    (kernel_radius, kernel_radius),
                    mode="edge",
                )

                smoothed_degradation = np.convolve(
                    padded_degradation,
                    smoothing_kernel,
                    mode="same",
                )[kernel_radius:-kernel_radius]

                degradation_ax.plot(
                    10.0 ** dense_log_diameters,
                    smoothed_degradation,
                    linewidth=2.2,
                    label="smoothed median trend",
                    zorder=4,
                )

        degradation_ax.set_xlabel("crater diameter (m)")
        degradation_ax.set_ylabel("degradation")

        degradation_ax.grid(
            True,
            which="both",
            linewidth=0.3,
            alpha=0.3,
        )

        handles, labels = degradation_ax.get_legend_handles_labels()
        if handles:
            degradation_ax.legend(
                handles, labels, frameon=False, loc="best"
            )

    else:
        degradation_ax.text(
            0.5,
            0.5,
            "No crater degradation values",
            ha="center",
            va="center",
            transform=degradation_ax.transAxes,
        )

        degradation_ax.set_xlabel("crater diameter (m)")
        degradation_ax.set_ylabel("degradation")

    degradation_ax.set_title(
        "Crater degradation versus diameter"
    )

    fig.suptitle(
        f"Heightmap and crater statistics — {terrain_name}",
        fontsize=14,
        fontweight="bold",
    )

    atomic_savefig(
        fig, path, dpi=dpi, bbox_inches="tight"
    )

    plt.close(fig)


# -----------------------------------------------------------------------------
# Main analysis
# -----------------------------------------------------------------------------


def analyze(args: argparse.Namespace) -> Dict[str, Any]:
    heightmap_path = Path(args.heightmap).expanduser().resolve()
    metadata_path = Path(args.metadata).expanduser().resolve()
    crater_json_path = Path(args.crater_json).expanduser().resolve()
    out_dir = Path(args.out_dir).expanduser().resolve()

    if not heightmap_path.is_file():
        raise FileNotFoundError(f"Heightmap does not exist: {heightmap_path}")
    if not metadata_path.is_file():
        raise FileNotFoundError(f"Metadata JSON does not exist: {metadata_path}")
    if not crater_json_path.is_file():
        raise FileNotFoundError(f"Crater JSON does not exist: {crater_json_path}")
    ensure_dir(out_dir)
    metadata = read_json(metadata_path)
    generated_at = utc_now()
    raw_u16, height_m, encoded_min_m, encoded_max_m = load_heightmap(heightmap_path, metadata)

    expected_size = expected_heightmap_size(metadata)
    if expected_size is not None and height_m.shape != (expected_size, expected_size):
        raise ValueError(
            f"Metadata expects {expected_size}x{expected_size}, but the heightmap is "
            f"{height_m.shape[1]}x{height_m.shape[0]}."
        )

    map_size_m = float(args.map_size_m) if args.map_size_m is not None else map_size_from_metadata(metadata)
    slope_deg, aspect_deg, _dz_dx, _dz_dy, dx_m, dy_m = compute_slope_aspect(height_m, map_size_m)
    hillshade = compute_hillshade(slope_deg, aspect_deg, args.sun_azimuth_deg, args.sun_elevation_deg)

    mean_spacing = 0.5 * (dx_m + dy_m)
    if args.roughness_window_m is not None:
        roughness_window_px = max(3, int(round(float(args.roughness_window_m) / mean_spacing)))
    else:
        roughness_window_px = int(args.roughness_window_px)
    if roughness_window_px % 2 == 0:
        roughness_window_px += 1
    roughness_m = compute_local_roughness(height_m, roughness_window_px)

    terrain_name = (
        args.name or str(metadata.get("preset") or "").strip()
        or clean_name(heightmap_path)
    )
    seed = seed_from_metadata(metadata)
    prefix = (
        f"{utc_file_timestamp(generated_at)}_"
        f"{safe_name(terrain_name)}_seed{seed}"
    )
    planned_output_paths = [
        out_dir / f"{prefix}_01_elevation_map.png",
        out_dir / f"{prefix}_02_hillshade.png",
        out_dir / f"{prefix}_03_slope_map.png",
        out_dir / f"{prefix}_04_roughness_map.png",
        out_dir / f"{prefix}_05_heightmap_overview.png",
        out_dir / f"{prefix}_06_heightmap_statistics.png",
        out_dir / f"{prefix}_heightmap_analysis.json",
        out_dir / f"{prefix}_heightmap_metrics.csv",
        out_dir / f"{prefix}_heightmap_analysis_summary.txt",
    ]
    existing = [path for path in planned_output_paths if path.exists()]
    if existing and not args.overwrite:
        raise FileExistsError(
            "Refusing to overwrite existing analysis outputs: "
            + ", ".join(str(path) for path in existing)
        )
    craters, crater_json_root = read_crater_catalog(crater_json_path)
    crater_metrics = build_crater_metrics(
        craters, map_size_m, args.crater_diameter_thresholds
    )
    metrics = build_metrics(
        terrain_name=terrain_name,
        raw_u16=raw_u16,
        height_m=height_m,
        slope_deg=slope_deg,
        roughness_m=roughness_m,
        map_size_m=map_size_m,
        encoded_min_m=encoded_min_m,
        encoded_max_m=encoded_max_m,
        dx_m=dx_m,
        dy_m=dy_m,
        roughness_window_px=roughness_window_px,
        slope_thresholds=args.slope_thresholds,
        sun_azimuth_deg=args.sun_azimuth_deg,
        sun_elevation_deg=args.sun_elevation_deg,
    )

    metrics["provenance"] = build_provenance(generated_at)
    metrics["seed"] = seed
    metrics["path_base"] = "this_json_directory"
    metrics["inputs"] = {
        "heightmap": portable_path(heightmap_path, out_dir),
        "metadata": portable_path(metadata_path, out_dir),
        "crater_json": portable_path(crater_json_path, out_dir),
    }
    metrics["source_checksums_sha256"] = {
        "heightmap": sha256_file(heightmap_path),
        "metadata": sha256_file(metadata_path),
        "crater_json": sha256_file(crater_json_path),
    }
    metrics["craters"] = crater_metrics
    metrics["source_crater_catalog"] = {
        "format": crater_json_root.get("format"),
        "format_version": crater_json_root.get(
            "format_version", crater_json_root.get("version")
        ),
        "declared_count": len(crater_json_root.get("craters", [])),
    }

    configure_plot_style()
    output_files: List[Path] = []

    elevation_path = out_dir / f"{prefix}_01_elevation_map.png"
    save_single_map(
        height_m, elevation_path, f"Elevation — {terrain_name}", "elevation (m)",
        map_size_m, "cividis", args.dpi, args.max_plot_size,
    )
    output_files.append(elevation_path)

    hillshade_path = out_dir / f"{prefix}_02_hillshade.png"
    save_single_map(
        hillshade, hillshade_path,
        f"Hillshade — {terrain_name} (azimuth {args.sun_azimuth_deg:g}°, elevation {args.sun_elevation_deg:g}°)",
        "normalized illumination", map_size_m, "gray", args.dpi, args.max_plot_size, 0.0, 1.0,
    )
    output_files.append(hillshade_path)

    slope_path = out_dir / f"{prefix}_03_slope_map.png"
    save_single_map(
        slope_deg, slope_path, f"Slope — {terrain_name}", "slope (deg)",
        map_size_m, "magma", args.dpi, args.max_plot_size,
        0.0, max(5.0, safe_percentile(slope_deg, 99.0)),
    )
    output_files.append(slope_path)

    roughness_path = out_dir / f"{prefix}_04_roughness_map.png"
    save_single_map(
        roughness_m, roughness_path,
        f"Local elevation roughness — {terrain_name} ({roughness_window_px} px window)",
        "local elevation std (m)", map_size_m, "viridis", args.dpi, args.max_plot_size,
        0.0, max(0.001, safe_percentile(roughness_m, 99.0)),
    )
    output_files.append(roughness_path)

    overview_path = out_dir / f"{prefix}_05_heightmap_overview.png"
    make_overview_figure(
        overview_path, terrain_name, height_m, hillshade, slope_deg, roughness_m,
        metrics, map_size_m, args.dpi, args.max_plot_size,
    )
    output_files.append(overview_path)

    statistics_path = out_dir / f"{prefix}_06_heightmap_statistics.png"
    make_statistics_figure(
        statistics_path, terrain_name, height_m, slope_deg, craters,
        map_size_m, args.slope_thresholds, args.dpi,
    )
    output_files.append(statistics_path)

    json_path = out_dir / f"{prefix}_heightmap_analysis.json"
    csv_path = out_dir / f"{prefix}_heightmap_metrics.csv"
    summary_path = out_dir / f"{prefix}_heightmap_analysis_summary.txt"
    final_output_files = output_files + [json_path, csv_path, summary_path]
    metrics["output_files"] = [path.name for path in final_output_files]
    atomic_write_json(json_path, metrics)
    write_metrics_csv(csv_path, metrics)
    write_summary(summary_path, metrics, final_output_files)
    output_files = final_output_files

    return {
        "terrain": terrain_name,
        "output_dir": str(out_dir),
        "output_files": [str(path) for path in output_files],
        "metrics": metrics,
    }


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Analyze one LunarSim-PG heightmap and its generated crater catalog; no rockfield is required.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("--heightmap", required=True, help="16-bit PNG, R16, or RAW heightmap.")
    parser.add_argument("--metadata", required=True, help="Matching heightmap metadata JSON.")
    parser.add_argument(
        "--crater-json", required=True,
        help="Matching generated crater catalogue JSON.",
    )
    parser.add_argument("--out-dir", required=True, help="Directory where analysis products are written.")
    parser.add_argument("--name", default=None, help="Optional terrain name shown in figures and reports.")
    parser.add_argument(
        "--map-size-m", type=float, default=None,
        help="Optional map-size override. Normally read from metadata.",
    )
    parser.add_argument("--sun-azimuth-deg", type=float, default=315.0, help="Hillshade light azimuth, clockwise from north.")
    parser.add_argument("--sun-elevation-deg", type=float, default=20.0, help="Hillshade light elevation above the horizon.")
    roughness = parser.add_mutually_exclusive_group()
    roughness.add_argument(
        "--roughness-window-px", type=int, default=11,
        help="Odd moving-window size used for local elevation roughness.",
    )
    roughness.add_argument(
        "--roughness-window-m", type=float, default=None,
        help="Approximate physical roughness window; overrides --roughness-window-px.",
    )
    parser.add_argument(
        "--slope-thresholds-deg", dest="slope_thresholds", type=parse_thresholds,
        default=parse_thresholds("5,10,15,20,30"),
        help="Comma-separated slope thresholds used in the report.",
    )
    parser.add_argument(
        "--crater-diameter-thresholds-m",
        dest="crater_diameter_thresholds",
        type=parse_positive_thresholds,
        default=parse_positive_thresholds("1,5,10,20"),
        help="Comma-separated crater-diameter thresholds used in the report.",
    )
    parser.add_argument("--max-plot-size", type=int, default=1600, help="Maximum raster dimension used in figures.")
    parser.add_argument("--dpi", type=int, default=220, help="PNG output resolution.")
    parser.add_argument("--overwrite", action="store_true", help="Allow replacement of an identical timestamped output name.")
    return parser


def validate_args(args: argparse.Namespace) -> None:
    if args.map_size_m is not None and args.map_size_m <= 0:
        raise ValueError("--map-size-m must be positive.")
    if not (0.0 <= args.sun_azimuth_deg < 360.0):
        raise ValueError("--sun-azimuth-deg must be in [0, 360).")
    if not (0.0 < args.sun_elevation_deg <= 90.0):
        raise ValueError("--sun-elevation-deg must be in (0, 90].")
    if args.roughness_window_px < 3:
        raise ValueError("--roughness-window-px must be at least 3.")
    if args.roughness_window_m is not None and args.roughness_window_m <= 0:
        raise ValueError("--roughness-window-m must be positive.")
    if args.max_plot_size < 128:
        raise ValueError("--max-plot-size must be at least 128.")
    if args.dpi < 72:
        raise ValueError("--dpi must be at least 72.")


def main() -> None:
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    parser = build_arg_parser()
    args = parser.parse_args()
    try:
        validate_args(args)
        result = analyze(args)
    except Exception as exc:
        print(f"Heightmap analysis failed: {exc}", file=sys.stderr)
        raise SystemExit(1) from exc

    metrics = result["metrics"]
    elevation = metrics["elevation_m"]
    slope = metrics["slope_deg"]
    roughness = metrics["roughness_m"]
    craters = metrics["craters"]

    print("Heightmap and crater analysis completed.")
    print(f"  terrain: {result['terrain']}")
    print(f"  output: {result['output_dir']}")
    print(f"  elevation min/max: {elevation['min']:.3f} / {elevation['max']:.3f} m")
    print(f"  relief: {elevation['range']:.3f} m")
    print(f"  slope mean/p95/max: {slope['mean']:.3f} / {slope['p95']:.3f} / {slope['max']:.3f} deg")
    print(f"  roughness mean/p95: {roughness['mean']:.3f} / {roughness['p95']:.3f} m")
    print(
        f"  craters: {craters['valid_diameter_count']} "
        f"({craters['density_per_km2']:.3f} per km^2)"
    )
    print(
        f"  crater diameter min/median/max: "
        f"{craters['diameter_m']['min']:.3f} / "
        f"{craters['diameter_m']['median']:.3f} / "
        f"{craters['diameter_m']['max']:.3f} m"
    )
    print("  files:")
    for path in result["output_files"]:
        print(f"    - {path}")


if __name__ == "__main__":
    main()
