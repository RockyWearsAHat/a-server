#!/usr/bin/env python3
"""Deep analysis of OG-DK copyright symbol and M character tile data."""

import sys

def main():
    ppm_path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/ogdk_current.ppm"
    
    with open(ppm_path, "rb") as f:
        magic = f.readline()
        dims = f.readline().split()
        w, h = int(dims[0]), int(dims[1])
        maxval = f.readline()
        data = f.read()

    def get_pixel(x, y):
        off = (y * w + x) * 3
        return data[off], data[off+1], data[off+2]

    # Let's zoom in on the copyright symbol at y=135-140
    # Previous output shows y=140 has: ..####.. then a lone # at x=231
    print("=== Copyright symbol area zoomed (y=134-141, x=30-46) ===")
    for y in range(134, 142):
        row = ""
        for x in range(30, 50):
            r, g, b = get_pixel(x, y)
            if r == 0 and g == 0 and b == 0:
                row += "."
            else:
                row += "#"
        print(f"y={y}: {row}")
    
    # Now let's zoom in on the M character in "MARIO" at the copyright section
    # y=147-151 has the text "MARIO IN JAPAN" 
    # Look for M at the start
    print()
    print("=== 'MARIO' text at y=147-151, x=76-130 ===")
    for y in range(146, 153):
        row = ""
        for x in range(76, 130):
            r, g, b = get_pixel(x, y)
            if r == 0 and g == 0 and b == 0:
                row += "."
            else:
                row += "#"
        print(f"y={y}: {row}")

    # Now zoom in on the first M in the menu (GAME BOY / PLAYER)
    # From the output, the menu items are at y=87-91 and y=99-103
    # y=87 line starts the text, looking for M specifically
    print()
    print("=== Menu M character zoom (y=86-93, x=54-70) ===")
    for y in range(86, 93):
        row = ""
        for x in range(54, 70):
            r, g, b = get_pixel(x, y)
            if r == 0 and g == 0 and b == 0:
                row += "."
            else:
                row += "#"
        print(f"y={y}: {row}")

    # Let's find all the M characters in the frame
    # An M in 5x8 NES font: ##...## / #######  / ##.#.## / ##...##
    # The user says there's an extra pixel in the middle of all Ms
    # NES M should be: ##...## / ###.### / ##.#.## / ##...## (no fill in row 2)
    
    # Look at all the text rows to find M patterns
    print()
    print("=== All M characters at y=87 line (x=50-240) ===")
    for y in range(86, 92):
        row = ""
        for x in range(50, 240):
            r, g, b = get_pixel(x, y)
            if r == 0 and g == 0 and b == 0:
                row += "."
            else:
                row += "#"
        print(f"y={y}: {row.rstrip('.')}")

    # y=90 should show M characters - let's see if ##.#.## has issue
    print()
    print("=== Row y=90 zoomed on M at menu area ===")
    # The M in line y=90 at x around 140-158 based on output
    for y in range(86, 93):
        row = ""
        for x in range(140, 170):
            r, g, b = get_pixel(x, y)
            if r == 0 and g == 0 and b == 0:
                row += "."
            else:
                row += "#"
        print(f"y={y}: {row}")
    
    # Verify the exact copyright text section from the output
    # y=135-139 looks like it has the full "©1981 NINTENDO CO.,LTD." text
    print()
    print("=== Copyright text detail y=135-140, x=24-240 ===")
    for y in range(134, 142):
        row = ""
        for x in range(24, 240):
            r, g, b = get_pixel(x, y)
            if r == 0 and g == 0 and b == 0:
                row += "."
            else:
                row += "#"
        if "#" in row:
            print(f"y={y}: {row.rstrip('.')}")

if __name__ == "__main__":
    main()
