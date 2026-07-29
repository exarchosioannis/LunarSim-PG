#!/usr/bin/env bash

set -euo pipefail

usage() {
    cat <<'EOF'
Usage: moonsim.sh [OPTION]

Launch the MoonSim terrain asset generator GUI.

Options:
  -h, --help       Show this help and exit.
  --check-deps     Verify the Python modules used by generation and analysis.

The supported end-to-end workflow is graphical.
EOF
}

check_dependencies() {
    command -v python3 >/dev/null 2>&1 || {
        printf 'Error: python3 is not installed.\n' >&2
        return 1
    }

    python3 -c 'import tkinter, numpy, PIL, matplotlib, scipy' >/dev/null 2>&1 || {
        cat >&2 <<'EOF'
Error: MoonSim terrain-tool Python dependencies are missing.
On Ubuntu, install them with:
  sudo apt-get update
  sudo apt-get install -y python3-tk python3-numpy python3-pil python3-matplotlib python3-scipy
EOF
        return 1
    }
}

case "${1:-}" in
    -h|--help)
        usage
        exit 0
        ;;
    --check-deps)
        [[ "$#" -eq 1 ]] || {
            printf 'Error: --check-deps does not accept additional arguments.\n' >&2
            exit 2
        }
        check_dependencies
        printf 'MoonSim terrain-tool Python dependencies are available.\n'
        exit 0
        ;;
    "")
        ;;
    *)
        printf 'Error: unknown option: %s\n\n' "$1" >&2
        usage >&2
        exit 2
        ;;
esac

check_dependencies

if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
    printf 'Error: no graphical desktop display was detected (DISPLAY and WAYLAND_DISPLAY are unset).\n' >&2
    exit 1
fi

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR"
HEIGHTMAP_DIR="$SCRIPT_DIR"

GENERATED_DIR="$PROJECT_DIR/generated"
GENERATED_HEIGHTMAPS_DIR="$GENERATED_DIR/heightmaps"
GENERATED_ROCKFIELDS_DIR="$GENERATED_DIR/rockfields"
UNREAL_IMPORT_DIR="$PROJECT_DIR/unreal_import"
UNREAL_HEIGHTMAP_DIR="$UNREAL_IMPORT_DIR/heightmaps"
UNREAL_ROCKFIELD_DIR="$UNREAL_IMPORT_DIR/rockfields"
LEGACY_FINAL_DIR="$HEIGHTMAP_DIR/final"

HEIGHTMAP_SCRIPT="$SCRIPT_DIR/heightmap_generator.py"
ROCKFIELD_SCRIPT="$SCRIPT_DIR/rockfield_generator.py"
HEIGHTMAP_ANALYSIS_SCRIPT="$SCRIPT_DIR/heightmap_analysis.py"
ROCKFIELD_ANALYSIS_SCRIPT="$SCRIPT_DIR/rockfield_analysis.py"
ANALYSIS_RESULTS_DIR="$PROJECT_DIR/analysis_results"

python3 - "$PROJECT_DIR" "$HEIGHTMAP_DIR" "$GENERATED_DIR" "$UNREAL_ROCKFIELD_DIR" "$UNREAL_HEIGHTMAP_DIR" "$GENERATED_HEIGHTMAPS_DIR" "$LEGACY_FINAL_DIR" "$HEIGHTMAP_SCRIPT" "$ROCKFIELD_SCRIPT" "$HEIGHTMAP_ANALYSIS_SCRIPT" "$ANALYSIS_RESULTS_DIR" "$ROCKFIELD_ANALYSIS_SCRIPT" <<'PY'
import csv
import copy
import json
import os
import queue
import re
import subprocess
import shutil
import sys
import tempfile
import threading
import time
from pathlib import Path
import tkinter as tk
import tkinter.font as tkfont
from tkinter import ttk, messagebox, filedialog

PROJECT_DIR = Path(sys.argv[1])
HEIGHTMAP_DIR = Path(sys.argv[2])
FINAL_DIR = Path(sys.argv[3])  # generated root
HEIGHTMAP_GENERATIONS_DIR = FINAL_DIR / "heightmaps"
ROCKFIELD_GENERATIONS_DIR = FINAL_DIR / "rockfields"
UNREAL_ROCKFIELD_DIR = Path(sys.argv[4])
HEIGHTMAP_PNG_DIR = Path(sys.argv[5])  # unreal_import/heightmaps
CRATER_JSON_DIR = Path(sys.argv[6])  # compatibility alias: generated/heightmaps
LEGACY_FINAL_DIR = Path(sys.argv[7])  # compatibility for older rockfield scripts that still write Heightmaps/final
HEIGHTMAP_SCRIPT = Path(sys.argv[8])
ROCKFIELD_SCRIPT = Path(sys.argv[9])
HEIGHTMAP_ANALYSIS_SCRIPT = Path(sys.argv[10])
ANALYSIS_RESULTS_DIR = Path(sys.argv[11])
ROCKFIELD_ANALYSIS_SCRIPT = Path(sys.argv[12])
ANALYSIS_HEIGHTMAPS_DIR = ANALYSIS_RESULTS_DIR / "heightmaps"
ANALYSIS_ROCKFIELDS_DIR = ANALYSIS_RESULTS_DIR / "rockfields"


def read_json(path):
    path = Path(path)
    with path.open("r", encoding="utf-8") as file:
        data = json.load(file)
    if not isinstance(data, dict):
        raise ValueError(f"JSON root must be an object: {path}")
    return data

MATERIALS = ["MareBasalt", "HighlandAnorthosite", "MixedBreccia", "FreshEjecta", "RegolithCovered"]
CLUSTER_BIASES = ["rim", "proximal", "rim_proximal", "all_ejecta"]
BACKGROUND_CAP_MODES = ["maxrocks", "unreal"]
RANGE_MODES = ["fixed", "actual"]
HEIGHTMAP_PRESET_CHOICES = [
    "mare_scientific",
    "apollo17_scientific",
    "highland_scientific",
    "fresh_crater_scientific",
    "custom_scientific",
]
ROCK_PROFILE_CHOICES = [
    "mare",
    "apollo_17",
    "polar_highlands",
    "new_fresh_zone",
    "custom",
]

# Crater size-frequency distribution definitions used when the GUI writes the
# --segments-json override. The user-facing K controls change abundance while
# the preset keeps its original size ranges and cumulative exponents.
CRATER_SEGMENT_TEMPLATES = {
    "mare_scientific": [
        {"min_diameter_m": 2.5, "max_diameter_m": 50.0, "b": 2.00},
        {"min_diameter_m": 50.0, "max_diameter_m": 250.0, "b": 2.00},
    ],
    "apollo17_scientific": [
        {"min_diameter_m": 2.5, "max_diameter_m": 50.0, "b": 1.80},
        {"min_diameter_m": 50.0, "max_diameter_m": 250.0, "b": 2.00},
    ],
    "highland_scientific": [
        {"min_diameter_m": 2.5, "max_diameter_m": 50.0, "b": 2.60},
        {"min_diameter_m": 50.0, "max_diameter_m": 250.0, "b": 2.10},
    ],
    "fresh_crater_scientific": [
        {"min_diameter_m": 2.5, "max_diameter_m": 50.0, "b": 3.50},
        {"min_diameter_m": 50.0, "max_diameter_m": 250.0, "b": 3.00},
    ],
    "custom_scientific": [
        {"min_diameter_m": 2.5, "max_diameter_m": 50.0, "b": 2.60},
        {"min_diameter_m": 50.0, "max_diameter_m": 250.0, "b": 2.10},
    ],
}

HEIGHTMAP_SIZE_CHOICES = [
    "1009",
    "2017",
    "4033",
]

ROCK_FIELDS = [
    ("Limits", [
        ("max_rocks", "Max rocks", "int"),
        ("min_rock_diameter", "Min rock diameter, m", "float"),
        ("max_rock_diameter_cap", "Max rock diameter cap, m", "float"),
        ("power_law_exponent", "Power-law exponent", "float"),
        ("min_source_crater_diameter_meters", "Min source crater diameter, m", "float"),
        ("max_source_crater_degrade", "Max source crater degrade", "float"),
    ]),
    ("Background", [
        ("background_density_per_m2", "Background density / m²", "float"),
        ("background_fraction_cap", "Background fraction cap", "float"),
        ("background_clump_fraction", "Background clump fraction", "float"),
        ("background_cluster_sigma_m", "Background cluster sigma, m", "float"),
    ]),

    ("Bart–Melosh scaling", [
        ("bm_max_diameter_a", "Max diameter coefficient A", "float"),
        ("bm_max_diameter_b", "Max diameter exponent B", "float"),
        ("bm_median_diameter_a", "Median diameter coefficient A", "float"),
        ("bm_median_diameter_b", "Median diameter exponent B", "float"),
        ("bm_max_distance_a", "Max distance coefficient A", "float"),
        ("bm_max_distance_b", "Max distance exponent B", "float"),
        ("bm_median_distance_a", "Median distance coefficient A", "float"),
        ("bm_median_distance_b", "Median distance exponent B", "float"),
        ("boulder_distance_scale", "Boulder distance scale", "float"),
    ]),

    ("Crater abundance", [
        ("boulder_diameter_scale", "Boulder diameter scale", "float"),
        ("crater_boulder_density_scale", "Crater boulder density scale", "float"),
        ("crater_count_exponent", "Crater count exponent", "float"),
        ("freshness_gamma", "Freshness gamma", "float"),
        ("freshness_floor", "Freshness floor", "float"),
        ("max_rocks_per_crater", "Max rocks per crater", "int"),
    ]),
    ("Zone fractions", [
        ("interior_fraction", "Interior fraction", "float"),
        ("rim_fraction", "Rim fraction", "float"),
        ("proximal_fraction", "Proximal fraction", "float"),
        ("distal_fraction", "Distal fraction", "float"),
    ]),
    ("Zone bounds", [
        ("interior_r_min", "Interior r min", "float"),
        ("interior_r_max", "Interior r max", "float"),
        ("rim_r_min", "Rim r min", "float"),
        ("rim_r_max", "Rim r max", "float"),
        ("proximal_r_min", "Proximal r min", "float"),
        ("proximal_r_max", "Proximal r max", "float"),
        ("distal_r_min", "Distal r min", "float"),
        ("distal_r_max", "Distal r max", "float"),
    ]),
    ("Distance and size", [
        ("interior_distance_power", "Interior distance power", "float"),
        ("rim_distance_power", "Rim distance power", "float"),
        ("proximal_distance_power", "Proximal distance power", "float"),
        ("distal_distance_power", "Distal distance power", "float"),
        ("distance_size_decay", "Distance size decay", "float"),
        ("interior_size_multiplier", "Interior size multiplier", "float"),
        ("rim_size_multiplier", "Rim size multiplier", "float"),
        ("proximal_size_multiplier", "Proximal size multiplier", "float"),
        ("distal_size_multiplier", "Distal size multiplier", "float"),
    ]),
    ("Clumping", [
        ("crater_clump_fraction", "Crater clump fraction", "float"),
        ("mean_cluster_size", "Mean cluster size", "float"),
        ("cluster_sigma_m", "Cluster sigma, m", "float"),
        ("cluster_zone_bias", "Cluster zone bias", "choice_cluster_bias"),
    ]),
    ("Random big-rock clumps", [
        ("enable_random_big_rock_clumps", "Enable random big-rock clumps", "bool"),
        ("random_big_rock_clump_count", "Random big-rock clump count", "int"),
        ("random_big_rock_mean_rocks_per_clump", "Random big-rock mean rocks/clump", "float"),
        ("random_big_rock_clump_sigma_m", "Random big-rock clump sigma, m", "float"),
        ("random_big_rock_min_diameter_meters", "Random big-rock min diameter, m", "float"),
        ("random_big_rock_max_diameter_meters", "Random big-rock max diameter, m", "float"),
        ("random_big_rock_exclude_crater_min_diameter_meters", "Exclude crater min diameter, m", "float"),
        ("random_big_rock_exclude_crater_radius_multiplier", "Exclude crater radius multiplier", "float"),
    ]),
    ("Unreal placement", [
        ("max_random_tilt_degrees", "Max random tilt, degrees", "float"),
        ("min_burial_fraction", "Min burial fraction (0.2 = 20%)", "float"),
        ("max_burial_fraction", "Max burial fraction (0.6 = 60%)", "float"),
    ]),
]

FIELD_KINDS = {key: kind for _, fields in ROCK_FIELDS for key, _, kind in fields}

def slugify(value):
    text = str(value).strip().lower()
    text = re.sub(r"[^a-z0-9]+", "_", text)
    text = text.strip("_")
    return text or "terrain"

