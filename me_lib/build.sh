#!/bin/bash

# MatchX Matching Engine Build Script
# Provides convenient interface for building, testing, and running the library

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Default values
CONFIG="release"
COMPILER="gcc"
ACTION=""
RUN_TESTS=false
RUN_EXAMPLES=false
CLEAN=false
VERBOSE=false
JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# Script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Print colored message
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Print usage
usage() {
    cat << EOF
${GREEN}MatchX Matching Engine Build Script${NC}

Usage: $0 [OPTIONS]

${YELLOW}Actions:${NC}
  --clean              Clean all build artifacts
  --generate           Generate build files with premake
  --build              Build the library and examples
  --rebuild            Clean + Generate + Build
  --test               Run Python tests
  --examples           Run all example programs
  --benchmark          Run benchmark example
  --all                Build and run tests and examples

${YELLOW}Configuration:${NC}
  --debug              Build in debug mode (default: release)
  --release            Build in release mode
  
${YELLOW}Compiler:${NC}
  --gcc                Use GCC compiler (default)
  --clang              Use Clang compiler
  
${YELLOW}Options:${NC}
  -j, --jobs N         Number of parallel jobs (default: $JOBS)
  -v, --verbose        Verbose output
  -h, --help           Show this help message

${YELLOW}Examples:${NC}
  $0 --clean --build --test           # Clean, build, and test
  $0 --rebuild --release --clang      # Rebuild in release with clang
  $0 --all                            # Build and run everything
  $0 --debug --build --examples       # Debug build and run examples
  $0 --benchmark                      # Run performance benchmark

${YELLOW}Premake Actions:${NC}
  gmake2               Generate GNU Makefiles (Linux/macOS)
  vs2022               Generate Visual Studio 2022 solution
  vs2019               Generate Visual Studio 2019 solution
  xcode4               Generate Xcode project

EOF
    exit 0
}

# Clean build artifacts
clean() {
    print_info "Cleaning build artifacts..."
    
    # Remove build directories
    rm -rf bin obj build
    
    # Remove premake generated files
    rm -f *.sln *.vcxproj *.vcxproj.filters *.vcxproj.user
    rm -f *.workspace *.make Makefile
    rm -rf *.xcodeproj *.xcworkspace
    
    # Remove Python cache
    find . -type d -name "__pycache__" -exec rm -rf {} + 2>/dev/null || true
    find . -type d -name ".pytest_cache" -exec rm -rf {} + 2>/dev/null || true
    find . -type f -name "*.pyc" -delete 2>/dev/null || true
    
    print_success "Clean complete"
}

# Check if premake5 is installed
check_premake() {
    if ! command -v premake5 &> /dev/null; then
        print_error "premake5 is not installed"
        echo "Please install premake5:"
        echo "  macOS:   brew install premake"
        echo "  Linux:   Download from https://premake.github.io/"
        exit 1
    fi
}

# Detect platform
detect_platform() {
    case "$(uname -s)" in
        Linux*)     PLATFORM="linux";;
        Darwin*)    PLATFORM="macosx";;
        MINGW*|MSYS*|CYGWIN*)     PLATFORM="windows";;
        *)          PLATFORM="unknown";;
    esac
}

# Generate build files
generate() {
    print_info "Generating build files..."
    check_premake
    detect_platform
    
    # Set compiler
    if [ "$COMPILER" = "clang" ]; then
        export CC=clang
        export CXX=clang++
    else
        export CC=gcc
        export CXX=g++
    fi
    
    print_info "Using compiler: $CC / $CXX"
    
    # Generate appropriate build files for platform
    case "$PLATFORM" in
        linux|macosx)
            premake5 gmake2
            ;;
        windows)
            premake5 vs2022
            ;;
        *)
            print_error "Unknown platform: $PLATFORM"
            exit 1
            ;;
    esac
    
    print_success "Build files generated"
}

# Build the project
build() {
    print_info "Building MatchEngine (config: $CONFIG, compiler: $COMPILER)..."
    
    if [ ! -f "Makefile" ] && [ ! -f "*.sln" ]; then
        print_warning "Build files not found, generating..."
        generate
    fi
    
    detect_platform
    
    # Build based on platform
    case "$PLATFORM" in
        linux|macosx)
            if [ "$VERBOSE" = true ]; then
                make config=$CONFIG -j$JOBS verbose=1
            else
                make config=$CONFIG -j$JOBS
            fi
            ;;
        windows)
            print_error "For Windows, please open the .sln file in Visual Studio"
            exit 1
            ;;
    esac
    
    print_success "Build complete"
    
    # Show build output location
    detect_platform
    BUILD_DIR="bin/${CONFIG^}-$PLATFORM-x86_64"
    print_info "Binaries located in: $BUILD_DIR"
}

