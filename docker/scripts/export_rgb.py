#!/usr/bin/env python3
import sys
from pathlib import Path

import numpy as np
from PIL import Image

import rclpy
from rclpy.serialization import deserialize_message

from rosbag2_py import SequentialReader, StorageOptions, ConverterOptions
from rosidl_runtime_py.utilities import get_message


IMAGE_TOPIC = "/camera/rgb/image_raw"


def image_msg_to_pil(msg):
    h = int(msg.height)
    w = int(msg.width)
    enc = msg.encoding.lower()
    data = bytes(msg.data)

    if enc == "bgra8":
        arr = np.frombuffer(data, dtype=np.uint8).reshape((h, w, 4))
        arr = arr[:, :, [2, 1, 0, 3]]  # BGRA -> RGBA
        return Image.fromarray(arr, "RGBA")

    elif enc == "rgba8":
        arr = np.frombuffer(data, dtype=np.uint8).reshape((h, w, 4))
        return Image.fromarray(arr, "RGBA")

    elif enc == "bgr8":
        arr = np.frombuffer(data, dtype=np.uint8).reshape((h, w, 3))
        arr = arr[:, :, [2, 1, 0]]  # BGR -> RGB
        return Image.fromarray(arr, "RGB")

    elif enc == "rgb8":
        arr = np.frombuffer(data, dtype=np.uint8).reshape((h, w, 3))
        return Image.fromarray(arr, "RGB")

    else:
        raise RuntimeError(f"Unsupported image encoding: {msg.encoding}")


def main():
    if len(sys.argv) < 2:
        print("Usage: export_rgb.py <bag_folder>")
        sys.exit(1)

    bag_folder = Path(sys.argv[1]).expanduser().resolve()
    out_dir = bag_folder / "ros_rgb"
    out_dir.mkdir(parents=True, exist_ok=True)

    rclpy.init(args=None)

    reader = SequentialReader()
    reader.open(
        StorageOptions(uri=str(bag_folder), storage_id="sqlite3"),
        ConverterOptions(input_serialization_format="cdr", output_serialization_format="cdr"),
    )

    topic_types = {t.name: t.type for t in reader.get_all_topics_and_types()}
    if IMAGE_TOPIC not in topic_types:
        print("Available topics:")
        for k, v in topic_types.items():
            print(f"  {k} : {v}")
        print(f"\nImage topic not found: {IMAGE_TOPIC}")
        sys.exit(1)

    image_msg_type = get_message(topic_types[IMAGE_TOPIC])

    count = 0
    while reader.has_next():
        tname, data, _t_rosbag_ns = reader.read_next()

        if tname != IMAGE_TOPIC:
            continue

        msg = deserialize_message(data, image_msg_type)
        img = image_msg_to_pil(msg)

        out_path = out_dir / f"{count:06d}.png"
        img.save(out_path)

        count += 1

    print(f"Extracted {count} images -> {out_dir}")

    rclpy.shutdown()


if __name__ == "__main__":
    main()
