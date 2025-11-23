#!/bin/bash
# Quick data download script for Kosmos
# Downloads essential data files for immediate functionality

set -e

echo "========================================"
echo "Kosmos Data Downloader"
echo "========================================"
echo ""

# Check if we're in the Kosmos directory
if [ ! -d "data" ]; then
    echo "Error: Please run this script from the Kosmos root directory"
    exit 1
fi

# Function to download with progress
download_file() {
    local url=$1
    local output=$2
    echo "Downloading: $output"
    wget -q --show-progress "$url" -O "$output" || {
        echo "Failed to download $output"
        return 1
    }
}

# VSOP87 Data (Required for planetary positions)
echo "[1/4] Downloading VSOP87 planetary data..."
cd data/vsop87
if [ ! -f "VSOP87D.ear" ]; then
    wget -q ftp://ftp.imcce.fr/pub/ephem/planets/vsop87/VSOP87D.{mer,ven,ear,mar,jup,sat,ura,nep}
    echo "✓ VSOP87 data downloaded (~5 MB)"
else
    echo "✓ VSOP87 data already exists"
fi
cd ../..

# Star Catalog (Basic test file)
echo ""
echo "[2/4] Creating basic star catalog..."
cd data/catalogs
if [ ! -f "nearest_stars.txt" ]; then
    cat > nearest_stars.txt << 'EOF'
# Nearest Stars Catalog (Test Data)
# Format: Name,Distance(ly),RA(deg),Dec(deg),Magnitude,SpectralType
Sol,0.0,0.0,0.0,-26.74,G2V
Proxima Centauri,4.24,217.42894,-62.67953,11.13,M5.5Ve
Alpha Centauri A,4.37,219.90205,-60.83399,-0.01,G2V
Alpha Centauri B,4.37,219.90205,-60.83399,1.34,K1V
Barnard's Star,5.96,269.45207,4.69339,9.53,M4.0V
Wolf 359,7.86,164.12031,7.00444,13.54,M6.0V
Sirius A,8.66,101.28715,-16.71611,-1.46,A1V
Sirius B,8.66,101.28715,-16.71611,8.44,DA2
Luyten 726-8 A,8.73,24.33803,-17.95691,12.61,M5.5V
Luyten 726-8 B,8.73,24.33803,-17.95691,13.18,M6.0V
Ross 154,9.69,281.43675,-23.81506,10.43,M3.5V
Ross 248,10.32,353.50424,44.16699,12.29,M5.5V
Epsilon Eridani,10.52,53.23267,-9.45832,3.73,K2V
Lacaille 9352,10.74,348.17075,-35.85147,7.34,M1.5V
Ross 128,11.01,176.93988,0.80321,11.13,M4.0V
EOF
    echo "✓ Created nearest_stars.txt with 15 stars"
else
    echo "✓ Star catalog already exists"
fi
cd ../..