def rock(
    max_rocks, min_rock_diameter, max_rock_diameter_cap, power_law_exponent,
    min_source_crater_diameter_meters, max_source_crater_degrade,
    background_density_per_m2, background_fraction_cap, background_clump_fraction, background_cluster_sigma_m,
    boulder_diameter_scale, crater_boulder_density_scale, crater_count_exponent, freshness_gamma, freshness_floor, max_rocks_per_crater,
    interior_fraction, rim_fraction, proximal_fraction, distal_fraction,
    interior_r_min, interior_r_max, rim_r_min, rim_r_max, proximal_r_min, proximal_r_max, distal_r_min, distal_r_max,
    interior_distance_power, rim_distance_power, proximal_distance_power, distal_distance_power,
    distance_size_decay, interior_size_multiplier, rim_size_multiplier, proximal_size_multiplier, distal_size_multiplier,
    crater_clump_fraction, mean_cluster_size, cluster_sigma_m, cluster_zone_bias,
    enable_random_big_rock_clumps, random_big_rock_clump_count, random_big_rock_mean_rocks_per_clump, random_big_rock_clump_sigma_m,
    random_big_rock_min_diameter_meters, random_big_rock_max_diameter_meters,
    random_big_rock_exclude_crater_min_diameter_meters, random_big_rock_exclude_crater_radius_multiplier,
    background_material, floor_material, old_ejecta_material, fresh_ejecta_material,

    # Shared scientific Bart–Melosh values
    bm_max_diameter_a=0.40,
    bm_max_diameter_b=0.65,
    bm_median_diameter_a=0.078,
    bm_median_diameter_b=0.62,
    bm_max_distance_a=0.024,
    bm_max_distance_b=0.66,
    bm_median_distance_a=0.0023,
    bm_median_distance_b=0.86,
    boulder_distance_scale=1000.0,
):
    return {
        "max_rocks": max_rocks,
        "min_rock_diameter": min_rock_diameter,
        "max_rock_diameter_cap": max_rock_diameter_cap,
        "power_law_exponent": power_law_exponent,
        "min_source_crater_diameter_meters": min_source_crater_diameter_meters,
        "max_source_crater_degrade": max_source_crater_degrade,
        "background_density_per_m2": background_density_per_m2,
        "background_fraction_cap": background_fraction_cap,
        "background_clump_fraction": background_clump_fraction,
        "background_cluster_sigma_m": background_cluster_sigma_m,
        "background_cap_mode": "maxrocks",
        "bm_max_diameter_a": bm_max_diameter_a,
        "bm_max_diameter_b": bm_max_diameter_b,
        "bm_median_diameter_a": bm_median_diameter_a,
        "bm_median_diameter_b": bm_median_diameter_b,
        "bm_max_distance_a": bm_max_distance_a,
        "bm_max_distance_b": bm_max_distance_b,
        "bm_median_distance_a": bm_median_distance_a,
        "bm_median_distance_b": bm_median_distance_b,
        "boulder_distance_scale": boulder_distance_scale,

        "boulder_diameter_scale": boulder_diameter_scale,
        "crater_boulder_density_scale": crater_boulder_density_scale,
        "crater_count_exponent": crater_count_exponent,
        "freshness_gamma": freshness_gamma,
        "freshness_floor": freshness_floor,
        "max_rocks_per_crater": max_rocks_per_crater,
        "interior_fraction": interior_fraction,
        "rim_fraction": rim_fraction,
        "proximal_fraction": proximal_fraction,
        "distal_fraction": distal_fraction,
        "interior_r_min": interior_r_min,
        "interior_r_max": interior_r_max,
        "rim_r_min": rim_r_min,
        "rim_r_max": rim_r_max,
        "proximal_r_min": proximal_r_min,
        "proximal_r_max": proximal_r_max,
        "distal_r_min": distal_r_min,
        "distal_r_max": distal_r_max,
        "interior_distance_power": interior_distance_power,
        "rim_distance_power": rim_distance_power,
        "proximal_distance_power": proximal_distance_power,
        "distal_distance_power": distal_distance_power,
        "distance_size_decay": distance_size_decay,
        "interior_size_multiplier": interior_size_multiplier,
        "rim_size_multiplier": rim_size_multiplier,
        "proximal_size_multiplier": proximal_size_multiplier,
        "distal_size_multiplier": distal_size_multiplier,
        "crater_clump_fraction": crater_clump_fraction,
        "mean_cluster_size": mean_cluster_size,
        "cluster_sigma_m": cluster_sigma_m,
        "cluster_zone_bias": cluster_zone_bias,
        "enable_random_big_rock_clumps": enable_random_big_rock_clumps,
        "random_big_rock_clump_count": random_big_rock_clump_count,
        "random_big_rock_mean_rocks_per_clump": random_big_rock_mean_rocks_per_clump,
        "random_big_rock_clump_sigma_m": random_big_rock_clump_sigma_m,
        "random_big_rock_min_diameter_meters": random_big_rock_min_diameter_meters,
        "random_big_rock_max_diameter_meters": random_big_rock_max_diameter_meters,
        "random_big_rock_exclude_crater_min_diameter_meters": random_big_rock_exclude_crater_min_diameter_meters,
        "random_big_rock_exclude_crater_radius_multiplier": random_big_rock_exclude_crater_radius_multiplier,
        "max_random_tilt_degrees": 12.0,
        "min_burial_fraction": 0.2,
        "max_burial_fraction": 0.6,
        "background_material": background_material,
        "floor_material": floor_material,
        "old_ejecta_material": old_ejecta_material,
        "fresh_ejecta_material": fresh_ejecta_material,
    }

PRESET_DEFAULTS = {
    "Mare": {
        "heightmap_preset": "mare_scientific",
        "rock_profile": "mare",
        "seed": 25654,
        "heightmap_size": 1009,
        "map_size_m": 500,
        "height_range_m": 80,
        "range_mode": "fixed",
        "crater_k_small": 0.015,
        "crater_k_large": 0.020,
        "rock": rock(
            50000, 0.26, 3.5, 4.7, 3.0, 1.0,
            0.0045, 0.60, 0.02, 4.0,
            0.25, 0.35, 2.25, 1.0, 0.01, 300,
            0.03, 0.45, 0.44, 0.08,
            0.30, 0.80, 0.85, 1.15, 1.15, 2.25, 2.25, 4.00,
            1.3, 1.9, 2.2, 3.0,
            1.7, 0.25, 1.10, 0.75, 0.25,
            0.40, 5.0, 2.0, "rim_proximal",
            False, 15, 10.0, 6.0, 1.5, 8.0, 60.0, 1.0,
            "MareBasalt", "RegolithCovered", "MixedBreccia", "FreshEjecta"
        ),
    },
    "Apollo 17": {
        "heightmap_preset": "apollo17_scientific",
        "rock_profile": "apollo_17",
        "seed": 12345,
        "heightmap_size": 1009,
        "map_size_m": 500,
        "height_range_m": 90,
        "range_mode": "fixed",
        "crater_k_small": 0.006,
        "crater_k_large": 0.020,
        "rock": rock(
            60000, 0.26, 8.6, 6.8, 3.0, 1.0,
            0.012, 0.45, 0.08, 3.0,
            0.33, 0.040, 2.30, 2.4, 0.05, 400,
            0.08, 0.55, 0.32, 0.05,
            0.25, 0.85, 0.85, 1.20, 1.20, 2.00, 2.00, 3.00,
            1.2, 1.5, 2.1, 3.2,
            1.8, 0.35, 1.25, 0.75, 0.20,
            0.70, 8.0, 1.8, "rim_proximal",
            False, 16, 10.0, 6.0, 2.0, 12.0, 60.0, 1.0,
            "MixedBreccia", "RegolithCovered", "MixedBreccia", "FreshEjecta"
        ),
    },
    "Polar Highlands": {
        "heightmap_preset": "highland_scientific",
        "rock_profile": "polar_highlands",
        "seed": 13542,
        "heightmap_size": 1009,
        "map_size_m": 500,
        "height_range_m": 100,
        "range_mode": "fixed",
        "crater_k_small": 0.030,
        "crater_k_large": 0.060,
        "rock": rock(
            70000, 0.26, 20.0, 3.8, 6.0, 1.0,
            0.010, 0.50, 0.06, 3.5,
            0.50, 0.060, 1.35, 1.2, 0.005, 3000,
            0.12, 0.38, 0.38, 0.12,
            0.20, 0.90, 0.85, 1.20, 1.20, 3.00, 3.00, 6.00,
            1.1, 1.6, 2.0, 2.5,
            1.4, 0.45, 1.25, 0.90, 0.35,
            0.65, 8.0, 5, "rim_proximal",
            False, 24, 12.0, 8.0, 2.5, 22.0, 60.0, 1.0,
            "HighlandAnorthosite", "RegolithCovered", "MixedBreccia", "FreshEjecta"
        ),
    },
    "New Fresh Zone": {
        "heightmap_preset": "fresh_crater_scientific",
        "rock_profile": "new_fresh_zone",
        "seed": 12345,
        "heightmap_size": 1009,
        "map_size_m": 500,
        "height_range_m": 120,
        "range_mode": "fixed",
        "crater_k_small": 0.080,
        "crater_k_large": 0.015,
        "rock": rock(
            100000, 0.26, 14.2, 5.3, 3.0, 0.45,
            0.0070, 0.25, 0.15, 2.0,
            0.55, 0.0100, 1.35, 1.5, 0.05, 5200,
            0.04, 0.38, 0.45, 0.13,
            0.20, 0.85, 0.85, 1.20, 1.20, 3.00, 3.00, 8.00,
            1.2, 1.4, 1.8, 2.4,
            1.4, 0.30, 1.35, 0.95, 0.35,
            0.75, 10.0, 2.5, "all_ejecta",
            False, 30, 14.0, 8.0, 2.5, 22.0, 60.0, 1.0,
            "FreshEjecta", "FreshEjecta", "MixedBreccia", "FreshEjecta"
        ),
    },
    "Custom": {
        # Independent highland-like heightmap preset with deliberately higher
        # crater abundance and numerous additional big-rock clumps. K values
        # remain editable in the Basic generation settings.
        "heightmap_preset": "custom_scientific",
        "rock_profile": "custom",
        "seed": 24680,
        "heightmap_size": 1009,
        "map_size_m": 500,
        "height_range_m": 110,
        "range_mode": "fixed",
        "crater_k_small": 0.060,
        "crater_k_large": 0.100,
        "rock": rock(
            100000, 0.26, 20.0, 3.8, 6.0, 1.0,
            0.012, 0.55, 0.08, 3.5,
            0.55, 0.080, 1.35, 1.2, 0.005, 3500,
            0.12, 0.38, 0.38, 0.12,
            0.20, 0.90, 0.85, 1.20, 1.20, 3.00, 3.00, 6.00,
            1.1, 1.6, 2.0, 2.5,
            1.4, 0.45, 1.25, 0.90, 0.35,
            0.70, 10.0, 5.0, "rim_proximal",
            True, 50, 18.0, 10.0, 3.0, 30.0, 60.0, 1.0,
            "HighlandAnorthosite", "RegolithCovered", "MixedBreccia", "FreshEjecta"
        ),
    },
}

preset_states = copy.deepcopy(PRESET_DEFAULTS)
active_preset = "Mare"

root = tk.Tk()
root.title("MoonSim Asset Generator")
root.geometry("1100x880")
root.minsize(920, 700)

# Minimal visual refresh: neutral surfaces, restrained accent colour, and a
# faded Unreal screenshot in the header. Replace moonsim_header.png with any
# other PNG to change the image without touching this script.
BG = "#f4f5f7"
SURFACE = "#ffffff"
TEXT = "#20242b"
MUTED = "#69707d"
BORDER = "#d7dbe2"
ACCENT = "#3568d4"
ACCENT_ACTIVE = "#2855b5"
BUTTON_BG = "#e9ecf1"
BUTTON_ACTIVE = "#dde2e9"
HEADER_IMAGE = PROJECT_DIR / "moonsim_header.png"

root.configure(background=BG)

style = ttk.Style(root)
try:
    style.theme_use("clam")
except tk.TclError:
    pass

def preferred_font(*names):
    available = set(tkfont.families(root))
    for name in names:
        if name in available:
            return name
    return tkfont.nametofont("TkDefaultFont").actual("family")

UI_FONT = preferred_font("Inter", "Segoe UI", "SF Pro Text", "Noto Sans", "DejaVu Sans")
MONO_FONT = preferred_font("JetBrains Mono", "Cascadia Mono", "Menlo", "DejaVu Sans Mono")

style.configure(".", font=(UI_FONT, 10), foreground=TEXT)
style.configure("TFrame", background=BG)
style.configure("TLabel", background=BG, foreground=TEXT)
style.configure(
    "Section.TLabelframe",
    background=BG,
    bordercolor=BORDER,
    lightcolor=BORDER,
    darkcolor=BORDER,
    relief="solid",
    borderwidth=1,
)
style.configure(
    "Section.TLabelframe.Label",
    background=BG,
    foreground=TEXT,
    font=(UI_FONT, 10, "bold"),
)
style.configure(
    "TEntry",
    fieldbackground=SURFACE,
    foreground=TEXT,
    bordercolor=BORDER,
    lightcolor=BORDER,
    darkcolor=BORDER,
    insertcolor=TEXT,
    padding=(8, 6),
)
style.map("TEntry", bordercolor=[("focus", ACCENT)])
style.configure(
    "TCombobox",
    fieldbackground=SURFACE,
    background=SURFACE,
    foreground=TEXT,
    bordercolor=BORDER,
    lightcolor=BORDER,
    darkcolor=BORDER,
    arrowcolor=MUTED,
    padding=(7, 5),
)
style.map(
    "TCombobox",
    fieldbackground=[("readonly", SURFACE)],
    foreground=[("readonly", TEXT)],
    bordercolor=[("focus", ACCENT)],
)
style.configure("TNotebook", background=BG, borderwidth=0)
style.configure(
    "TNotebook.Tab",
    background="#e8ebf0",
    foreground=MUTED,
    borderwidth=0,
    padding=(12, 7),
)
style.map(
    "TNotebook.Tab",
    background=[("selected", SURFACE), ("active", "#eef0f4")],
    foreground=[("selected", TEXT), ("active", TEXT)],
)
style.configure(
    "Primary.TButton",
    background=ACCENT,
    foreground="#ffffff",
    borderwidth=0,
    focusthickness=0,
    padding=(14, 8),
)
style.map(
    "Primary.TButton",
    background=[("active", ACCENT_ACTIVE), ("pressed", ACCENT_ACTIVE), ("disabled", "#aeb9ce")],
    foreground=[("disabled", "#f3f5f8")],
)
style.configure(
    "Secondary.TButton",
    background=BUTTON_BG,
    foreground=TEXT,
    borderwidth=0,
    focusthickness=0,
    padding=(12, 8),
)
style.map(
    "Secondary.TButton",
    background=[("active", BUTTON_ACTIVE), ("pressed", BUTTON_ACTIVE), ("disabled", "#eceef2")],
    foreground=[("disabled", "#9ca3ad")],
)
style.configure("Preset.TRadiobutton", background=BG, foreground=TEXT, padding=(8, 6))
style.map("Preset.TRadiobutton", background=[("active", BG)], foreground=[("active", TEXT)])
style.configure("TCheckbutton", background=BG, foreground=TEXT)
style.map("TCheckbutton", background=[("active", BG)])
style.configure("Status.TLabel", background=BG, foreground=MUTED, font=(UI_FONT, 9))

