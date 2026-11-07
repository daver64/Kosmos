#!/bin/bash
# Script to download planet textures from Solar System Scope
# These are free 2K resolution textures suitable for educational use

echo "Downloading planet textures..."
echo "These textures are from Solar System Scope (https://www.solarsystemscope.com/textures/)"
echo ""

# Create directory if it doesn't exist
mkdir -p "$(dirname "$0")"
cd "$(dirname "$0")"

# Function to download with progress
download() {
    local url=$1
    local output=$2
    echo "Downloading $output..."
    wget --progress=bar:force -O "$output" "$url" 2>&1 | tail -f -n +6
}

# Download 2K textures (smaller, faster downloads)
# For higher quality, replace '2k' with '4k' or '8k' in the URLs

download "https://www.solarsystemscope.com/textures/download/2k_sun.jpg" "sun.jpg"
download "https://www.solarsystemscope.com/textures/download/2k_mercury.jpg" "mercury.jpg"
download "https://www.solarsystemscope.com/textures/download/2k_venus_surface.jpg" "venus.jpg"
download "https://www.solarsystemscope.com/textures/download/2k_earth_daymap.jpg" "earth.jpg"
download "https://www.solarsystemscope.com/textures/download/2k_moon.jpg" "moon.jpg"
download "https://www.solarsystemscope.com/textures/download/2k_mars.jpg" "mars.jpg"
download "https://www.solarsystemscope.com/textures/download/2k_jupiter.jpg" "jupiter.jpg"
download "https://www.solarsystemscope.com/textures/download/2k_saturn.jpg" "saturn.jpg"
download "https://www.solarsystemscope.com/textures/download/2k_uranus.jpg" "uranus.jpg"
download "https://www.solarsystemscope.com/textures/download/2k_neptune.jpg" "neptune.jpg"

# Optional: Download Earth night lights and clouds
# download "https://www.solarsystemscope.com/textures/download/2k_earth_nightmap.jpg" "earth_night.jpg"
# download "https://www.solarsystemscope.com/textures/download/2k_earth_clouds.jpg" "earth_clouds.jpg"

echo ""
echo "Download complete! Textures are ready to use."
echo "The simulator will automatically load these textures."
