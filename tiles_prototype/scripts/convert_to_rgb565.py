import struct
import os

def convert_to_rgb565(png_path, rgb565_path):
    """
    Converts a PNG tile to RGB565 format with LVGL 9 header.
    Requires GDAL for reading the input image.
    """
    try:
        from osgeo import gdal
    except ImportError:
        print("Error: GDAL (python3-gdal) is required for this script.")
        return False

    ds = gdal.Open(png_path)
    if not ds:
        return False

    width = ds.RasterXSize
    height = ds.RasterYSize
    bands = ds.RasterCount

    r_band = ds.GetRasterBand(1).ReadRaster()
    g_band = ds.GetRasterBand(2).ReadRaster()
    b_band = ds.GetRasterBand(3).ReadRaster()
    a_band = None
    if bands == 4:
        a_band = ds.GetRasterBand(4).ReadRaster()

    ds = None # Close dataset

    # 1. Create the LVGL 9 Header (12 bytes)
    # Format: <BBHHHhh (Magic: 1B, Format: 1B, Flags: 2B, Width: 2B, Height: 2B, Stride: 2B, Reserved: 2B)
    # Magic: 0x19, Color Format: 0x12 (RGB565), Stride: width * 2
    lv_header = struct.pack('<BBHHHhh',
                            0x19,       # Magic
                            0x12,       # Color format RGB565
                            0,          # Flags
                            width,      # Width
                            height,     # Height
                            width * 2,  # Stride in bytes
                            0)          # Reserved

    # 2. Prepare the pixel data (Little-Endian)
    pixel_data = bytearray(width * height * 2)

    for i in range(width * height):
        r = r_band[i]
        g = g_band[i]
        b = b_band[i]

        if a_band is not None:
            alpha = a_band[i] / 255.0
            r = int(r * alpha + 255 * (1 - alpha))
            g = int(g * alpha + 255 * (1 - alpha))
            b = int(b * alpha + 255 * (1 - alpha))

        # Pack into RGB565 (5 bits Red, 6 bits Green, 5 bits Blue)
        # Big-Endian: [RRRRRGGG] [GGGBBBBB] -> 0xRRGGBB
        # Little-Endian: [GGGBBBBB] [RRRRRGGG] (for CONFIG_LV_COLOR_16_SWAP=n)
        val = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

        # Write as Little-Endian
        pixel_data[i*2] = val & 0xFF          # LSB
        pixel_data[i*2 + 1] = (val >> 8) & 0xFF # MSB

    # 3. Write Header + Pixels
    with open(rgb565_path, 'wb') as f:
        f.write(lv_header)
        f.write(pixel_data)

    print(f"Successfully converted {png_path} to {rgb565_path}")
    return True

if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="Convert PNG/JPEG to LVGL 9 RGB565 format.")
    parser.add_argument("input", help="Input image file")
    parser.add_argument("output", help="Output .rgb565 file")
    args = parser.parse_args()
    convert_to_rgb565(args.input, args.output)