root.option_add("*TCombobox*Listbox.background", SURFACE)
root.option_add("*TCombobox*Listbox.foreground", TEXT)
root.option_add("*TCombobox*Listbox.selectBackground", ACCENT)
root.option_add("*TCombobox*Listbox.selectForeground", "#ffffff")

root.columnconfigure(0, weight=1)
root.rowconfigure(0, weight=1)

outer = ttk.Frame(root, padding=(18, 14, 18, 16))
outer.grid(row=0, column=0, sticky="nsew")
outer.columnconfigure(0, weight=1)
outer.rowconfigure(3, weight=1)

header = tk.Canvas(
    outer,
    height=138,
    background="#e6e8ec",
    highlightthickness=1,
    highlightbackground=BORDER,
    relief="flat",
)
header.grid(row=0, column=0, rowspan=2, sticky="ew", pady=(0, 14))

header_photo = None
if HEADER_IMAGE.exists():
    try:
        header_photo = tk.PhotoImage(file=str(HEADER_IMAGE))
        header.create_image(0, 0, image=header_photo, anchor="nw")
        header.image = header_photo
    except tk.TclError:
        header_photo = None

header_title = header.create_text(
    24,
    28,
    text="MoonSim Heightmap + Rockfield Generator",
    anchor="nw",
    fill=TEXT,
    font=(UI_FONT, 18, "bold"),
)
header_subtitle = header.create_text(
    24,
    68,
    text="Generate complete heightmap and rockfield run packages, export only the assets Unreal needs, and run either analysis from the same GUI.",
    anchor="nw",
    fill="#4f5662",
    font=(UI_FONT, 10),
    width=930,
)
header_rule = header.create_line(0, 137, 1100, 137, fill=BORDER)

def resize_header(event):
    header.itemconfigure(header_subtitle, width=max(420, event.width - 48))
    header.coords(header_rule, 0, 137, event.width, 137)

header.bind("<Configure>", resize_header)

top = ttk.Frame(outer)
top.grid(row=2, column=0, sticky="ew")
top.columnconfigure(0, weight=0)
top.columnconfigure(1, weight=1)

terrain_var = tk.StringVar(value=active_preset)
basic_vars = {
    "heightmap_preset": tk.StringVar(),
    "rock_profile": tk.StringVar(),
    "seed": tk.StringVar(),
    "heightmap_size": tk.StringVar(),
    "map_size_m": tk.StringVar(),
    "height_range_m": tk.StringVar(),
    "range_mode": tk.StringVar(),
    "crater_k_small": tk.StringVar(),
    "crater_k_large": tk.StringVar(),
}
rock_vars = {}
existing_heightmap_run_var = tk.StringVar()
# Internal resolved paths. They are kept for compatibility with the generation
# functions, but the user chooses one complete heightmap run folder.
existing_crater_json_var = tk.StringVar()
existing_metadata_json_var = tk.StringVar()

preset_box = ttk.LabelFrame(top, text="Terrain preset", padding=12, style="Section.TLabelframe")
preset_box.grid(row=0, column=0, sticky="nsew", padx=(0, 10))
preset_box.columnconfigure(0, weight=1)

basic_box = ttk.LabelFrame(top, text="Basic generation settings", padding=12, style="Section.TLabelframe")
basic_box.grid(row=0, column=1, sticky="nsew", padx=(10, 0))
basic_box.columnconfigure(1, weight=1)
basic_box.columnconfigure(3, weight=1)

def make_label(parent, text, row, col):
    ttk.Label(parent, text=text).grid(row=row, column=col, sticky="w", padx=(0, 8), pady=6)

def make_entry(parent, var, row, col):
    e = ttk.Entry(parent, textvariable=var)
    e.grid(row=row, column=col, sticky="ew", pady=6)
    return e

def make_combo(parent, var, values, row, col):
    c = ttk.Combobox(parent, textvariable=var, values=values, state="readonly")
    c.grid(row=row, column=col, sticky="ew", pady=6)
    return c


def set_existing_heightmap_run(path):
    info = resolve_heightmap_run_folder(Path(path))
    existing_heightmap_run_var.set(str(info["run_dir"]))
    existing_crater_json_var.set(str(info["crater_json"]))
    existing_metadata_json_var.set(str(info["metadata"]))
    status_var.set(
        "Heightmap run selected. Its metadata and crater catalog will be used for rock generation."
    )


def browse_existing_heightmap_run():
    path = filedialog.askdirectory(
        title="Choose a generated heightmap run folder",
        initialdir=str(
            HEIGHTMAP_GENERATIONS_DIR
            if HEIGHTMAP_GENERATIONS_DIR.exists()
            else PROJECT_DIR
        ),
    )
    if not path:
        return
    try:
        set_existing_heightmap_run(path)
    except Exception as exc:
        messagebox.showerror(
            "Invalid heightmap run",
            str(exc),
            parent=root,
        )


def clear_existing_heightmap_inputs():
    existing_heightmap_run_var.set("")
    existing_crater_json_var.set("")
    existing_metadata_json_var.set("")
    status_var.set("Heightmap source cleared.")


make_label(basic_box, "Seed", 0, 0)
make_entry(basic_box, basic_vars["seed"], 0, 1)
make_label(basic_box, "Heightmap size", 0, 2)
heightmap_size_combo = ttk.Combobox(
    basic_box,
    textvariable=basic_vars["heightmap_size"],
    values=HEIGHTMAP_SIZE_CHOICES,
    state="readonly",
)
heightmap_size_combo.grid(row=0, column=3, sticky="ew", pady=4)

make_label(basic_box, "Map size, m", 1, 0)
make_entry(basic_box, basic_vars["map_size_m"], 1, 1)
make_label(basic_box, "Height range, m", 1, 2)
make_entry(basic_box, basic_vars["height_range_m"], 1, 3)

make_label(basic_box, "Heightmap preset", 2, 0)
make_combo(basic_box, basic_vars["heightmap_preset"], HEIGHTMAP_PRESET_CHOICES, 2, 1)
make_label(basic_box, "Rock profile", 2, 2)
make_combo(basic_box, basic_vars["rock_profile"], ROCK_PROFILE_CHOICES, 2, 3)

make_label(basic_box, "Crater K, 2.5–50 m", 3, 0)
make_entry(basic_box, basic_vars["crater_k_small"], 3, 1)
make_label(basic_box, "Crater K, 50–250 m", 3, 2)
make_entry(basic_box, basic_vars["crater_k_large"], 3, 3)

make_label(basic_box, "Heightmap run for rock generation", 4, 0)
ttk.Entry(
    basic_box,
    textvariable=existing_heightmap_run_var,
).grid(row=4, column=1, columnspan=2, sticky="ew", pady=6)

heightmap_run_actions = ttk.Frame(basic_box)
heightmap_run_actions.grid(row=4, column=3, sticky="ew", pady=6)
heightmap_run_actions.columnconfigure(0, weight=1)
heightmap_run_actions.columnconfigure(1, weight=1)

ttk.Button(
    heightmap_run_actions,
    text="Browse",
    command=browse_existing_heightmap_run,
).grid(row=0, column=0, sticky="ew")

ttk.Button(
    heightmap_run_actions,
    text="Clear",
    command=clear_existing_heightmap_inputs,
).grid(row=0, column=1, sticky="ew", padx=(5, 0))

ttk.Label(
    basic_box,
    text=(
        "Generate a heightmap first, or select a folder under generated/heightmaps. "
        "The GUI resolves the timestamped heightmap, metadata, and crater files automatically."
    ),
    foreground=MUTED,
    wraplength=760,
).grid(row=5, column=0, columnspan=4, sticky="w", pady=(0, 4))

advanced_container = ttk.LabelFrame(outer, text="Advanced rock parameters", padding=8, style="Section.TLabelframe")
advanced_container.grid(row=3, column=0, sticky="nsew", pady=(14, 10))
advanced_container.columnconfigure(0, weight=1)
advanced_container.rowconfigure(0, weight=1)

# Keep the original window size. Only the contents of each advanced tab scroll
# when the available height is too small to display every parameter.
notebook = ttk.Notebook(advanced_container, height=250)
notebook.grid(row=0, column=0, sticky="nsew")


def bind_advanced_mousewheel(widget, canvas):
    """Scroll the active advanced tab while the pointer is over its controls."""
    def on_mousewheel(event):
        if getattr(event, "num", None) == 4:
            canvas.yview_scroll(-3, "units")
        elif getattr(event, "num", None) == 5:
            canvas.yview_scroll(3, "units")
        else:
            delta = getattr(event, "delta", 0)
            if delta:
                canvas.yview_scroll(-1 if delta > 0 else 1, "units")
        return "break"

    widget.bind("<MouseWheel>", on_mousewheel, add="+")
    widget.bind("<Button-4>", on_mousewheel, add="+")
    widget.bind("<Button-5>", on_mousewheel, add="+")


def create_tab(name, fields):
    tab = ttk.Frame(notebook)
    notebook.add(tab, text=name)
    tab.columnconfigure(0, weight=1)
    tab.rowconfigure(0, weight=1)

    canvas = tk.Canvas(
        tab,
        background=BG,
        highlightthickness=0,
        borderwidth=0,
        relief="flat",
        yscrollincrement=24,
    )
    tab_scroll = ttk.Scrollbar(tab, orient="vertical", command=canvas.yview)
    canvas.configure(yscrollcommand=tab_scroll.set)
    canvas.grid(row=0, column=0, sticky="nsew")
    tab_scroll.grid(row=0, column=1, sticky="ns")

    content = ttk.Frame(canvas, padding=(14, 12, 14, 12))
    content.columnconfigure(1, weight=1)
    content.columnconfigure(3, weight=1)
    content_window = canvas.create_window((0, 0), window=content, anchor="nw")

    def update_scroll_region(_event=None):
        canvas.configure(scrollregion=canvas.bbox("all"))

    def fit_content_width(event):
        # Match the form width to the viewport while leaving room for the
        # vertical scrollbar. Scrolling therefore remains vertical only.
        canvas.itemconfigure(content_window, width=event.width)

    content.bind("<Configure>", update_scroll_region)
    canvas.bind("<Configure>", fit_content_width)
    bind_advanced_mousewheel(canvas, canvas)
    bind_advanced_mousewheel(content, canvas)

    for idx, (key, label, kind) in enumerate(fields):
        row = idx // 2
        side = (idx % 2) * 2
        label_widget = ttk.Label(content, text=label)
        label_widget.grid(row=row, column=side, sticky="w", padx=(0, 8), pady=6)
        bind_advanced_mousewheel(label_widget, canvas)

        if kind == "bool":
            var = tk.BooleanVar()
            rock_vars[key] = var
            control = ttk.Checkbutton(content, variable=var)
            control.grid(row=row, column=side + 1, sticky="w", pady=6)
        elif kind == "choice_cluster_bias":
            var = tk.StringVar()
            rock_vars[key] = var
            control = ttk.Combobox(content, textvariable=var, values=CLUSTER_BIASES, state="readonly")
            control.grid(row=row, column=side + 1, sticky="ew", pady=6)
        elif kind == "choice_background_cap":
            var = tk.StringVar()
            rock_vars[key] = var
            control = ttk.Combobox(content, textvariable=var, values=BACKGROUND_CAP_MODES, state="readonly")
            control.grid(row=row, column=side + 1, sticky="ew", pady=6)
        else:
            var = tk.StringVar()
            rock_vars[key] = var
            control = ttk.Entry(content, textvariable=var)
            control.grid(row=row, column=side + 1, sticky="ew", pady=6)

        bind_advanced_mousewheel(control, canvas)

for section_name, fields in ROCK_FIELDS:
    create_tab(section_name, fields)

log_box = ttk.LabelFrame(outer, text="Activity log", padding=8, style="Section.TLabelframe")
log_box.grid(row=5, column=0, sticky="nsew", pady=(4, 10))
log_box.columnconfigure(0, weight=1)
log_box.rowconfigure(0, weight=1)
outer.rowconfigure(5, weight=1)

log_text = tk.Text(
    log_box,
    height=9,
    wrap="word",
    background=SURFACE,
    foreground="#343a44",
    insertbackground=TEXT,
    selectbackground="#cdd9f2",
    selectforeground=TEXT,
    relief="flat",
    borderwidth=0,
    padx=10,
    pady=8,
    font=(MONO_FONT, 9),
)
log_scroll = ttk.Scrollbar(log_box, orient="vertical", command=log_text.yview)
log_text.configure(yscrollcommand=log_scroll.set)
log_text.grid(row=0, column=0, sticky="nsew")
log_scroll.grid(row=0, column=1, sticky="ns")

status_var = tk.StringVar(value="Ready.")
ttk.Label(outer, textvariable=status_var, anchor="w", style="Status.TLabel").grid(row=6, column=0, sticky="ew", pady=(0, 10))

buttons = ttk.Frame(outer)
buttons.grid(row=7, column=0, sticky="ew")
buttons.columnconfigure(0, weight=1)

def append_log(text):
    log_text.insert("end", text)
    log_text.see("end")
    root.update_idletasks()

def clear_log():
    log_text.delete("1.0", "end")

def load_state_to_widgets(name):
    state = preset_states[name]
    for k, var in basic_vars.items():
        var.set(str(state[k]))
    for k, value in state["rock"].items():
        var = rock_vars.get(k)
        if var is None:
            continue
        if isinstance(var, tk.BooleanVar):
            var.set(bool(value))
        else:
            var.set(str(value))
    status_var.set(f"Loaded {name} preset values.")

def parse_value(key, raw):
    kind = FIELD_KINDS.get(key, "string")
    if kind == "bool":
        return bool(raw)
    if kind.startswith("choice_"):
        return str(raw)
    text = str(raw).strip()
    if text == "":
        raise ValueError(f"{key} cannot be empty.")
    if kind == "int":
        return int(text)
    if kind == "float":
        return float(text)
    return text

