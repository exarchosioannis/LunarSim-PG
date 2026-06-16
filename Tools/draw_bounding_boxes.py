#!/usr/bin/env python3
"""
Draw UnrealGT bounding boxes on RGB images.

Usage:
    python3 Tools/draw_bounding_boxes.py /path/to/Session_001

Example:
    python3 Tools/draw_bounding_boxes.py /home/exarchos/Projects/UnrealProjects/simulator_test5.7/Saved/Datasets/2026-05-26_15-22-43/Session_001

Input:
    Session_001/Images/RGB/*.png
    Session_001/Images/BoundingBoxes/*.csv

Output:
    Session_001/Images/RGB_BoundingBoxes/*.png

Expected CSV format:
    actor_name,mesh_name,min_x,min_y,max_x,max_y,center_x,center_y,extent_x,extent_y,width,height
"""

import argparse
import csv
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


def natural_sort_key(path: Path):
    # Sorts 1.png, 2.png, 10.png correctly
    return [int(part) if part.isdigit() else part.lower() for part in path.stem.split("_")]


def safe_float(value, default=0.0):
    try:
        return float(value)
    except Exception:
        return default


def find_bbox_csv_for_image(image_path: Path, bbox_dir: Path) -> Path | None:
    """
    For image 5.png, expects BoundingBoxes/5.csv.
    """
    csv_path = bbox_dir / f"{image_path.stem}.csv"
    if csv_path.exists():
        return csv_path
    return None


def load_boxes(csv_path: Path, image_width: int, image_height: int, min_box_size: float = 1.0):
    boxes = []

    with csv_path.open("r", newline="") as f:
        reader = csv.DictReader(f)

        required_columns = {"min_x", "min_y", "max_x", "max_y"}
        if not required_columns.issubset(set(reader.fieldnames or [])):
            raise ValueError(
                f"CSV is missing required columns {required_columns}. "
                f"Found columns: {reader.fieldnames}"
            )

        for row in reader:
            actor_name = row.get("actor_name", "")
            mesh_name = row.get("mesh_name", "")

            min_x = safe_float(row.get("min_x"))
            min_y = safe_float(row.get("min_y"))
            max_x = safe_float(row.get("max_x"))
            max_y = safe_float(row.get("max_y"))

            # Clamp to image boundaries.
            min_x = max(0.0, min(float(image_width - 1), min_x))
            max_x = max(0.0, min(float(image_width - 1), max_x))
            min_y = max(0.0, min(float(image_height - 1), min_y))
            max_y = max(0.0, min(float(image_height - 1), max_y))

            # Fix accidentally inverted boxes.
            if max_x < min_x:
                min_x, max_x = max_x, min_x
            if max_y < min_y:
                min_y, max_y = max_y, min_y

            box_width = max_x - min_x
            box_height = max_y - min_y

            if box_width < min_box_size or box_height < min_box_size:
                continue

            label = "rock"

            boxes.append(
                {
                    "label": label,
                    "actor_name": actor_name,
                    "mesh_name": mesh_name,
                    "min_x": min_x,
                    "min_y": min_y,
                    "max_x": max_x,
                    "max_y": max_y,
                }
            )

    return boxes


def draw_boxes(image_path: Path, boxes, output_path: Path, line_width: int = 3, show_labels: bool = True):
    image = Image.open(image_path).convert("RGB")
    draw = ImageDraw.Draw(image)

    try:
        font = ImageFont.truetype("DejaVuSans.ttf", 16)
    except Exception:
        font = ImageFont.load_default()

    for box in boxes:
        min_x = box["min_x"]
        min_y = box["min_y"]
        max_x = box["max_x"]
        max_y = box["max_y"]
        label = box["label"]

        # Red GT boxes.
        color = (255, 0, 0)

        for i in range(line_width):
            draw.rectangle(
                [min_x - i, min_y - i, max_x + i, max_y + i],
                outline=color
            )

        if show_labels:
            text = label

            # Pillow compatibility for text size.
            try:
                bbox = draw.textbbox((0, 0), text, font=font)
                text_width = bbox[2] - bbox[0]
                text_height = bbox[3] - bbox[1]
            except Exception:
                text_width, text_height = draw.textsize(text, font=font)

            text_x = min_x
            text_y = max(0, min_y - text_height - 4)

            draw.rectangle(
                [text_x, text_y, text_x + text_width + 6, text_y + text_height + 4],
                fill=(255, 0, 0)
            )
            draw.text(
                (text_x + 3, text_y + 2),
                text,
                fill=(0, 0, 0),
                font=font
            )

    output_path.parent.mkdir(parents=True, exist_ok=True)
    image.save(output_path)