# Milky Way Skybox (Optional - large download)
echo ""
echo "[3/4] Downloading Milky Way skybox..."
echo "Note: This is ~50 MB and may take a while"
read -p "Download Milky Way background? (y/N) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    cd data/textures
    mkdir -p skybox
    cd skybox
    # Check if file exists and is not empty
    if [ ! -f "milky_way_gaia_8k.png" ] || [ ! -s "milky_way_gaia_8k.png" ]; then
        if [ ! -f "milkyway.jpg" ] || [ ! -s "milkyway.jpg" ]; then
            # Remove any existing empty files
            rm -f milky_way_gaia_8k.png milkyway.jpg 2>/dev/null
            echo "Trying multiple sources..."
        
            # Try ESA Gaia (original source)
            if download_file "https://www.cosmos.esa.int/documents/29201/1769576/GaiaDR2_FluxRGB_Cartesian_8192x4096.png" \
                            "milky_way_gaia_8k.png"; then
                echo "✓ Milky Way skybox downloaded from ESA Gaia (8K resolution)"
            # Try alternative Gaia URL
            elif download_file "https://sci.esa.int/documents/33356/35971/1567214882002-GaiaDR2_FluxRGB_Cartesian_8192x4096.png" \
                              "milky_way_gaia_8k.png"; then
                echo "✓ Milky Way skybox downloaded from ESA archive (8K resolution)"
            # Try Celestia Motherlode
            elif download_file "http://www.celestiamotherlode.net/catalog/images/screenshots/milkyway-4k.jpg" \
                              "milkyway.jpg"; then
                echo "✓ Milky Way skybox downloaded from Celestia (4K resolution)"
            else
                echo "⚠ Could not download Milky Way texture automatically"
                echo ""
                echo "Would you like to generate a procedural starfield instead? (requires Python + Pillow)"
                read -p "Generate procedural skybox? (y/N) " -n 1 -r
                echo
                if [[ $REPLY =~ ^[Yy]$ ]]; then
                    cd ../../..
                    if command -v python3 &> /dev/null; then
                        echo "Checking Python dependencies..."
                        if python3 -c "import PIL, numpy" 2>/dev/null; then
                            python3 tools/generate_skybox.py
                        else
                            echo "Installing required packages (Pillow and numpy)..."
                            sudo apt-get install -y python3-pil python3-numpy 2>/dev/null || {
                                echo ""
                                echo "⚠ Auto-install failed. Please install manually:"
                                echo "  sudo apt-get install python3-pil python3-numpy"
                                echo "Then run: python3 tools/generate_skybox.py"
                            }
                            # Try running after install attempt
                            if python3 -c "import PIL, numpy" 2>/dev/null; then
                                python3 tools/generate_skybox.py
                            fi
                        fi
                    else
                        echo "⚠ Python3 not found. Install with: sudo apt-get install python3"
                        echo "   Then: sudo apt-get install python3-pil python3-numpy"
                    fi
                    cd data/textures/skybox
                else
                    echo ""
                    echo "Manual download options:"
                    echo "  1. ESA Gaia: https://www.cosmos.esa.int/web/gaia/dr2"
                    echo "  2. Celestia: Search 'milky way texture' on celestiamotherlode.net"
                    echo "  3. NASA: https://svs.gsfc.nasa.gov/cgi-bin/search.cgi?query=milky+way"
                    echo ""
                    echo "Save any downloaded texture as:"
                    echo "  data/textures/skybox/milkyway.jpg (or .png)"
                fi
            fi
        else
            echo "✓ Milky Way skybox already exists"
        fi
    else
        echo "✓ Milky Way skybox already exists"
    fi
    cd ../../..
else
    echo "⊘ Skipped Milky Way skybox"
fi

# Planet Textures (Optional - moderate downloads)
echo ""
echo "[4/4] Downloading planet textures..."
echo "Note: Each texture is ~2-5 MB"
read -p "Download essential planet textures? (y/N) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    cd data/textures/celestial/Sol/planets
    
    # Earth
    if [ ! -f "Earth_color.jpg" ]; then
        echo "Downloading Earth texture..."
        download_file "https://eoimages.gsfc.nasa.gov/images/imagerecords/73000/73909/world.topo.bathy.200412.3x5400x2700.jpg" \
                     "Earth_color.jpg"
        echo "✓ Earth texture downloaded"
    else
        echo "✓ Earth texture already exists"
    fi
    
    # Mars (if Solar System Scope is accessible)
    if [ ! -f "Mars_color.jpg" ]; then
        echo "Downloading Mars texture..."
        download_file "https://www.solarsystemscope.com/textures/download/2k_mars.jpg" \
                     "Mars_color.jpg" || echo "⊘ Mars texture download failed (may need manual download)"
    else
        echo "✓ Mars texture already exists"
    fi
    
    cd ../../../../..
else
    echo "⊘ Skipped planet textures"
fi

# Summary
echo ""
echo "========================================"
echo "Download Complete!"
echo "========================================"
echo ""
echo "Downloaded:"
echo "  ✓ VSOP87 planetary data (8 planets)"
echo "  ✓ Basic star catalog (15 nearest stars)"
if [ -f "data/textures/skybox/milky_way_gaia_8k.png" ] || [ -f "data/textures/skybox/milkyway.jpg" ]; then
    echo "  ✓ Milky Way skybox texture"
fi
if [ -f "data/textures/celestial/Sol/planets/Earth_color.jpg" ]; then
    echo "  ✓ Planet textures"
fi
echo ""
echo "For more data:"
echo "  • Star catalogs: see data/catalogs/README.md"
echo "  • Textures: see data/textures/TEXTURE_SOURCES.md"
echo ""
echo "You can now run: ./Kosmos"
echo "========================================"
