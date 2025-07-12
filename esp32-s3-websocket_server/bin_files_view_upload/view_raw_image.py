#code downloaded from the net, and combined with AI. Now parses filename for dimensions.

import os
import argparse
from PIL import Image
import numpy as np

def view_image_from_filename(file_path):
    """
    Reads a local raw RGB565 image file, parsing dimensions
    from its filename (e.g., "112x112_12345.bin").

    Args:
        file_path (str): The local path to the .bin file.
    """
    try:
        # if the file exists
        if not os.path.exists(file_path):
            print(f"❌ Error: File not found at '{file_path}'")
            return

        # --- NEW: Parse width and height from the filename ---
        basename = os.path.basename(file_path)
        parts = basename.split('_')[0].split('x')
        if len(parts) != 2:
            raise ValueError("Filename is not in the expected 'widthxheight_...' format.")
        
        width, height = int(parts[0]), int(parts[1])
        print(f"-> Parsed dimensions from filename: {width}x{height}")

        # read the binary data from local file
        print(f"-> Reading local file: '{file_path}'...")
        with open(file_path, 'rb') as f:
            raw_data = f.read()
        print(f"   ...Success! Read {len(raw_data)} bytes.")

        # Convert RGB565 bytes to a viewable RGB image
        # The ESP32-CAM RGB565 is big-endian, so, swap bytes.
        dt = np.dtype(np.uint16)
        dt = dt.newbyteorder('>')  # Specify big-endian byte order
        rgb565_data = np.frombuffer(raw_data, dtype=dt)

        # Create a RGB image array
        image_array = np.zeros((height, width, 3), dtype=np.uint8)

        # 16-bit RGB565 data to 24-bit RGB channels
        r = (rgb565_data & 0xF800) >> 8
        g = (rgb565_data & 0x07E0) >> 3
        b = (rgb565_data & 0x001F) << 3

        image_array[:, :, 0] = r.reshape((height, width))
        image_array[:, :, 1] = g.reshape((height, width))
        image_array[:, :, 2] = b.reshape((height, width))

        # Create image from the array and display it
        print("-> Reconstructing image from raw RGB565 data...")
        img = Image.fromarray(image_array, 'RGB')

        print("✅ All done! Displaying the image.")
        img.show()

        # if needed, save the image as PNG or JPG
        save_path = file_path.replace('.bin', '.png')
        img.save(save_path)
        print(f"Image also saved as '{save_path}'")

    except (ValueError, IndexError) as e:
        print(f"❌ Error parsing filename: {e}")
        print("   Please ensure the filename is formatted like '112x112_1677813633.bin'")
    except Exception as e:
        print(f"❌ An unexpected error occurred: {e}")

if __name__ == '__main__':
    # --- MODIFIED: Only needs the path now ---
    parser = argparse.ArgumentParser(description="View local raw RGB565 image by parsing dimensions from its filename.")
    parser.add_argument("--path", required=True, help="Path to the local .bin image file (e.g., '112x112_12345.bin').")
    
    args = parser.parse_args()
    view_image_from_filename(args.path)