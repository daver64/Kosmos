#!/bin/bash

# Kosmos Project Helper Script

case "$1" in
    build)
        echo "Building Kosmos..."
        ./build.sh
        ;;
    clean)
        echo "Cleaning build directory..."
        rm -rf build
        echo "Done."
        ;;
    run)
        if [ -f "build/bin/Kosmos" ]; then
            echo "Running Kosmos..."
            cd build/bin
            ./Kosmos
        else
            echo "Error: Kosmos not built yet. Run './kosmos.sh build' first."
        fi
        ;;
    test)
        if [ -d "build" ]; then
            echo "Running tests..."
            cd build
            ctest --output-on-failure
        else
            echo "Error: Build directory not found. Run './kosmos.sh build' first."
        fi
        ;;
    info)
        echo "========================================="
        echo "  Kosmos Space Flight Simulator"
        echo "========================================="
        echo ""
        echo "Project: ~/Documents/Projects/Kosmos"
        echo "Language: C++20"
        echo "Build: CMake + Make"
        echo ""
        echo "Modules:"
        echo "  - Core (Application, Time, Logger)"
        echo "  - Physics (Vessel, FuelTank, Thruster)"
        echo "  - Rendering (OpenGL 4.x)"
        echo "  - Plugin System (DLL/.so vessels)"
        echo ""
        echo "Status:"
        echo "  ✓ Project structure created"
        echo "  ✓ Propulsion system implemented"
        echo "  ✓ Build system configured"
        echo "  ⧗ Awaiting SDL3 and GLM integration"
        echo ""
        ;;
    *)
        echo "Kosmos Project Helper"
        echo ""
        echo "Usage: ./kosmos.sh [command]"
        echo ""
        echo "Commands:"
        echo "  build   - Build the project"
        echo "  clean   - Clean build directory"
        echo "  run     - Run the simulator"
        echo "  test    - Run unit tests"
        echo "  info    - Show project information"
        echo ""
        ;;
esac