# Run Python tests
run_tests() {
    print_info "Running tests..."
    
    # Check if pytest is installed
    if ! command -v pytest &> /dev/null; then
        print_error "pytest is not installed"
        echo "Install with: pip install pytest cffi"
        exit 1
    fi
    
    # Check if library exists
    detect_platform
    BUILD_DIR="bin/${CONFIG^}-$PLATFORM-x86_64"
    
    if [ "$PLATFORM" = "macosx" ]; then
        LIB_FILE="$BUILD_DIR/libmatchengine.dylib"
    elif [ "$PLATFORM" = "linux" ]; then
        LIB_FILE="$BUILD_DIR/libmatchengine.so"
    else
        LIB_FILE="$BUILD_DIR/matchengine.dll"
    fi
    
    if [ ! -f "$LIB_FILE" ]; then
        print_error "Library not found: $LIB_FILE"
        print_info "Building library first..."
        build
    fi
    
    cd tests/
    
    # Run tests with pytest
    if [ "$VERBOSE" = true ]; then
        pytest -v --tb=short
    else
        pytest -v
    fi
    
    cd ..
    
    print_success "Tests complete"
}

# Run examples
run_examples() {
    print_info "Running examples..."
    
    detect_platform
    BUILD_DIR="bin/${CONFIG^}-$PLATFORM-x86_64"
    EXAMPLE_DIR="$BUILD_DIR/examples"
    
    if [ ! -d "$EXAMPLE_DIR" ]; then
        print_error "Examples not found: $EXAMPLE_DIR"
        print_info "Building first..."
        build
    fi
    
    # Set library path
    if [ "$PLATFORM" = "macosx" ]; then
        export DYLD_LIBRARY_PATH="$BUILD_DIR:$DYLD_LIBRARY_PATH"
    else
        export LD_LIBRARY_PATH="$BUILD_DIR:$LD_LIBRARY_PATH"
    fi
    
    # Run BasicExample
    if [ -f "$EXAMPLE_DIR/BasicExample" ]; then
        print_info "Running BasicExample..."
        "$EXAMPLE_DIR/BasicExample"
        echo ""
    fi
    
    # Run AdvancedExample
    if [ -f "$EXAMPLE_DIR/AdvancedExample" ]; then
        print_info "Running AdvancedExample..."
        "$EXAMPLE_DIR/AdvancedExample"
        echo ""
    fi
    
    print_success "Examples complete"
}

# Run benchmark
run_benchmark() {
    print_info "Running benchmark..."
    
    detect_platform
    BUILD_DIR="bin/${CONFIG^}-$PLATFORM-x86_64"
    EXAMPLE_DIR="$BUILD_DIR/examples"
    
    if [ ! -f "$EXAMPLE_DIR/Benchmark" ]; then
        print_error "Benchmark not found"
        print_info "Building first..."
        build
    fi
    
    # Set library path
    if [ "$PLATFORM" = "macosx" ]; then
        export DYLD_LIBRARY_PATH="$BUILD_DIR:$DYLD_LIBRARY_PATH"
    else
        export LD_LIBRARY_PATH="$BUILD_DIR:$LD_LIBRARY_PATH"
    fi
    
    "$EXAMPLE_DIR/Benchmark"
    
    print_success "Benchmark complete"
}

# Parse command line arguments
if [ $# -eq 0 ]; then
    usage
fi

while [ $# -gt 0 ]; do
    case "$1" in
        # Actions
        --clean)
            CLEAN=true
            ;;
        --generate)
            ACTION="generate"
            ;;
        --build)
            ACTION="build"
            ;;
        --rebuild)
            CLEAN=true
            ACTION="build"
            ;;
        --test)
            RUN_TESTS=true
            ;;
        --examples)
            RUN_EXAMPLES=true
            ;;
        --benchmark)
            ACTION="benchmark"
            ;;
        --all)
            ACTION="build"
            RUN_TESTS=true
            RUN_EXAMPLES=true
            ;;
            
        # Configuration
        --debug)
            CONFIG="debug"
            ;;
        --release)
            CONFIG="release"
            ;;
            
        # Compiler
        --gcc)
            COMPILER="gcc"
            ;;
        --clang)
            COMPILER="clang"
            ;;
            
        # Options
        -j|--jobs)
            shift
            JOBS="$1"
            ;;
        -v|--verbose)
            VERBOSE=true
            ;;
        -h|--help)
            usage
            ;;
            
        # Premake actions
        gmake2|vs2022|vs2019|xcode4)
            check_premake
            premake5 "$1"
            exit 0
            ;;
            
        *)
            print_error "Unknown option: $1"
            usage
            ;;
    esac
    shift
done

# Execute actions
print_info "MatchX Build Script"
print_info "Configuration: $CONFIG"
print_info "Compiler: $COMPILER"
echo ""

if [ "$CLEAN" = true ]; then
    clean
fi

if [ "$ACTION" = "generate" ]; then
    generate
elif [ "$ACTION" = "build" ]; then
    generate
    build
elif [ "$ACTION" = "benchmark" ]; then
    if [ ! -f "Makefile" ]; then
        generate
    fi
    build
    run_benchmark
fi

if [ "$RUN_TESTS" = true ]; then
    run_tests
fi

if [ "$RUN_EXAMPLES" = true ]; then
    run_examples
fi

print_success "All done!"