def current_widgets_to_state():
    state = {}
    for k, var in basic_vars.items():
        value = var.get().strip()
        if k in {"seed", "heightmap_size"}:
            value = int(value)
        elif k in {"map_size_m", "height_range_m", "crater_k_small", "crater_k_large"}:
            value = float(value)
        state[k] = value

    state["range_mode"] = "fixed"
    state["terrain_label"] = terrain_var.get()

    rock_settings = {}
    for k, var in rock_vars.items():
        if isinstance(var, tk.BooleanVar):
            rock_settings[k] = bool(var.get())
        else:
            rock_settings[k] = parse_value(k, var.get())
    state["rock"] = rock_settings
    return state

def save_active_state():
    global active_preset
    preset_states[active_preset] = current_widgets_to_state()

def clear_current_run():
    current_run.update({
        "run_dir": None,
        "heightmap_out_dir": None,
        "crater_json": None,
        "metadata": None,
        "base_name": None,
        "run_name": None,
        "terrain_label": None,
    })


def on_preset_change():
    global active_preset
    try:
        save_active_state()
    except Exception:
        # Do not block switching because a half-typed number is temporarily invalid.
        pass
    active_preset = terrain_var.get()
    clear_current_run()
    clear_existing_heightmap_inputs()
    load_state_to_widgets(active_preset)
    status_var.set(f"Loaded {active_preset} preset values. Generate a heightmap or select both required JSON files before generating rocks.")

def reset_current_defaults():
    global active_preset
    if messagebox.askyesno("Reset defaults", f"Reset {active_preset} to its default values?"):
        preset_states[active_preset] = copy.deepcopy(PRESET_DEFAULTS[active_preset])
        load_state_to_widgets(active_preset)

for name in PRESET_DEFAULTS:
    ttk.Radiobutton(
        preset_box,
        text=name,
        variable=terrain_var,
        value=name,
        style="Preset.TRadiobutton",
        command=on_preset_change,
    ).grid(sticky="ew", padx=4, pady=3)

def newest_path(paths):
    existing = []
    for path in paths:
        try:
            path = Path(path)
            if path.exists() and path.is_file():
                existing.append(path)
        except OSError:
            continue
    if not existing:
        return None
    return max(existing, key=lambda p: p.stat().st_mtime)


def analysis_heightmap_candidates():
    candidates = []

    if HEIGHTMAP_GENERATIONS_DIR.exists():
        for run_dir in HEIGHTMAP_GENERATIONS_DIR.iterdir():
            if not run_dir.is_dir():
                continue
            try:
                candidates.append(resolve_heightmap_run_folder(run_dir)["heightmap"])
            except Exception:
                continue

    if HEIGHTMAP_PNG_DIR.exists():
        candidates.extend(HEIGHTMAP_PNG_DIR.glob("*.png"))

    # Legacy exports are read-only compatibility.
    legacy_heightmaps = PROJECT_DIR / "heightmap_pngs"
    if legacy_heightmaps.exists():
        candidates.extend(legacy_heightmaps.glob("*.png"))

    unique = []
    seen = set()
    for candidate in candidates:
        try:
            if not candidate.is_file():
                continue
            key = str(candidate.resolve())
        except OSError:
            continue
        if key not in seen:
            seen.add(key)
            unique.append(candidate)
    return unique

def matching_crater_json_for_heightmap(heightmap_path):
    run_dir = heightmap_run_for_asset(heightmap_path)
    if run_dir is not None:
        try:
            return resolve_heightmap_run_folder(run_dir)["crater_json"]
        except Exception:
            pass

    try:
        info = resolve_heightmap_run_folder(Path(heightmap_path).parent)
        return info["crater_json"]
    except Exception:
        pass

    # Legacy matching.
    stem = Path(heightmap_path).stem
    legacy_craters = PROJECT_DIR / "crater_jsons"
    candidates = []
    if legacy_craters.exists():
        candidates.extend(legacy_craters.glob("*_rockfield_craters.json"))
    scored = []
    for candidate in candidates:
        base = base_name_from_crater_json(candidate)
        score = 0
        if stem.endswith(f"_{base}") or stem == base:
            score += 1000 + len(base)
        elif base in stem:
            score += 500 + len(base)
        if score:
            scored.append((score, candidate.stat().st_mtime, candidate))
    if not scored:
        return None
    scored.sort(key=lambda item: (item[0], item[1]), reverse=True)
    return scored[0][2]

def matching_metadata_for_heightmap(heightmap_path, crater_json=None):
    run_dir = heightmap_run_for_asset(heightmap_path)
    if run_dir is not None:
        try:
            return resolve_heightmap_run_folder(run_dir)["metadata"]
        except Exception:
            pass

    try:
        info = resolve_heightmap_run_folder(Path(heightmap_path).parent)
        return info["metadata"]
    except Exception:
        pass

    if crater_json is not None:
        base_name = base_name_from_crater_json(crater_json)
        return find_metadata_for_crater_json(crater_json, base_name)
    return None

def matching_rock_settings_for_metadata(metadata_path, heightmap_path):
    if not FINAL_DIR.exists():
        return None

    metadata_path = Path(metadata_path) if metadata_path else None
    heightmap_path = Path(heightmap_path)
    metadata_stem = metadata_path.stem if metadata_path else ""
    heightmap_stem = heightmap_path.stem

    settings_files = list(FINAL_DIR.rglob("*_rock_settings.json"))
    scored = []

    for settings_path in settings_files:
        score = 0
        parent = settings_path.parent

        # Strongest match: the settings run contains a copied metadata JSON
        # with the same base name.
        if metadata_stem:
            direct_metadata = parent / f"{metadata_stem}.json"
            if direct_metadata.exists():
                score += 1200

            for candidate in parent.glob("*.json"):
                if candidate.stem == metadata_stem:
                    score += 1000
                    break

        # A run name may also be embedded in the exported heightmap filename.
        if heightmap_stem.startswith(parent.name + "_"):
            score += 800

        # Terrain labels in the run folder provide a weaker fallback match.
        parent_tokens = {
            token for token in re.split(r"[^a-z0-9]+", parent.name.lower())
            if len(token) >= 4 and not token.isdigit()
        }
        heightmap_tokens = {
            token for token in re.split(r"[^a-z0-9]+", heightmap_stem.lower())
            if len(token) >= 4 and not token.isdigit()
        }
        score += 20 * len(parent_tokens & heightmap_tokens)

        if score:
            try:
                scored.append((score, settings_path.stat().st_mtime, settings_path))
            except OSError:
                pass

    if not scored:
        return None

    scored.sort(key=lambda item: (item[0], item[1]), reverse=True)
    return scored[0][2]


def default_analysis_output_dir(heightmap_path):
    run_dir = heightmap_run_for_asset(heightmap_path)
    run_name = run_dir.name if run_dir is not None else Path(heightmap_path).stem
    return ANALYSIS_HEIGHTMAPS_DIR / run_name

def latest_heightmap_analysis_inputs():
    heightmap_path = newest_path(analysis_heightmap_candidates())
    if heightmap_path is None:
        return {
            "heightmap": None,
            "metadata": None,
            "crater_json": None,
            "out_dir": ANALYSIS_HEIGHTMAPS_DIR,
        }

    crater_json = matching_crater_json_for_heightmap(heightmap_path)
    metadata = matching_metadata_for_heightmap(
        heightmap_path,
        crater_json=crater_json,
    )
    return {
        "heightmap": heightmap_path,
        "metadata": metadata,
        "crater_json": crater_json,
        "out_dir": default_analysis_output_dir(heightmap_path),
    }

