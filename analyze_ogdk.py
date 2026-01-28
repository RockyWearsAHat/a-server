#!/usr/bin/env python3
"""Analyze OG-DK frame capture"""

with open('ogdk_frame120.ppm', 'rb') as f:
    f.readline()  # P6
    f.readline()  # dimensions
    f.readline()  # max value
    data = f.read()

# Check if it looks like NES Donkey Kong
# The Classic NES title screen should show:
# - "DONKEY KONG" title
# - Nintendo copyright
# - "1 PLAYER GAME A" menu options

# Let's look for text-like patterns in specific regions
print("Frame Analysis for Classic NES Series Donkey Kong")
print("=" * 50)

# Extract unique colors
colors = set()
for i in range(0, len(data), 3):
    colors.add((data[i], data[i+1], data[i+2]))

print(f"\nUnique colors ({len(colors)}):")
for r, g, b in sorted(colors):
    if r == g == b == 0:
        print(f"  ({r}, {g}, {b}) - black")
    elif r == g == b:
        print(f"  ({r}, {g}, {b}) - gray")
    elif r == 128 and g == 248 and b == 248:
        print(f"  ({r}, {g}, {b}) - cyan (Classic NES typical)")
    elif r == 248 and g == 0 and b == 0:
        print(f"  ({r}, {g}, {b}) - red")
    elif r == 248 and g == 248 and b == 0:
        print(f"  ({r}, {g}, {b}) - yellow")
    elif r == 248 and g == 248 and b == 248:
        print(f"  ({r}, {g}, {b}) - white")
    else:
        print(f"  ({r}, {g}, {b})")

# Check horizontal scroll register effect (Classic NES uses scroll for effects)
print("\n\nChecking if image looks like a static frame or animated...")

# Count pixels by color
color_counts = {}
for i in range(0, len(data), 3):
    c = (data[i], data[i+1], data[i+2])
    color_counts[c] = color_counts.get(c, 0) + 1

total = sum(color_counts.values())
print("\nColor distribution:")
for c, count in sorted(color_counts.items(), key=lambda x: -x[1]):
    pct = 100.0 * count / total
    print(f"  {c}: {count:6d} pixels ({pct:.1f}%)")

# Check if cyan forms recognizable NES structures
# In Classic NES DK, cyan is used for the steel girders
print("\n\nLooking for girder-like horizontal structures...")
cyan = (128, 248, 248)
for y in range(0, 160, 8):  # Check every 8 lines (tile boundary)
    cyan_count = 0
    for x in range(240):
        idx = (y * 240 + x) * 3
        if (data[idx], data[idx+1], data[idx+2]) == cyan:
            cyan_count += 1
    if cyan_count > 100:  # Significant cyan
        print(f"  Row {y}: {cyan_count} cyan pixels")

# The scrambled look might be due to wrong tile/palette indexing
# Let's check if we're seeing a known pattern
print("\n\nFirst 64 bytes of top-left 8x8 tile:")
for row in range(8):
    pixels = []
    for col in range(8):
        idx = (row * 240 + col) * 3
        r, g, b = data[idx], data[idx+1], data[idx+2]
        if r == g == b == 0:
            pixels.append('.')
        elif (r, g, b) == cyan:
            pixels.append('C')
        elif r == 248 and g == 248 and b == 248:
            pixels.append('W')
        else:
            pixels.append('?')
    print(f"  {''.join(pixels)}")