def main():
    parser = argparse.ArgumentParser(
        description="Draw UnrealGT bounding boxes on RGB images."
    )
    parser.add_argument(
        "session_path",
        type=str,
        help="Path to the dataset session folder, e.g. .../Session_001"
    )
    parser.add_argument(
        "--overwrite",
        action="store_true",
        help="Overwrite existing output images."
    )
    parser.add_argument(
        "--no-labels",
        action="store_true",
        help="Draw boxes without text labels."
    )
    parser.add_argument(
        "--min-box-size",
        type=float,
        default=1.0,
        help="Ignore boxes smaller than this size in pixels. Default: 1.0"
    )
    parser.add_argument(
        "--line-width",
        type=int,
        default=3,
        help="Bounding box line width. Default: 3"
    )

    args = parser.parse_args()

    session_path = Path(args.session_path).expanduser().resolve()
    rgb_dir = session_path / "Images" / "RGB"
    bbox_dir = session_path / "Images" / "BoundingBoxes"
    output_dir = session_path / "Images" / "RGB_BoundingBoxes"

    if not session_path.exists():
        print(f"[ERROR] Session path does not exist: {session_path}")
        return

    if not rgb_dir.exists():
        print(f"[ERROR] RGB folder does not exist: {rgb_dir}")
        print("Expected structure: Session_001/Images/RGB/*.png")
        return

    if not bbox_dir.exists():
        print(f"[ERROR] BoundingBoxes folder does not exist: {bbox_dir}")
        print("Expected structure: Session_001/Images/BoundingBoxes/*.csv")
        return

    rgb_images = sorted(rgb_dir.glob("*.png"), key=natural_sort_key)

    if not rgb_images:
        print(f"[ERROR] No RGB PNG images found in: {rgb_dir}")
        return

    output_dir.mkdir(parents=True, exist_ok=True)

    total = len(rgb_images)
    drawn = 0
    skipped = 0
    missing_csv = 0
    total_boxes = 0

    print(f"[INFO] Session: {session_path}")
    print(f"[INFO] Input RGB folder: {rgb_dir}")
    print(f"[INFO] Input boxes folder: {bbox_dir}")
    print(f"[INFO] Output folder: {output_dir}")
    print(f"[INFO] Found {total} RGB images.")
    print()

    for i, image_path in enumerate(rgb_images, start=1):
        out_path = output_dir / image_path.name

        if out_path.exists() and not args.overwrite:
            skipped += 1
            remaining = total - i
            print(f"[{i}/{total}] Skipped existing: {out_path.name} | left: {remaining}")
            continue

        csv_path = find_bbox_csv_for_image(image_path, bbox_dir)
        if csv_path is None:
            missing_csv += 1
            remaining = total - i
            print(f"[{i}/{total}] Missing bbox CSV for {image_path.name} | left: {remaining}")
            continue

        try:
            with Image.open(image_path) as img:
                image_width, image_height = img.size

            boxes = load_boxes(
                csv_path,
                image_width=image_width,
                image_height=image_height,
                min_box_size=args.min_box_size
            )

            draw_boxes(
                image_path,
                boxes,
                out_path,
                line_width=max(1, args.line_width),
                show_labels=not args.no_labels
            )

            drawn += 1
            total_boxes += len(boxes)
            remaining = total - i

            print(
                f"[{i}/{total}] Drew {len(boxes)} boxes: "
                f"{image_path.name} + {csv_path.name} -> {out_path.name} | left: {remaining}"
            )

        except Exception as e:
            remaining = total - i
            print(f"[{i}/{total}] ERROR processing {image_path.name}: {e} | left: {remaining}")

    print()
    print("[DONE]")
    print(f"Images processed: {drawn}")
    print(f"Skipped:          {skipped}")
    print(f"Missing CSVs:     {missing_csv}")
    print(f"Total RGB images: {total}")
    print(f"Total boxes:      {total_boxes}")
    print(f"Output:           {output_dir}")


if __name__ == "__main__":
    main()