def open_folder(path):
    path = Path(path).expanduser()
    if not path.exists():
        messagebox.showerror("Folder not found", f"The folder does not exist yet:\\n\\n{path}")
        return

    try:
        if sys.platform.startswith("linux"):
            subprocess.Popen(
                ["xdg-open", str(path)],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
        elif sys.platform == "darwin":
            subprocess.Popen(["open", str(path)])
        elif os.name == "nt":
            os.startfile(str(path))
        else:
            raise RuntimeError("No supported folder opener was found.")
    except Exception as exc:
        messagebox.showerror("Could not open folder", str(exc))


def run_heightmap_analysis_thread(
    heightmap_path,
    metadata_path,
    crater_json_path,
    output_dir,
    dialog,
    run_button,
    open_button,
    dialog_status_var,
):
    try:
        ANALYSIS_HEIGHTMAPS_DIR.mkdir(parents=True, exist_ok=True)
        output_dir.mkdir(parents=True, exist_ok=True)

        cmd = [
            sys.executable,
            HEIGHTMAP_ANALYSIS_SCRIPT,
            "--heightmap", heightmap_path,
            "--metadata", metadata_path,
            "--crater-json", crater_json_path,
            "--out-dir", output_dir,
        ]

        append_log("\\nHeightmap analysis started.\\n")
        append_log(f"Using heightmap:\\n  {heightmap_path}\\n")
        append_log(f"Using metadata JSON:\\n  {metadata_path}\\n")
        append_log(f"Using crater JSON:\\n  {crater_json_path}\\n")
        append_log(f"Analysis output folder:\\n  {output_dir}\\n")
        status_var.set("Analyzing heightmap...")
        dialog_status_var.set("Analysis running...")

        run_command(
            cmd,
            PROJECT_DIR,
            "heightmap analysis",
            output_filter=None,
        )

        append_log("\\nHeightmap analysis finished successfully.\\n")
        status_var.set("Heightmap analysis finished.")
        dialog_status_var.set("Analysis completed successfully.")
        open_button.config(state="normal")

        messagebox.showinfo(
            "Heightmap analysis complete",
            f"Analysis results were saved to:\\n\\n{output_dir}",
            parent=dialog,
        )

    except Exception as exc:
        append_log(f"\\nERROR: {exc}\\n")
        status_var.set("Heightmap analysis failed.")
        dialog_status_var.set("Analysis failed. See the activity log.")
        messagebox.showerror(
            "Heightmap analysis failed",
            str(exc),
            parent=dialog,
        )
    finally:
        set_buttons_busy(False)
        if dialog.winfo_exists():
            run_button.config(state="normal")


def open_heightmap_analysis_dialog():
    dialog = tk.Toplevel(root)
    dialog.title("Analyze heightmap")
    dialog.configure(background=BG)
    dialog.transient(root)
    # Allow vertical resizing so the action buttons remain reachable on
    # systems with larger fonts or display scaling.
    dialog.resizable(True, True)
    dialog.minsize(760, 390)

    width = 900
    height = 440
    root.update_idletasks()
    x = root.winfo_rootx() + max(20, (root.winfo_width() - width) // 2)
    y = root.winfo_rooty() + max(20, (root.winfo_height() - height) // 2)
    dialog.geometry(f"{width}x{height}+{x}+{y}")

    dialog.columnconfigure(0, weight=1)
    dialog.rowconfigure(0, weight=1)

    container = ttk.Frame(dialog, padding=(18, 18, 18, 24))
    container.grid(row=0, column=0, sticky="nsew")
    container.columnconfigure(0, weight=1)

    ttk.Label(
        container,
        text="Heightmap analysis",
        font=(UI_FONT, 16, "bold"),
    ).grid(row=0, column=0, sticky="w")

    ttk.Label(
        container,
        text=(
            "Choose one generated heightmap folder. The GUI automatically uses "
            "the timestamped heightmap, metadata, and crater files from that folder."
        ),
        foreground=MUTED,
        wraplength=820,
    ).grid(row=1, column=0, sticky="w", pady=(4, 14))

    inputs_box = ttk.LabelFrame(
        container,
        text="Generated heightmap package",
        padding=12,
        style="Section.TLabelframe",
    )
    inputs_box.grid(row=2, column=0, sticky="ew")
    inputs_box.columnconfigure(1, weight=1)

    folder_var = tk.StringVar()
    output_dir_var = tk.StringVar()
    dialog_status_var = tk.StringVar(value="Choose a generated heightmap folder.")

    ttk.Label(inputs_box, text="Heightmap folder").grid(
        row=0, column=0, sticky="w", padx=(0, 10), pady=6
    )
    ttk.Entry(inputs_box, textvariable=folder_var).grid(
        row=0, column=1, sticky="ew", pady=6
    )

    def resolve_folder(path_value, update_status=True):
        if not str(path_value).strip():
            raise ValueError("Choose a generated heightmap folder.")
        info = resolve_heightmap_run_folder(path_value)
        output_dir = ANALYSIS_HEIGHTMAPS_DIR / info["run_dir"].name
        folder_var.set(str(info["run_dir"]))
        output_dir_var.set(str(output_dir))
        if update_status:
            dialog_status_var.set(
                "Complete timestamped heightmap package found."
            )
        return info, output_dir

    def browse_folder():
        initial = HEIGHTMAP_GENERATIONS_DIR
        current = folder_var.get().strip()
        if current and Path(current).is_dir():
            initial = Path(current).parent
        elif not initial.exists():
            initial = PROJECT_DIR

        selected = filedialog.askdirectory(
            parent=dialog,
            title="Choose generated heightmap folder",
            initialdir=str(initial),
        )
        if selected:
            try:
                resolve_folder(selected)
            except Exception as exc:
                folder_var.set(selected)
                output_dir_var.set("")
                dialog_status_var.set(str(exc))

    folder_actions = ttk.Frame(inputs_box)
    folder_actions.grid(row=0, column=2, sticky="e", padx=(8, 0), pady=6)

    ttk.Button(
        folder_actions,
        text="Browse",
        command=browse_folder,
        style="Secondary.TButton",
    ).grid(row=0, column=0, sticky="ew")

    ttk.Button(
        folder_actions,
        text="Clear",
        command=lambda: (
            folder_var.set(""),
            output_dir_var.set(""),
            dialog_status_var.set("Choose a generated heightmap folder."),
        ),
        style="Secondary.TButton",
    ).grid(row=0, column=1, sticky="ew", padx=(5, 0))

    ttk.Label(inputs_box, text="Results folder").grid(
        row=1, column=0, sticky="w", padx=(0, 10), pady=6
    )
    ttk.Entry(
        inputs_box,
        textvariable=output_dir_var,
        state="readonly",
    ).grid(row=1, column=1, columnspan=2, sticky="ew", pady=6)

    options = ttk.Frame(container)
    options.grid(row=3, column=0, sticky="ew", pady=(12, 0))
    options.columnconfigure(0, weight=1)

    def use_newest_generated():
        candidates = []
        for run_dir in HEIGHTMAP_GENERATIONS_DIR.glob("*"):
            if not run_dir.is_dir():
                continue
            try:
                resolve_heightmap_run_folder(run_dir)
                candidates.append(run_dir)
            except Exception:
                continue
        if not candidates:
            dialog_status_var.set("No complete generated heightmap package was found.")
            return
        newest = max(candidates, key=lambda path: path.stat().st_mtime)
        resolve_folder(newest)

    ttk.Button(
        options,
        text="Use newest generated",
        command=use_newest_generated,
        style="Secondary.TButton",
    ).grid(row=0, column=1, sticky="e")

    ttk.Label(
        container,
        textvariable=dialog_status_var,
        style="Status.TLabel",
        anchor="w",
    ).grid(row=4, column=0, sticky="ew", pady=(14, 8))

    actions = ttk.Frame(container)
    actions.grid(row=5, column=0, sticky="ew")
    actions.columnconfigure(0, weight=1)

    open_button = ttk.Button(
        actions,
        text="Open results folder",
        command=lambda: open_folder(output_dir_var.get()),
        style="Secondary.TButton",
        state="disabled",
    )
    open_button.grid(row=0, column=1, sticky="e", padx=(0, 8))

    def start_analysis():
        try:
            if not HEIGHTMAP_ANALYSIS_SCRIPT.exists():
                raise FileNotFoundError(
                    f"Missing analysis script: {HEIGHTMAP_ANALYSIS_SCRIPT}"
                )

            info, output_dir = resolve_folder(folder_var.get())

            set_buttons_busy(True)
            run_button.config(state="disabled")
            open_button.config(state="disabled")
            dialog_status_var.set("Starting analysis...")

            threading.Thread(
                target=run_heightmap_analysis_thread,
                args=(
                    info["heightmap"],
                    info["metadata"],
                    info["crater_json"],
                    output_dir,
                    dialog,
                    run_button,
                    open_button,
                    dialog_status_var,
                ),
                daemon=True,
            ).start()

        except Exception as exc:
            messagebox.showerror("Invalid analysis input", str(exc), parent=dialog)

    ttk.Button(
        actions,
        text="Close",
        command=dialog.destroy,
        style="Secondary.TButton",
    ).grid(row=0, column=2, sticky="e", padx=(0, 8))

    run_button = ttk.Button(
        actions,
        text="Analyze heightmap",
        command=start_analysis,
        style="Primary.TButton",
    )
    run_button.grid(row=0, column=3, sticky="e")

    use_newest_generated()
    dialog.protocol("WM_DELETE_WINDOW", dialog.destroy)
    dialog.grab_set()
    dialog.focus_set()


# -----------------------------------------------------------------------------
# Rockfield-analysis dialog
# -----------------------------------------------------------------------------

def packaged_rockfield_json(run_dir):
    run_dir = Path(run_dir)
    candidates = []

    legacy = run_dir / "unreal_rockfield.json"
    if legacy.is_file():
        candidates.append(legacy)

    candidates.extend(
        path
        for path in run_dir.glob("*_rockfield.json")
        if path.is_file()
    )
    return newest_path(candidates)


def rockfield_analysis_run_candidates():
    candidates = []
    if ROCKFIELD_GENERATIONS_DIR.exists():
        for run_dir in ROCKFIELD_GENERATIONS_DIR.iterdir():
            if not run_dir.is_dir():
                continue
            if (
                packaged_rockfield_json(run_dir) is not None
                and (run_dir / "rock_settings.json").is_file()
                and (run_dir / "source_heightmap.json").is_file()
            ):
                candidates.append(run_dir)
    return candidates


def newest_rockfield_analysis_run():
    candidates = rockfield_analysis_run_candidates()
    if not candidates:
        return None

    def modified_time(run_dir):
        rockfield = packaged_rockfield_json(run_dir)
        try:
            return rockfield.stat().st_mtime if rockfield is not None else 0.0
        except OSError:
            return 0.0

    return max(candidates, key=modified_time)


def resolve_rockfield_run_folder(run_folder):
    run_dir = Path(run_folder).expanduser().resolve()
    if not run_dir.is_dir():
        raise FileNotFoundError(
            f"Rockfield run folder does not exist: {run_dir}"
        )

    rockfield = packaged_rockfield_json(run_dir)
    rock_settings = run_dir / "rock_settings.json"
    source_manifest_path = run_dir / "source_heightmap.json"

    missing_package_files = []
    if rockfield is None:
        missing_package_files.append("*_rockfield.json")
    missing_package_files.extend(
        path.name
        for path in (rock_settings, source_manifest_path)
        if not path.is_file()
    )
    if missing_package_files:
        raise FileNotFoundError(
            "The selected folder is not a complete compact rockfield run. "
            "Missing: " + ", ".join(missing_package_files)
        )

    source_manifest = read_json(source_manifest_path)
    heightmap = resolve_manifest_path(
        source_manifest.get("heightmap"),
        source_manifest_path.parent,
    )
    metadata = resolve_manifest_path(
        source_manifest.get("metadata"),
        source_manifest_path.parent,
    )
    crater_json = resolve_manifest_path(
        source_manifest.get("craters"),
        source_manifest_path.parent,
    )

    missing_source_files = []
    if metadata is None or not metadata.is_file():
        missing_source_files.append("source metadata.json")
    if crater_json is None or not crater_json.is_file():
        missing_source_files.append("source craters.json")
    if missing_source_files:
        raise FileNotFoundError(
            "The source_heightmap.json link is incomplete or stale. Missing: "
            + ", ".join(missing_source_files)
        )

    return {
        "run_dir": run_dir,
        "rockfield": rockfield,
        "rock_settings": rock_settings,
        "heightmap": (
            heightmap
            if heightmap is not None and heightmap.is_file()
            else None
        ),
        "metadata": metadata,
        "crater_json": crater_json,
        "out_dir": ANALYSIS_ROCKFIELDS_DIR / run_dir.name,
    }


def run_rockfield_analysis_thread(
    inputs,
    dialog,
    run_button,
    open_button,
    dialog_status_var,
):
    output_dir = Path(inputs["out_dir"])
    try:
        ANALYSIS_ROCKFIELDS_DIR.mkdir(parents=True, exist_ok=True)
        output_dir.mkdir(parents=True, exist_ok=True)

        command = [
            sys.executable,
            ROCKFIELD_ANALYSIS_SCRIPT,
            "--rock-json",
            inputs["rockfield"],
            "--metadata",
            inputs["metadata"],
            "--crater-json",
            inputs["crater_json"],
            "--rock-settings",
            inputs["rock_settings"],
            "--out-dir",
            output_dir,
        ]
        if inputs.get("heightmap"):
            command += ["--heightmap", inputs["heightmap"]]

        append_log("\nRockfield analysis started.\n")
        append_log(f"Using compact rockfield run:\n  {inputs['run_dir']}\n")
        append_log(f"Using rockfield JSON:\n  {inputs['rockfield']}\n")
        append_log(f"Using rock settings:\n  {inputs['rock_settings']}\n")
        append_log(f"Using metadata JSON:\n  {inputs['metadata']}\n")
        append_log(f"Using crater JSON:\n  {inputs['crater_json']}\n")
        if inputs.get("heightmap"):
            append_log(f"Using heightmap:\n  {inputs['heightmap']}\n")
        append_log(f"Analysis output folder:\n  {output_dir}\n")

        status_var.set("Analyzing rockfield...")
        dialog_status_var.set("Analysis running...")
        run_command(
            command,
            PROJECT_DIR,
            "rockfield analysis",
            output_filter=None,
        )

        append_log("\nRockfield analysis finished successfully.\n")
        status_var.set("Rockfield analysis finished.")
        dialog_status_var.set("Analysis completed successfully.")
        open_button.config(state="normal")
        messagebox.showinfo(
            "Rockfield analysis complete",
            f"Analysis results were saved to:\n\n{output_dir}",
            parent=dialog,
        )
    except Exception as exc:
        append_log(f"\nERROR: {exc}\n")
        status_var.set("Rockfield analysis failed.")
        dialog_status_var.set("Analysis failed. See the activity log.")
        messagebox.showerror(
            "Rockfield analysis failed",
            str(exc),
            parent=dialog,
        )
    finally:
        set_buttons_busy(False)
        if dialog.winfo_exists():
            run_button.config(state="normal")


def open_rockfield_analysis_dialog():
    dialog = tk.Toplevel(root)
    dialog.title("Analyze rockfield")
    dialog.configure(background=BG)
    dialog.transient(root)
    dialog.resizable(True, True)
    dialog.minsize(820, 360)

    width = 940
    height = 430

    root.update_idletasks()
    x = root.winfo_rootx() + max(
        20,
        (root.winfo_width() - width) // 2,
    )
    y = root.winfo_rooty() + max(
        20,
        (root.winfo_height() - height) // 2,
    )
    dialog.geometry(f"{width}x{height}+{x}+{y}")

    dialog.columnconfigure(0, weight=1)
    dialog.rowconfigure(0, weight=1)

    container = ttk.Frame(dialog, padding=(18, 18, 18, 24))
    container.grid(row=0, column=0, sticky="nsew")
    container.columnconfigure(0, weight=1)

    ttk.Label(
        container,
        text="Rockfield analysis",
        font=(UI_FONT, 16, "bold"),
    ).grid(row=0, column=0, sticky="w")

    ttk.Label(
        container,
        text=(
            "Choose one generated rockfield folder. The rockfield JSON, "
            "generation settings, source heightmap metadata, crater catalog, "
        ),
        foreground=MUTED,
        wraplength=880,
    ).grid(row=1, column=0, sticky="w", pady=(4, 14))

    inputs_box = ttk.LabelFrame(
        container,
        text="Generated rockfield folder",
        padding=12,
        style="Section.TLabelframe",
    )
    inputs_box.grid(row=2, column=0, sticky="ew")
    inputs_box.columnconfigure(0, weight=1)

    run_folder_var = tk.StringVar()
    output_dir_var = tk.StringVar()
    dialog_status_var = tk.StringVar(value="Choose a generated rockfield folder.")

    run_entry = ttk.Entry(
        inputs_box,
        textvariable=run_folder_var,
    )
    run_entry.grid(row=0, column=0, sticky="ew", padx=(0, 8))

    def apply_run_folder(path):
        inputs = resolve_rockfield_run_folder(path)
        run_folder_var.set(str(inputs["run_dir"]))
        output_dir_var.set(str(inputs["out_dir"]))
        if inputs.get("heightmap"):
            dialog_status_var.set(
                "Compact run resolved successfully, including terrain data."
            )
        else:
            dialog_status_var.set(
                "Run resolved. Heightmap is unavailable, so terrain-surface "
                "comparisons will be skipped."
            )
        return inputs

    def browse_run_folder():
        path = filedialog.askdirectory(
            parent=dialog,
            title="Choose generated rockfield folder",
            initialdir=str(
                ROCKFIELD_GENERATIONS_DIR
                if ROCKFIELD_GENERATIONS_DIR.exists()
                else PROJECT_DIR
            ),
        )
        if path:
            try:
                apply_run_folder(path)
            except Exception as exc:
                messagebox.showerror(
                    "Invalid rockfield folder",
                    str(exc),
                    parent=dialog,
                )

    ttk.Button(
        inputs_box,
        text="Browse...",
        command=browse_run_folder,
        style="Secondary.TButton",
    ).grid(row=0, column=1, sticky="e")

    ttk.Label(
        inputs_box,
        text=(
            "Expected files: *_rockfield.json, rock_settings.json, "
            "and source_heightmap.json"
        ),
        foreground=MUTED,
    ).grid(row=1, column=0, columnspan=2, sticky="w", pady=(8, 0))

    output_box = ttk.LabelFrame(
        container,
        text="Analysis output",
        padding=12,
        style="Section.TLabelframe",
    )
    output_box.grid(row=3, column=0, sticky="ew", pady=(12, 0))
    output_box.columnconfigure(0, weight=1)

    ttk.Entry(
        output_box,
        textvariable=output_dir_var,
        state="readonly",
    ).grid(row=0, column=0, sticky="ew")

    options = ttk.Frame(container)
    options.grid(row=4, column=0, sticky="ew", pady=(12, 0))
    options.columnconfigure(0, weight=1)

    ttk.Label(
        options,
        textvariable=dialog_status_var,
        style="Status.TLabel",
        anchor="w",
    ).grid(row=0, column=0, sticky="ew")

    def use_newest_generated():
        newest = newest_rockfield_analysis_run()
        if newest is None:
            run_folder_var.set("")
            output_dir_var.set(str(ANALYSIS_ROCKFIELDS_DIR))
            dialog_status_var.set("No complete generated rockfield run was found.")
            return
        try:
            apply_run_folder(newest)
        except Exception as exc:
            dialog_status_var.set(str(exc))

    ttk.Button(
        options,
        text="Use newest generated",
        command=use_newest_generated,
        style="Secondary.TButton",
    ).grid(row=0, column=1, sticky="e")

    actions = ttk.Frame(container)
    actions.grid(row=5, column=0, sticky="ew", pady=(18, 0))
    actions.columnconfigure(0, weight=1)

    open_button = ttk.Button(
        actions,
        text="Open results folder",
        command=lambda: open_folder(output_dir_var.get()),
        style="Secondary.TButton",
        state="disabled",
    )
    open_button.grid(row=0, column=1, sticky="e", padx=(0, 8))

    def start_analysis():
        try:
            if not ROCKFIELD_ANALYSIS_SCRIPT.exists():
                raise FileNotFoundError(
                    f"Missing analysis script: {ROCKFIELD_ANALYSIS_SCRIPT}"
                )

            run_folder = run_folder_var.get().strip()
            if not run_folder:
                raise ValueError("Choose a generated rockfield folder.")

            inputs = apply_run_folder(run_folder)

            set_buttons_busy(True)
            run_button.config(state="disabled")
            open_button.config(state="disabled")
            dialog_status_var.set("Starting analysis...")

            threading.Thread(
                target=run_rockfield_analysis_thread,
                args=(
                    inputs,
                    dialog,
                    run_button,
                    open_button,
                    dialog_status_var,
                ),
                daemon=True,
            ).start()
        except Exception as exc:
            messagebox.showerror(
                "Invalid analysis input",
                str(exc),
                parent=dialog,
            )

    ttk.Button(
        actions,
        text="Close",
        command=dialog.destroy,
        style="Secondary.TButton",
    ).grid(row=0, column=2, sticky="e", padx=(0, 8))

    run_button = ttk.Button(
        actions,
        text="Analyze rockfield",
        command=start_analysis,
        style="Primary.TButton",
    )
    run_button.grid(row=0, column=3, sticky="e")

    use_newest_generated()
    dialog.protocol("WM_DELETE_WINDOW", dialog.destroy)
    dialog.grab_set()
    dialog.focus_set()


def filter_heightmap_summary(output):
    lines = output.splitlines()
    summary = []
    inside_summary = False

    for line in lines:
        stripped = line.strip()

        if line.startswith("Generated terrain-aware lunar heightmap"):
            inside_summary = True

        if inside_summary:
            summary.append(line)

        if inside_summary and stripped.startswith("actual height min/max:"):
            break

    if summary:
        return "\n".join(summary).rstrip() + "\n"

    # Fallback if the heightmap script output format changes.
    ignored_fragments = [
        "DeprecationWarning:",
        "Image.fromarray(",
        "Output files:",
        "Analysis command:",
    ]
    filtered = []
    skip_output_paths = False

    for line in lines:
        stripped = line.strip()

        if stripped == "Output files:" or stripped == "Analysis command:":
            skip_output_paths = True
            continue

        if skip_output_paths:
            continue

        if any(fragment in line for fragment in ignored_fragments):
            continue

        filtered.append(line)

    return "\n".join(filtered).rstrip() + "\n"

def filter_rockfield_summary(output):
    lines = output.splitlines()
    summary = []

    for line in lines:
        stripped = line.strip()
        if stripped.startswith("Generated ") and " rocks for " in stripped:
            summary.append(stripped)

    if summary:
        return "\n".join(summary).rstrip() + "\n"

    # Fallback if the rockfield script output format changes.
    ignored_prefixes = (
        "Wrote summary:",
        "Manifest:",
    )

    filtered = []
    for line in lines:
        stripped = line.strip()
        if not stripped:
            continue
        if stripped.startswith(ignored_prefixes):
            continue
        filtered.append(line)

    return "\n".join(filtered).rstrip() + "\n"

def run_command(cmd, cwd, label, output_filter=None):
    proc = subprocess.Popen(
        [str(x) for x in cmd],
        cwd=str(cwd),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
    )

    output_lines = []
    assert proc.stdout is not None
    for line in proc.stdout:
        output_lines.append(line)
        if output_filter is None:
            append_log(line)

    output = "".join(output_lines)
    code = proc.wait()

    if output_filter is not None:
        append_log(output_filter(output))

    if code != 0:
        raise RuntimeError(f"{label} failed with exit code {code}")

    return output

def newest_file(folder, pattern, newer_than=None):
    folder = Path(folder)
    candidates = list(folder.glob(pattern)) if folder.exists() else []
    if newer_than is not None:
        candidates = [p for p in candidates if p.stat().st_mtime >= newer_than]
    if not candidates:
        return None
    return max(candidates, key=lambda p: p.stat().st_mtime)

def unique_existing_dirs(paths):
    result = []
    seen = set()

    for path in paths:
        try:
            path = Path(path)
            key = str(path.resolve())
        except OSError:
            continue

        if key in seen:
            continue

        seen.add(key)
        if path.exists() and path.is_dir():
            result.append(path)

    return result


def rockfield_search_dirs(run_dir):
    """
    Search both the new Tools layout and older hardcoded project layout.
    """
    run_dir = Path(run_dir)

    return unique_existing_dirs([
        run_dir,
        ROCKFIELD_GENERATIONS_DIR,
        HEIGHTMAP_GENERATIONS_DIR,
        FINAL_DIR,
        LEGACY_FINAL_DIR,

        # If the .sh is inside Tools, this finds the project root Heightmaps/final.
        PROJECT_DIR.parent / "Heightmaps" / "final",
        HEIGHTMAP_DIR.parent / "Heightmaps" / "final",

        # Other common layouts.
        PROJECT_DIR / "Heightmaps" / "final",
        HEIGHTMAP_DIR / "Heightmaps" / "final",
        ROCKFIELD_SCRIPT.parent / "final",
        ROCKFIELD_SCRIPT.parent / "Heightmaps" / "final",
    ])


def find_unreal_rockfield_json(run_dir, base_name, newer_than=None):
    search_dirs = rockfield_search_dirs(run_dir)

    exact_names = [
        f"{base_name}_unreal_rockfield.json",
        f"{base_name}_rockfield.json",
    ]

    candidates = []

    for folder in search_dirs:
        for exact_name in exact_names:
            exact = folder / exact_name
            if exact.exists() and exact.is_file():
                candidates.append(exact)

        for pattern in ("*_unreal_rockfield.json", "*_rockfield.json"):
            for match in folder.glob(pattern):
                if match.exists() and match.is_file():
                    candidates.append(match)

    if newer_than is not None:
        filtered = []
        for candidate in candidates:
            try:
                if candidate.stat().st_mtime >= newer_than:
                    filtered.append(candidate)
            except OSError:
                pass
        candidates = filtered

    if not candidates:
        return None

    # Prefer a filename containing the expected base name. Otherwise take newest.
    matching_base = [p for p in candidates if base_name in p.name]
    if matching_base:
        return max(matching_base, key=lambda p: p.stat().st_mtime)

    return max(candidates, key=lambda p: p.stat().st_mtime)


def convert_minimal_unreal_json_if_needed(expected_path, terrain_name, crater_json):
    if expected_path.exists():
        return expected_path

    output_dir = expected_path.parent
    positions_path = output_dir / "rock_positions.json"
    metadata_csv = output_dir / "rock_metadata.csv"

    # Some older rockfield script versions ignore --out-dir and write to FINAL_DIR.
    # Fall back to those files if the run folder did not receive them.
    if not positions_path.exists():
        for fallback_positions in [
            FINAL_DIR / "rock_positions.json",
            LEGACY_FINAL_DIR / "rock_positions.json",
            PROJECT_DIR.parent / "Heightmaps" / "final" / "rock_positions.json",
            HEIGHTMAP_DIR.parent / "Heightmaps" / "final" / "rock_positions.json",
        ]:
            if fallback_positions.exists():
                positions_path = fallback_positions
                break
    if not metadata_csv.exists():
        for fallback_metadata in [
            FINAL_DIR / "rock_metadata.csv",
            LEGACY_FINAL_DIR / "rock_metadata.csv",
            PROJECT_DIR.parent / "Heightmaps" / "final" / "rock_metadata.csv",
            HEIGHTMAP_DIR.parent / "Heightmaps" / "final" / "rock_metadata.csv",
        ]:
            if fallback_metadata.exists():
                metadata_csv = fallback_metadata
                break
    rocks = []

    if metadata_csv.exists():
        with metadata_csv.open("r", encoding="utf-8", newline="") as f:
            for row in csv.DictReader(f):
                try:
                    rocks.append({
                        "instance_id": int(row.get("instance_id", len(rocks))),
                        "x_m": float(row["x_m"]),
                        "y_m": float(row["y_m"]),
                        "diameter_m": float(row["diameter_m"]),
                        "size_class": row.get("size_class", ""),
                        "material_type": row.get("material_type", ""),
                        "crater_zone": row.get("crater_zone", ""),
                        "dominant_crater_index": int(float(row.get("dominant_crater_index", -1))),
                        "distance_to_dominant_crater_center_m": float(row.get("distance_to_dominant_crater_center_m", -1.0)),
                        "normalized_crater_radius": float(row.get("normalized_crater_radius", -1.0)),
                        "local_slope_deg": float(row.get("local_slope_deg", 0.0)),
                        "local_density_per_m2": float(row.get("local_density_per_m2", 0.0)),
                        "acceptance_probability": float(row.get("acceptance_probability", 1.0)),
                        "source_type": row.get("source_type", ""),
                        "clump_id": int(float(row.get("clump_id", -1))),
                    })
                except Exception:
                    continue
    elif positions_path.exists():
        with positions_path.open("r", encoding="utf-8") as f:
            root = json.load(f)
        rocks = root.get("rocks", [])

    if not rocks:
        return None

    expected_path.parent.mkdir(parents=True, exist_ok=True)
    with expected_path.open("w", encoding="utf-8") as f:
        json.dump({
            "format": "MoonSimOfflineRockField",
            "version": 1,
            "units": "meters",
            "coordinate_frame": "centered_map_meters",
            "terrain_name": terrain_name,
            "source_crater_json": str(crater_json),
            "rock_count": len(rocks),
            "rocks": rocks,
        }, f, indent=2)
    return expected_path

def move_primary_file(source_path, destination_path):
    source_path = Path(source_path)
    destination_path = Path(destination_path)
    if not source_path.exists():
        raise FileNotFoundError(source_path)

    destination_path.parent.mkdir(parents=True, exist_ok=True)
    if destination_path.exists():
        destination_path.unlink()

    try:
        if source_path.resolve() == destination_path.resolve():
            return destination_path
    except OSError:
        pass

    shutil.move(str(source_path), str(destination_path))
    return destination_path


def remove_empty_directories(root_dir):
    root_dir = Path(root_dir)
    if not root_dir.exists():
        return
    for directory in sorted(
        (path for path in root_dir.rglob("*") if path.is_dir()),
        key=lambda path: len(path.parts),
        reverse=True,
    ):
        try:
            directory.rmdir()
        except OSError:
            pass


def first_existing_file(candidates):
    for candidate in candidates:
        candidate = Path(candidate)
        if candidate.exists() and candidate.is_file():
            return candidate
    return None


def first_recursive_file(root_dir, names=(), patterns=()):
    root_dir = Path(root_dir)
    for name in names:
        matches = sorted(root_dir.rglob(name))
        if matches:
            return matches[0]
    for pattern in patterns:
        matches = sorted(root_dir.rglob(pattern))
        if matches:
            return matches[0]
    return None


def relative_or_absolute(path, start_dir):
    if path is None:
        return None
    path = Path(path).resolve()
    try:
        return os.path.relpath(path, Path(start_dir).resolve())
    except (OSError, ValueError):
        return str(path)


def resolve_manifest_path(value, manifest_dir):
    if not value:
        return None
    path = Path(str(value)).expanduser()
    if not path.is_absolute():
        path = Path(manifest_dir) / path
    return path.resolve()


def finalize_heightmap_run(
    run_name,
    run_dir,
    generator_out_dir,
):
    """Move a timestamped compact heightmap package into its final run folder."""
    run_dir = Path(run_dir)
    generator_out_dir = Path(generator_out_dir)

    metadata_candidates = sorted(
        generator_out_dir.glob("*_metadata.json"),
        key=lambda path: path.stat().st_mtime,
        reverse=True,
    )
    if not metadata_candidates:
        # Compatibility with the original fixed-name generator.
        legacy_metadata = generator_out_dir / "metadata.json"
        if legacy_metadata.is_file():
            metadata_candidates = [legacy_metadata]

    if not metadata_candidates:
        raise FileNotFoundError(
            "Heightmap generation did not produce a metadata JSON file."
        )

    metadata_source = metadata_candidates[0]
    package_metadata = read_json(metadata_source)
    output_files = package_metadata.get("output_files", {})
    if not isinstance(output_files, dict):
        raise ValueError(
            f"Invalid output_files section in generated metadata: {metadata_source}"
        )

    def package_file(key, legacy_name):
        value = output_files.get(key)
        if value:
            path = resolve_manifest_path(value, metadata_source.parent)
            if path is not None:
                return path
        return generator_out_dir / legacy_name

    heightmap_source = package_file("heightmap", "heightmap.png")
    crater_source = package_file("craters", "craters.json")
    summary_source = package_file(
        "generation_summary",
        "generation_summary.txt",
    )

    required = [
        heightmap_source,
        metadata_source,
        crater_source,
        summary_source,
    ]
    missing = [path.name for path in required if not path.is_file()]
    if missing:
        raise FileNotFoundError(
            "Heightmap generation produced an incomplete package. Missing: "
            + ", ".join(missing)
        )

    # Keep the generator's timestamp/preset/seed filenames intact.
    canonical_heightmap = move_primary_file(
        heightmap_source,
        run_dir / heightmap_source.name,
    )
    canonical_craters = move_primary_file(
        crater_source,
        run_dir / crater_source.name,
    )
    canonical_summary = move_primary_file(
        summary_source,
        run_dir / summary_source.name,
    )
    canonical_metadata = move_primary_file(
        metadata_source,
        run_dir / metadata_source.name,
    )

    HEIGHTMAP_PNG_DIR.mkdir(parents=True, exist_ok=True)
    unreal_heightmap = HEIGHTMAP_PNG_DIR / f"{run_name}.png"
    shutil.copy2(canonical_heightmap, unreal_heightmap)

    remove_empty_directories(generator_out_dir)
    try:
        generator_out_dir.rmdir()
    except OSError:
        pass

    return {
        "run_dir": run_dir,
        "heightmap": canonical_heightmap,
        "metadata": canonical_metadata,
        "crater_json": canonical_craters,
        "generation_summary": canonical_summary,
        "unreal_heightmap": unreal_heightmap,
        "base_name": base_name_from_crater_json(canonical_craters),
        "run_name": run_name,
    }


def finalize_rockfield_run(
    run_name,
    run_dir,
    generator_out_dir,
    unreal_source,
    source_info,
):
    run_dir = Path(run_dir)
    generator_out_dir = Path(generator_out_dir)
    unreal_source = Path(unreal_source)

    # Keep the generator's timestamp/terrain/seed filename intact.
    canonical_rockfield = move_primary_file(
        unreal_source,
        run_dir / unreal_source.name,
    )

    # The generator originally wrote relative source paths from its temporary
    # directory. Rebase them to the final compact run folder after moving it.
    try:
        rockfield_payload = read_json(canonical_rockfield)
        rockfield_payload["path_base"] = "this_json_directory"
        rockfield_payload["source_crater_json"] = relative_or_absolute(
            source_info.get("crater_json"),
            run_dir,
        )
        rockfield_payload["source_metadata_json"] = relative_or_absolute(
            source_info.get("metadata"),
            run_dir,
        )
        with canonical_rockfield.open("w", encoding="utf-8") as file:
            json.dump(rockfield_payload, file, indent=2, allow_nan=False)
    except Exception as exc:
        raise RuntimeError(
            f"Could not rebase source paths in {canonical_rockfield.name}: {exc}"
        ) from exc

    UNREAL_ROCKFIELD_DIR.mkdir(parents=True, exist_ok=True)
    unreal_import = UNREAL_ROCKFIELD_DIR / f"{run_name}.json"
    shutil.copy2(canonical_rockfield, unreal_import)

    source_manifest = {
        "format": "MoonSimRockfieldSourceHeightmap",
        "version": 1,
        "heightmap_run": (
            Path(source_info["run_dir"]).name
            if source_info.get("run_dir")
            else None
        ),
        "heightmap": relative_or_absolute(
            source_info.get("heightmap"),
            run_dir,
        ),
        "metadata": relative_or_absolute(
            source_info.get("metadata"),
            run_dir,
        ),
        "craters": relative_or_absolute(
            source_info.get("crater_json"),
            run_dir,
        ),
    }
    with (run_dir / "source_heightmap.json").open(
        "w",
        encoding="utf-8",
    ) as file:
        json.dump(source_manifest, file, indent=2)

    if generator_out_dir.exists() and generator_out_dir != run_dir:
        shutil.rmtree(generator_out_dir, ignore_errors=True)

    allowed_files = {
        canonical_rockfield.name,
        "rock_settings.json",
        "source_heightmap.json",
    }
    for child in run_dir.iterdir():
        if child.name in allowed_files:
            continue
        if child.is_dir():
            shutil.rmtree(child, ignore_errors=True)
        else:
            try:
                child.unlink()
            except OSError:
                pass

    return {
        "run_dir": run_dir,
        "unreal_rockfield": canonical_rockfield,
        "unreal_import": unreal_import,
    }


current_run = {
    "run_dir": None,
    "heightmap_out_dir": None,
    "heightmap": None,
    "crater_json": None,
    "metadata": None,
    "base_name": None,
    "run_name": None,
    "terrain_label": None,
}

def set_buttons_busy(is_busy):
    state = "disabled" if is_busy else "normal"
    heightmap_button.config(state=state)
    rock_button.config(state=state)
    analyze_heightmap_button.config(state=state)
    analyze_rockfield_button.config(state=state)
    reset_button.config(state=state)
    close_button.config(state=state)

def unique_generation_run_dir(parent_dir, run_name):
    parent_dir = Path(parent_dir)
    parent_dir.mkdir(parents=True, exist_ok=True)

    run_dir = parent_dir / run_name
    counter = 2
    while run_dir.exists():
        run_dir = parent_dir / f"{run_name}_{counter:02d}"
        counter += 1

    return run_dir.name, run_dir


def make_run_folder(state):
    run_timestamp = time.strftime("%Y%m%d_%H%M%S")
    terrain_slug = slugify(
        state.get("terrain_label", state.get("rock_profile", "terrain"))
    )
    run_name, run_dir = unique_generation_run_dir(
        HEIGHTMAP_GENERATIONS_DIR,
        f"{run_timestamp}_{terrain_slug}_seed{int(state['seed'])}",
    )
    generator_out_dir = run_dir / "intermediate"
    generator_out_dir.mkdir(parents=True, exist_ok=True)
    return run_name, run_dir, generator_out_dir

def make_rock_run_folder(state):
    run_timestamp = time.strftime("%Y%m%d_%H%M%S")
    terrain_slug = slugify(
        state.get("terrain_label", state.get("rock_profile", "terrain"))
    )
    run_name, run_dir = unique_generation_run_dir(
        ROCKFIELD_GENERATIONS_DIR,
        f"{run_timestamp}_{terrain_slug}_seed{int(state['seed'])}",
    )
    run_dir.mkdir(parents=True, exist_ok=False)
    generator_out_dir = Path(
        tempfile.mkdtemp(
            prefix=f".{run_name}_generator_",
            dir=str(ROCKFIELD_GENERATIONS_DIR),
        )
    )
    return run_name, run_dir, generator_out_dir

def make_rock_settings_json(state, run_dir, run_name):
    settings_json = Path(run_dir) / "rock_settings.json"
    rock_settings = dict(state["rock"])
    rock_settings["seed"] = int(state["seed"])
    rock_settings["map_size_meters_x"] = float(state["map_size_m"])
    rock_settings["map_size_meters_y"] = float(state["map_size_m"])
    rock_settings["rock_profile"] = str(state["rock_profile"])
    rock_settings["background_cap_mode"] = "maxrocks"
    rock_settings["use_scientific_preset_values"] = False

    with settings_json.open("w", encoding="utf-8") as file:
        json.dump({"settings": rock_settings}, file, indent=2)

    return settings_json

def find_metadata_json(heightmap_out_dir, base_name):
    candidates = [
        heightmap_out_dir / "metadata" / f"{base_name}.json",
        heightmap_out_dir / "json" / f"{base_name}.json",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


def all_generation_run_dirs():
    """Return clean packaged runs plus recognized legacy run folders."""
    run_dirs = []
    seen = set()

    clean_parents = (
        HEIGHTMAP_GENERATIONS_DIR,
        ROCKFIELD_GENERATIONS_DIR,
    )
    for parent in clean_parents:
        if not parent.exists():
            continue
        for child in parent.iterdir():
            if child.is_dir():
                key = str(child.resolve())
                if key not in seen:
                    seen.add(key)
                    run_dirs.append(child)

    # Legacy generation_files is read-only compatibility. New files are never
    # written there.
    legacy_root = PROJECT_DIR / "generation_files"
    if legacy_root.exists():
        for child in legacy_root.iterdir():
            if child.is_dir() and child.name not in {
                "heightmap_generations",
                "rockfield_generations",
            }:
                key = str(child.resolve())
                if key not in seen:
                    seen.add(key)
                    run_dirs.append(child)

    return run_dirs

def resolve_heightmap_run_folder(run_folder):
    run_dir = Path(run_folder).expanduser().resolve()
    if not run_dir.is_dir():
        raise FileNotFoundError(f"Heightmap run folder does not exist: {run_dir}")

    # New timestamped package: discover metadata, then trust its output_files map.
    metadata_candidates = sorted(
        run_dir.glob("*_metadata.json"),
        key=lambda path: path.stat().st_mtime,
        reverse=True,
    )

    # Compatibility with older compact packages.
    legacy_metadata = run_dir / "metadata.json"
    if legacy_metadata.is_file():
        metadata_candidates.append(legacy_metadata)

    for metadata_path in metadata_candidates:
        try:
            package_metadata = read_json(metadata_path)
            output_files = package_metadata.get("output_files", {})
            if not isinstance(output_files, dict):
                continue

            heightmap = resolve_manifest_path(
                output_files.get("heightmap"),
                metadata_path.parent,
            )
            crater_json = resolve_manifest_path(
                output_files.get("craters"),
                metadata_path.parent,
            )

            # Old compact metadata may not contain output_files.
            if heightmap is None:
                old_heightmap = run_dir / "heightmap.png"
                heightmap = old_heightmap if old_heightmap.is_file() else None
            if crater_json is None:
                old_craters = run_dir / "craters.json"
                crater_json = old_craters if old_craters.is_file() else None

            if (
                heightmap is not None
                and heightmap.is_file()
                and crater_json is not None
                and crater_json.is_file()
            ):
                return {
                    "run_dir": run_dir,
                    "heightmap": heightmap,
                    "metadata": metadata_path,
                    "crater_json": crater_json,
                    "base_name": base_name_from_crater_json(crater_json),
                    "run_name": run_dir.name,
                }
        except Exception:
            continue

    # Original fixed-name compact package without a modern output_files map.
    clean_heightmap = run_dir / "heightmap.png"
    clean_metadata = run_dir / "metadata.json"
    clean_craters = run_dir / "craters.json"
    if clean_heightmap.is_file() and clean_metadata.is_file() and clean_craters.is_file():
        return {
            "run_dir": run_dir,
            "heightmap": clean_heightmap,
            "metadata": clean_metadata,
            "crater_json": clean_craters,
            "base_name": run_dir.name,
            "run_name": run_dir.name,
        }

    # Legacy packaged heightmap run.
    generator_root = run_dir / "heightmap" if (run_dir / "heightmap").is_dir() else run_dir
    crater_candidates = list(generator_root.glob("rockfield_json/*_rockfield_craters.json"))
    crater_candidates += list(generator_root.rglob("*_rockfield_craters.json"))
    crater_json = newest_path(crater_candidates)
    if crater_json is None:
        raise FileNotFoundError(
            f"No craters.json or *_rockfield_craters.json was found in {run_dir}."
        )
    base_name = base_name_from_crater_json(crater_json)
    metadata = find_metadata_json(generator_root, base_name)
    if metadata is None:
        metadata = find_metadata_for_crater_json(crater_json, base_name)
    if metadata is None or not Path(metadata).is_file():
        raise FileNotFoundError(f"No matching metadata JSON was found in {run_dir}.")

    heightmap_candidates = [
        generator_root / "png16" / f"{base_name}.png",
        generator_root / f"{base_name}.png",
    ]
    heightmap = first_existing_file(heightmap_candidates)
    if heightmap is None:
        exact = [path for path in generator_root.rglob("*.png") if path.stem == base_name]
        heightmap = exact[0] if exact else None
    if heightmap is None:
        raise FileNotFoundError(f"No matching main heightmap PNG was found in {run_dir}.")

    return {
        "run_dir": run_dir,
        "heightmap": Path(heightmap),
        "metadata": Path(metadata),
        "crater_json": Path(crater_json),
        "base_name": base_name,
        "run_name": run_dir.name,
    }


def heightmap_run_for_asset(asset_path):
    asset_path = Path(asset_path).expanduser().resolve()
    if asset_path.parent.parent == HEIGHTMAP_GENERATIONS_DIR:
        return asset_path.parent
    if asset_path.parent == HEIGHTMAP_PNG_DIR:
        candidate = HEIGHTMAP_GENERATIONS_DIR / asset_path.stem
        if candidate.is_dir():
            return candidate
    return None


def find_metadata_for_crater_json(crater_json, base_name):
    crater_json = Path(crater_json)
    if crater_json.name == "craters.json":
        sibling = crater_json.parent / "metadata.json"
        if sibling.is_file():
            return sibling
    elif crater_json.name.endswith("_craters.json"):
        prefix = crater_json.name[:-len("_craters.json")]
        sibling = crater_json.parent / f"{prefix}_metadata.json"
        if sibling.is_file():
            return sibling
    candidates = []

    possible_base_names = []
    if base_name:
        possible_base_names.append(str(base_name))

    crater_stem_base = base_name_from_crater_json(crater_json)
    if crater_stem_base not in possible_base_names:
        possible_base_names.append(crater_stem_base)

    # Normal heightmap output layout:
    # generated/<run>/heightmap/rockfield_json/<base>_rockfield_craters.json
    if crater_json.parent.name == "rockfield_json":
        heightmap_out_dir = crater_json.parent.parent
        for possible_base in possible_base_names:
            candidates.extend([
                heightmap_out_dir / "metadata" / f"{possible_base}.json",
                heightmap_out_dir / "json" / f"{possible_base}.json",
            ])

    # Exported crater layout:
    # generated/heightmaps/<run_name>_<original_base>_rockfield_craters.json
    # Metadata stays in generated/<run_name>/heightmap/metadata/<original_base>.json
    try:
        if crater_json.parent.resolve() == CRATER_JSON_DIR.resolve():
            crater_name = crater_json.name
            for run_dir in sorted(all_generation_run_dirs(), key=lambda p: len(p.name), reverse=True):
                if not run_dir.is_dir():
                    continue

                prefix = run_dir.name + "_"
                if crater_name.startswith(prefix) and crater_name.endswith("_rockfield_craters.json"):
                    original_crater_name = crater_name[len(prefix):]
                    original_base = original_crater_name.replace("_rockfield_craters.json", "")
                    if original_base and original_base not in possible_base_names:
                        possible_base_names.append(original_base)

                    for possible_base in possible_base_names:
                        candidates.extend([
                            run_dir / "heightmap" / "metadata" / f"{possible_base}.json",
                            run_dir / "heightmap" / "json" / f"{possible_base}.json",
                            run_dir / "metadata" / f"{possible_base}.json",
                            run_dir / "json" / f"{possible_base}.json",
                            run_dir / f"{possible_base}.json",
                        ])
    except OSError:
        pass

    for possible_base in possible_base_names:
        candidates.extend([
            crater_json.parent / f"{possible_base}.json",
            crater_json.parent.parent / "metadata" / f"{possible_base}.json" if crater_json.parent.parent else crater_json.parent / f"{possible_base}.json",
            crater_json.parent.parent / "json" / f"{possible_base}.json" if crater_json.parent.parent else crater_json.parent / f"{possible_base}.json",
        ])

    seen = set()
    for candidate in candidates:
        try:
            if not candidate:
                continue
            key = str(candidate)
            if key in seen:
                continue
            seen.add(key)
            if candidate.exists():
                return candidate
        except OSError:
            pass

    return None


def require_metadata_for_crater_json(crater_json, base_name):
    metadata = find_metadata_for_crater_json(crater_json, base_name)
    if metadata and Path(metadata).exists():
        return Path(metadata)

    raise FileNotFoundError(
        "Metadata JSON is required for rock generation, but it was not found for this crater JSON.\n\n"
        f"Crater JSON:\n  {crater_json}\n\n"
        "Expected the matching metadata JSON in one of these places:\n"
        "  generated/<heightmap_run>/heightmap/metadata/<base>.json\n"
        "  generated/<heightmap_run>/heightmap/json/<base>.json\n"
        "  next to the selected crater JSON\n\n"
        "Generate the heightmap with this GUI again, or keep the matching metadata JSON with the crater JSON."
    )


def base_name_from_crater_json(crater_json):
    path = Path(crater_json)
    if path.name == "craters.json":
        manifest_path = path.parent / "run_manifest.json"
        if manifest_path.is_file():
            try:
                manifest = read_json(manifest_path)
                value = manifest.get("generator_base_name")
                if value:
                    return str(value)
            except Exception:
                pass
        return path.parent.name

    name = path.name
    if name.endswith("_rockfield_craters.json"):
        return name.replace("_rockfield_craters.json", "")
    if name.endswith("_craters.json"):
        return name[:-len("_craters.json")]
    return path.stem

def find_latest_heightmap_run():
    clean_runs = []
    if HEIGHTMAP_GENERATIONS_DIR.exists():
        for run_dir in HEIGHTMAP_GENERATIONS_DIR.glob("*"):
            if not run_dir.is_dir():
                continue
            try:
                resolve_heightmap_run_folder(run_dir)
                clean_runs.append(run_dir)
            except Exception:
                continue

    if clean_runs:
        latest = max(clean_runs, key=lambda path: path.stat().st_mtime)
        return resolve_heightmap_run_folder(latest)

    legacy_candidates = []
    legacy_root = PROJECT_DIR / "generation_files"
    if legacy_root.exists():
        legacy_candidates.extend(
            legacy_root.glob("*/heightmap/rockfield_json/*_rockfield_craters.json")
        )
    if not legacy_candidates:
        return None
    crater_json = max(legacy_candidates, key=lambda path: path.stat().st_mtime)
    try:
        return resolve_heightmap_run_folder(crater_json.parents[2])
    except Exception:
        return None


def make_crater_segments_json(state):
    """Create a temporary CLI override without adding files to the run package."""
    preset_name = str(state["heightmap_preset"])
    templates = CRATER_SEGMENT_TEMPLATES.get(preset_name)
    if not templates or len(templates) != 2:
        raise ValueError(
            f"No two-segment crater template is configured for {preset_name}."
        )

    k_values = [
        float(state["crater_k_small"]),
        float(state["crater_k_large"]),
    ]
    if any(value < 0.0 for value in k_values):
        raise ValueError("Crater K values must be zero or greater.")

    segments = []
    for template, k_value in zip(templates, k_values):
        segment = dict(template)
        segment["K"] = k_value
        segments.append(segment)

    with tempfile.NamedTemporaryFile(
        mode="w",
        encoding="utf-8",
        suffix="_moonsim_crater_segments.json",
        delete=False,
    ) as file:
        json.dump(segments, file, indent=2)
        return Path(file.name)


def heightmap_generation_thread(state):
    crater_segments_json = None
    try:
        HEIGHTMAP_GENERATIONS_DIR.mkdir(parents=True, exist_ok=True)
        ROCKFIELD_GENERATIONS_DIR.mkdir(parents=True, exist_ok=True)
        HEIGHTMAP_PNG_DIR.mkdir(parents=True, exist_ok=True)
        UNREAL_ROCKFIELD_DIR.mkdir(parents=True, exist_ok=True)
        ANALYSIS_HEIGHTMAPS_DIR.mkdir(parents=True, exist_ok=True)
        ANALYSIS_ROCKFIELDS_DIR.mkdir(parents=True, exist_ok=True)

        run_name, run_dir, generator_out_dir = make_run_folder(state)
        crater_segments_json = make_crater_segments_json(state)

        append_log("\nHeightmap generation started.\n")
        append_log(f"Run package:\n  {run_dir}\n")
        status_var.set("Generating heightmap...")

        if not HEIGHTMAP_SCRIPT.exists():
            raise FileNotFoundError(f"Missing heightmap script: {HEIGHTMAP_SCRIPT}")

        heightmap_cmd = [
            sys.executable,
            HEIGHTMAP_SCRIPT,
            "--out", generator_out_dir,
            "--preset", state["heightmap_preset"],
            "--seed", int(state["seed"]),
            "--size", int(state["heightmap_size"]),
            "--map-size-m", float(state["map_size_m"]),
            "--height-range-m", float(state["height_range_m"]),
            "--range-mode", state["range_mode"],
            "--segments-json", crater_segments_json,
        ]

        run_command(
            heightmap_cmd,
            HEIGHTMAP_DIR,
            "heightmap generator",
            output_filter=filter_heightmap_summary,
        )

        packaged = finalize_heightmap_run(
            run_name,
            run_dir,
            generator_out_dir,
        )

        current_run.update({
            "run_dir": packaged["run_dir"],
            "heightmap_out_dir": packaged["run_dir"],
            "heightmap": packaged["heightmap"],
            "crater_json": packaged["crater_json"],
            "metadata": packaged["metadata"],
            "base_name": packaged["base_name"],
            "run_name": packaged["run_name"],
            "terrain_label": state.get("terrain_label"),
        })

        root.after(
            0,
            lambda: existing_heightmap_run_var.set(
                str(packaged["run_dir"])
            ),
        )
        root.after(
            0,
            lambda: existing_crater_json_var.set(
                str(packaged["crater_json"])
            ),
        )
        root.after(
            0,
            lambda: existing_metadata_json_var.set(
                str(packaged["metadata"])
            ),
        )

        append_log("Primary heightmap files:\n")
        append_log(f"  {packaged['heightmap']}\n")
        append_log(f"  {packaged['metadata']}\n")
        append_log(f"  {packaged['crater_json']}\n")
        append_log("Unreal Landscape import:\n")
        append_log(f"  {packaged['unreal_heightmap']}\n")
        append_log("\nHeightmap generation finished.\n")
        status_var.set(
            "Heightmap generation finished. The compact run is ready for analysis and rock generation."
        )

    except Exception as exc:
        append_log(f"\nERROR: {exc}\n")
        status_var.set("Heightmap generation failed.")
        messagebox.showerror("Heightmap generation failed", str(exc))
    finally:
        if crater_segments_json is not None:
            try:
                Path(crater_segments_json).unlink(missing_ok=True)
            except OSError:
                pass
        set_buttons_busy(False)


def get_heightmap_source_for_rocks(state):
    selected_run = str(state.get("existing_heightmap_run", "")).strip()
    if selected_run:
        info = resolve_heightmap_run_folder(selected_run)
        info["source_label"] = "selected heightmap run"
        return info

    run_info = dict(current_run)
    if run_info.get("run_dir"):
        try:
            info = resolve_heightmap_run_folder(run_info["run_dir"])
            info["source_label"] = "current generated heightmap run"
            return info
        except Exception:
            pass

    raise FileNotFoundError(
        "Rock generation needs a complete heightmap run. Generate a heightmap first, "
        "or choose a complete folder under generated/heightmaps."
    )

def rock_generation_thread(state):
    generator_out_dir = None
    try:
        HEIGHTMAP_GENERATIONS_DIR.mkdir(parents=True, exist_ok=True)
        ROCKFIELD_GENERATIONS_DIR.mkdir(parents=True, exist_ok=True)
        HEIGHTMAP_PNG_DIR.mkdir(parents=True, exist_ok=True)
        UNREAL_ROCKFIELD_DIR.mkdir(parents=True, exist_ok=True)
        ANALYSIS_HEIGHTMAPS_DIR.mkdir(parents=True, exist_ok=True)
        ANALYSIS_ROCKFIELDS_DIR.mkdir(parents=True, exist_ok=True)

        source_info = get_heightmap_source_for_rocks(state)
        crater_json = Path(source_info["crater_json"])
        metadata = Path(source_info["metadata"])
        base_name = str(source_info["base_name"])

        run_name, run_dir, generator_out_dir = make_rock_run_folder(state)
        settings_json = make_rock_settings_json(state, run_dir, run_name)

        append_log("\nRock generation started.\n")
        append_log(f"Source heightmap run:\n  {source_info['run_dir']}\n")
        append_log(f"Using crater catalog:\n  {crater_json}\n")
        append_log(f"Using metadata:\n  {metadata}\n")
        append_log(f"Rockfield run package:\n  {run_dir}\n")
        status_var.set("Generating rocks...")

        if not ROCKFIELD_SCRIPT.exists():
            raise FileNotFoundError(f"Missing rockfield script: {ROCKFIELD_SCRIPT}")

        rock_cmd = [
            sys.executable,
            ROCKFIELD_SCRIPT,
            "--crater-json", crater_json,
            "--out-dir", generator_out_dir,
            "--settings-json", settings_json,
            "--profile", state["rock_profile"],
            "--no-scientific-presets",
            "--seed", int(state["seed"]),
            "--map-size-m", float(state["map_size_m"]),
            "--coords-centered",
            "--metadata", metadata,
        ]

        rock_start_time = time.time()
        run_command(
            rock_cmd,
            HEIGHTMAP_DIR,
            "rockfield generator",
            output_filter=filter_rockfield_summary,
        )

        converted = find_unreal_rockfield_json(
            generator_out_dir,
            base_name,
            newer_than=rock_start_time - 2.0,
        )

        # Compatibility fallback for older generators that wrote only the
        # position/CSV files and required conversion to a compact JSON.
        if converted is None:
            legacy_expected = (
                generator_out_dir / f"{base_name}_unreal_rockfield.json"
            )
            converted = convert_minimal_unreal_json_if_needed(
                legacy_expected,
                base_name,
                crater_json,
            )

        if not converted or not converted.exists():
            raise FileNotFoundError(
                "Rock generation completed, but no rockfield JSON was found or converted."
            )

        packaged = finalize_rockfield_run(
            run_name,
            run_dir,
            generator_out_dir,
            converted,
            source_info,
        )

        append_log("Compact rockfield files:\n")
        append_log(f"  {packaged['unreal_rockfield']}\n")
        append_log(f"  {run_dir / 'rock_settings.json'}\n")
        append_log(f"  {run_dir / 'source_heightmap.json'}\n")
        append_log("Unreal rockfield import:\n")
        append_log(f"  {packaged['unreal_import']}\n")
        append_log("\nRock generation finished successfully.\n")
        status_var.set(
            "Rock generation finished. The compact run and Unreal import JSON are ready."
        )

    except Exception as exc:
        append_log(f"\nERROR: {exc}\n")
        status_var.set("Rock generation failed.")
        messagebox.showerror("Rock generation failed", str(exc))
    finally:
        if generator_out_dir is not None:
            try:
                shutil.rmtree(generator_out_dir, ignore_errors=True)
            except OSError:
                pass
        set_buttons_busy(False)

def collect_current_state():
    global active_preset
    save_active_state()
    state = copy.deepcopy(preset_states[active_preset])
    state["terrain_label"] = active_preset
    state["existing_heightmap_run"] = existing_heightmap_run_var.get().strip()
    state["existing_crater_json"] = existing_crater_json_var.get().strip()
    state["existing_metadata_json"] = existing_metadata_json_var.get().strip()
    return state

def generate_heightmap_only():
    try:
        state = collect_current_state()
        set_buttons_busy(True)
        threading.Thread(target=heightmap_generation_thread, args=(state,), daemon=True).start()
    except Exception as exc:
        messagebox.showerror("Invalid value", str(exc))

def generate_rocks_only():
    try:
        state = collect_current_state()
        set_buttons_busy(True)
        threading.Thread(target=rock_generation_thread, args=(state,), daemon=True).start()
    except Exception as exc:
        messagebox.showerror("Invalid value", str(exc))

buttons.columnconfigure(2, weight=1)

analyze_heightmap_button = ttk.Button(
    buttons,
    text="Analyze heightmap",
    command=open_heightmap_analysis_dialog,
    style="Secondary.TButton",
)
analyze_heightmap_button.grid(
    row=0,
    column=0,
    sticky="w",
    padx=(0, 8),
)

analyze_rockfield_button = ttk.Button(
    buttons,
    text="Analyze rockfield",
    command=open_rockfield_analysis_dialog,
    style="Secondary.TButton",
)
analyze_rockfield_button.grid(
    row=0,
    column=1,
    sticky="w",
)

ttk.Button(
    buttons,
    text="Clear log",
    command=clear_log,
    style="Secondary.TButton",
).grid(
    row=0,
    column=3,
    sticky="e",
    padx=(0, 8),
)

reset_button = ttk.Button(
    buttons,
    text="Reset current preset defaults",
    command=reset_current_defaults,
    style="Secondary.TButton",
)
reset_button.grid(
    row=0,
    column=4,
    sticky="e",
    padx=(0, 8),
)

heightmap_button = ttk.Button(
    buttons,
    text="Generate heightmap",
    command=generate_heightmap_only,
    style="Primary.TButton",
)
heightmap_button.grid(
    row=0,
    column=5,
    sticky="e",
    padx=(0, 8),
)

rock_button = ttk.Button(
    buttons,
    text="Generate rocks",
    command=generate_rocks_only,
    style="Primary.TButton",
)
rock_button.grid(
    row=0,
    column=6,
    sticky="e",
    padx=(0, 8),
)

close_button = ttk.Button(
    buttons,
    text="Close",
    command=root.destroy,
    style="Secondary.TButton",
)
close_button.grid(
    row=0,
    column=7,
    sticky="e",
)

load_state_to_widgets(active_preset)

latest_heightmap_run = find_latest_heightmap_run()
if latest_heightmap_run is not None:
    existing_heightmap_run_var.set(str(latest_heightmap_run["run_dir"]))
    existing_crater_json_var.set(str(latest_heightmap_run["crater_json"]))
    existing_metadata_json_var.set(str(latest_heightmap_run["metadata"]))

root.mainloop()
PY