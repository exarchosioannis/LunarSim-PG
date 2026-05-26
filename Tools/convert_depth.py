#!/usr/bin/env python3
"""
Convert UnrealGT encoded depth images to grayscale depth previews.

Usage:
    python3 Tools/convert_depth.py /path/to/Session_001

Example:
    python3 Tools/convert_depth.py /home/exarchos/Projects/UnrealProjects/simulator_test5.7/Saved/Datasets/2026-05-26_15-22-43/Session_001

Input:
    Session_001/Images/Depth/*.png

Output:
    Session_001/Images/Depth_Greyscale/*.png

UnrealGT depth encoding:
    depth_mm = R + G * 256 + B * 256 * 256
"""

import argparse
from pathlib import Path

import numpy as np
from PIL import Image


def natural_sort_key(path: Path):
    # Sorts 1.png, 2.png, 10.png correctly
    return [int(part) if part.isdigit() else part.lower() for part in path.stem.split("_")]


def decode_unrealgt_depth(depth_png_path: Path) -> np.ndarray:
    img = Image.open(depth_png_path).convert("RGB")
    arr = np.asarray(img, dtype=np.uint32)

    r = arr[:, :, 0]
    g = arr[:, :, 1]
    b = arr[:, :, 2]

    depth_mm = r + g * 256 + b * 256 * 256
    return depth_mm.astype(np.float32)


def depth_to_grayscale(depth_mm: np.ndarray, max_depth_m: float | None = None) -> np.ndarray:
    valid = depth_mm > 0

    if not np.any(valid):
        return np.zeros(depth_mm.shape, dtype=np.uint8)

    if max_depth_m is None:
        # Percentile avoids one very far pixel making the whole image too dark.
        max_depth_mm = np.percentile(depth_mm[valid], 99)
    else:
        max_depth_mm = max_depth_m * 1000.0

    if max_depth_mm <= 0:
        max_depth_mm = float(depth_mm[valid].max())

    normalized = np.clip(depth_mm / max_depth_mm, 0.0, 1.0)

    # Near = bright, far = dark. Easier to inspect visually.
    grayscale = (255.0 * (1.0 - normalized)).astype(np.uint8)

    # Keep invalid/no-depth pixels black.
    grayscale[~valid] = 0

    return grayscale


def main():
    parser = argparse.ArgumentParser(
        description="Convert UnrealGT RGB-encoded depth PNGs to grayscale preview images."
    )
    parser.add_argument(
        "session_path",
        type=str,
        help="Path to the dataset session folder, e.g. .../Session_001"
    )
    parser.add_argument(
        "--max-depth-m",
        type=float,
        default=None,
        help="Optional fixed max depth in meters. Example: --max-depth-m 100"
    )
    parser.add_argument(
        "--overwrite",
        action="store_true",
        help="Overwrite existing grayscale images."
    )

    args = parser.parse_args()

    session_path = Path(args.session_path).expanduser().resolve()
    depth_dir = session_path / "Images" / "Depth"
    output_dir = session_path / "Images" / "Depth_Greyscale"

    if not session_path.exists():
        print(f"[ERROR] Session path does not exist: {session_path}")
        return

    if not depth_dir.exists():
        print(f"[ERROR] Depth folder does not exist: {depth_dir}")
        print("Expected structure: Session_001/Images/Depth/*.png")
        return

    depth_images = sorted(depth_dir.glob("*.png"), key=natural_sort_key)

    if not depth_images:
        print(f"[ERROR] No PNG depth images found in: {depth_dir}")
        return

    output_dir.mkdir(parents=True, exist_ok=True)

    total = len(depth_images)
    converted = 0
    skipped = 0

    print(f"[INFO] Session: {session_path}")
    print(f"[INFO] Input depth folder: {depth_dir}")
    print(f"[INFO] Output folder: {output_dir}")
    print(f"[INFO] Found {total} depth images.")
    print()

    for i, depth_path in enumerate(depth_images, start=1):
        out_path = output_dir / depth_path.name

        if out_path.exists() and not args.overwrite:
            skipped += 1
            remaining = total - i
            print(f"[{i}/{total}] Skipped existing: {out_path.name} | left: {remaining}")
            continue

        try:
            depth_mm = decode_unrealgt_depth(depth_path)
            grayscale = depth_to_grayscale(depth_mm, max_depth_m=args.max_depth_m)

            Image.fromarray(grayscale, mode="L").save(out_path)

            converted += 1
            remaining = total - i

            valid = depth_mm[depth_mm > 0]
            if valid.size > 0:
                min_m = valid.min() / 1000.0
                max_m = valid.max() / 1000.0
                print(
                    f"[{i}/{total}] Converted {depth_path.name} -> {out_path.name} "
                    f"| depth: {min_m:.2f}m - {max_m:.2f}m | left: {remaining}"
                )
            else:
                print(
                    f"[{i}/{total}] Converted {depth_path.name} -> {out_path.name} "
                    f"| no valid depth | left: {remaining}"
                )

        except Exception as e:
            remaining = total - i
            print(f"[{i}/{total}] ERROR converting {depth_path.name}: {e} | left: {remaining}")

    print()
    print("[DONE]")
    print(f"Converted: {converted}")
    print(f"Skipped:   {skipped}")
    print(f"Total:     {total}")
    print(f"Output:    {output_dir}")


if __name__ == "__main__":
    main()