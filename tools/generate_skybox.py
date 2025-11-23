#!/usr/bin/env python3
"""
Generate a simple procedural Milky Way skybox texture
This creates a basic starfield as a fallback when high-quality textures aren't available
"""

import numpy as np
from PIL import Image
import random

def create_starfield(width=4096, height=2048, num_stars=10000):
    """
    Create an equirectangular starfield texture
    
    Args:
        width: Texture width (should be 2x height for equirectangular)
        height: Texture height
        num_stars: Number of stars to generate
    """
    print(f"Generating {width}x{height} starfield with {num_stars} stars...")
    
    # Create black background
    img = np.zeros((height, width, 3), dtype=np.uint8)
    
    # Add Milky Way band (brighter region across the middle)
    print("Adding Milky Way band...")
    for y in range(height):
        # Brightness based on distance from galactic plane
        dist_from_plane = abs(y - height/2) / (height/2)
        brightness = max(0, 1.0 - dist_from_plane**2) * 30
        
        for x in range(width):
            # Add some variation across the band
            variation = np.sin(x / width * np.pi * 4) * 10
            value = int(brightness + variation)
            if value > 0:
                img[y, x] = [value, value, int(value * 1.1)]  # Slightly bluish
    
    # Add random stars
    print("Adding stars...")
    for i in range(num_stars):
        if i % 1000 == 0:
            print(f"  {i}/{num_stars} stars...")
        
        x = random.randint(0, width - 1)
        y = random.randint(0, height - 1)
        
        # Star brightness (magnitude-based)
        magnitude = random.gauss(8, 2)  # Average magnitude 8
        brightness = max(0, min(255, int(255 * (10 - magnitude) / 10)))
        
        if brightness < 50:
            continue
        
        # Star color (slightly varied)
        color_temp = random.gauss(1.0, 0.3)
        if color_temp > 1.2:  # Blue stars
            color = [int(brightness * 0.7), int(brightness * 0.8), brightness]
        elif color_temp < 0.8:  # Red stars
            color = [brightness, int(brightness * 0.7), int(brightness * 0.5)]
        else:  # White/yellow stars
            color = [brightness, brightness, int(brightness * 0.9)]
        
        # Draw star (with slight glow for bright stars)
        img[y, x] = color
        
        if brightness > 200:  # Bright stars get a glow
            for dy in range(-1, 2):
                for dx in range(-1, 2):
                    ny, nx = y + dy, x + dx
                    if 0 <= ny < height and 0 <= nx < width:
                        glow = [c // 2 for c in color]
                        img[ny, nx] = np.maximum(img[ny, nx], glow)
    
    print("Creating image...")
    return Image.fromarray(img, 'RGB')

def main():
    import sys
    import os
    
    # Check if PIL is available
    try:
        from PIL import Image
    except ImportError:
        print("Error: Pillow (PIL) is required. Install with: pip3 install Pillow")
        sys.exit(1)
    
    output_dir = "data/textures/skybox"
    output_file = os.path.join(output_dir, "milkyway_procedural_4k.jpg")
    
    print("=" * 60)
    print("Procedural Milky Way Generator")
    print("=" * 60)
    print()
    
    # Create output directory if it doesn't exist
    os.makedirs(output_dir, exist_ok=True)
    
    # Generate starfield
    img = create_starfield(width=4096, height=2048, num_stars=15000)
    
    # Save
    print(f"Saving to {output_file}...")
    img.save(output_file, 'JPEG', quality=95)
    
    print()
    print("✓ Done!")
    print(f"Generated: {output_file}")
    print(f"Size: {os.path.getsize(output_file) / 1024 / 1024:.1f} MB")
    print()
    print("Note: This is a basic procedural texture.")
    print("For better quality, download real Milky Way textures:")
    print("  • ESA Gaia: https://www.cosmos.esa.int/web/gaia/dr2")
    print("  • Celestia: https://celestiamotherlode.net/")
    print()

if __name__ == "__main__":
    main()
