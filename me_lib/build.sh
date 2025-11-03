#!/bin/bash
# MatchX Matching Engine Build Script
# Uses build/ directory for all artifacts (CMake-style)

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

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

# Build directory
BUILD_DIR="build"

print_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
print_success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }
print_warning() { echo -e "${YELLOW}[WARNING]${NC} $1"; }
print_error() { echo -e "${RED}[ERROR]${NC} $1"; }

usage() {
    echo -e "${GREEN}MatchX Matching Engine Build Script${NC}"
    echo ""
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo -e "${YELLOW}Actions:${NC}"
    echo "  --clean              Clean all build artifacts (including .venv)"
    echo "  --generate           Generate build files (in build/)"
    echo "  --build              Build the library and examples"
    echo "  --rebuild            Clean + Generate + Build"
    echo "  --test               Run Python tests (auto-creates venv + installs deps)"
    echo "  --examples           Run all example programs"
    echo "  --benchmark          Run benchmark example"
    echo "  --all                Build and run tests and examples"
    echo ""
    echo -e "${YELLOW}Configuration:${NC}"
    echo "  --debug              Build in debug mode"
    echo "  --release            Build in release mode (default)"
    echo ""
    echo -e "${YELLOW}Compiler:${NC}"
    echo "  --gcc                Use GCC compiler (default)"
    echo "  --clang              Use Clang compiler"
    echo ""
    echo -e "${YELLOW}Options:${NC}"
    echo "  -j, --jobs N         Number of parallel jobs (default: $JOBS)"
    echo "  -v, --verbose        Verbose output"
    echo "  -h, --help           Show this help message"
    echo ""
    echo -e "${YELLOW}Examples:${NC}"
    echo "  $0 --build                    # Build in release mode"
    echo "  $0 --rebuild --debug          # Clean and build in debug mode"
    echo "  $0 --build --test --examples  # Build and run tests and examples"
    echo "  $0 --all                      # Build and run everything"
    echo ""
    echo -e "${YELLOW}Notes:${NC}"
    echo "  • --test will automatically create a Python virtual environment (.venv)"
    echo "  • --test will install pytest and cffi if not present"
    echo "  • --clean will remove the .venv directory"
    echo ""
    exit 0
}

clean() {
    print_info "Cleaning all build artifacts..."
    
    # Remove build directory
    rm -rf "$BUILD_DIR"
    
    # Remove virtual environment
    rm -rf .venv
    
    # Remove any stray bin/obj at root (from old builds)
    rm -rf bin obj
    
    # Remove any Makefiles at root
    rm -f Makefile *.make
    
    # Remove Python cache
    find . -type d -name "__pycache__" -exec rm -rf {} + 2>/dev/null || true
    find . -type d -name ".pytest_cache" -exec rm -rf {} + 2>/dev/null || true
    find . -type f -name "*.pyc" -delete 2>/dev/null || true
    
    print_success "Clean complete"
}

check_premake() {
    if ! command -v premake5 &> /dev/null; then
        print_error "premake5 is not installed"
        echo ""
        echo "Please install premake5:"
        echo "  macOS:   brew install premake"
        echo "  Ubuntu:  sudo apt install premake5"
        echo "  Arch:    sudo pacman -S premake"
        echo "  Other:   Download from https://premake.github.io/"
        exit 1
    fi
}

detect_platform() {
    case "$(uname -s)" in
        Linux*)     PLATFORM="linux";;
        Darwin*)    PLATFORM="macosx";;
        MINGW*|MSYS*|CYGWIN*) PLATFORM="windows";;
        *)          PLATFORM="unknown";;
    esac
}

generate() {
    print_info "Generating build files in $BUILD_DIR/..."
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
    print_info "Platform: $PLATFORM"
    
    # Run premake from project root (where premake5.lua is)
    # This ensures "location build" puts files in ./build/
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
    
    # Verify build directory was created
    if [ ! -d "$BUILD_DIR" ]; then
        print_error "Build directory was not created!"
        exit 1
    fi
    
    print_success "Build files generated in $BUILD_DIR/"
    ls -la "$BUILD_DIR"/*.make 2>/dev/null | head -5 || true
}

build() {
    print_info "Building MatchEngine (config: $CONFIG, compiler: $COMPILER)..."
    
    if [ ! -d "$BUILD_DIR" ]; then
        print_warning "Build directory not found, generating..."
        generate
    fi
    
    if [ ! -f "$BUILD_DIR/Makefile" ]; then
        print_warning "Makefile not found, regenerating..."
        generate
    fi
    
    detect_platform
    
    # Change to build directory to run make
    cd "$BUILD_DIR"
    
    case "$PLATFORM" in
        linux|macosx)
            print_info "Running make with $JOBS parallel jobs..."
            if [ "$VERBOSE" = true ]; then
                make config=$CONFIG -j$JOBS verbose=1
            else
                make config=$CONFIG -j$JOBS
            fi
            ;;
        windows)
            cd ..
            print_error "For Windows, please open build/*.sln in Visual Studio"
            exit 1
            ;;
    esac
    
    # Return to project root
    cd "$SCRIPT_DIR"
    
    print_success "Build complete"
    print_info "Binaries located in: $BUILD_DIR/bin/$CONFIG/"
    echo ""
    ls -lh "$BUILD_DIR/bin/$CONFIG/" 2>/dev/null || true
}

setup_test_environment() {
    print_info "Setting up test environment..."
    
    VENV_DIR="$SCRIPT_DIR/.venv"
    
    # Check if Python 3 is available
    if ! command -v python3 &> /dev/null; then
        print_error "python3 is not installed"
        echo ""
        echo "Please install Python 3:"
        echo "  Ubuntu/Debian: sudo apt install python3 python3-venv python3-pip"
        echo "  Fedora/RHEL:   sudo dnf install python3 python3-pip"
        echo "  macOS:         brew install python3"
        exit 1
    fi
    
    # Create virtual environment if it doesn't exist
    if [ ! -d "$VENV_DIR" ]; then
        print_info "Creating Python virtual environment..."
        python3 -m venv "$VENV_DIR"
        print_success "Virtual environment created at $VENV_DIR"
    else
        print_info "Using existing virtual environment at $VENV_DIR"
    fi
    
    # Activate virtual environment
    source "$VENV_DIR/bin/activate"
    
    # Upgrade pip
    print_info "Upgrading pip..."
    pip install --quiet --upgrade pip
    
    # Install test dependencies
    print_info "Installing test dependencies (pytest, cffi)..."
    pip install --quiet pytest cffi
    
    print_success "Test environment ready"
}

run_tests() {
    print_info "Running tests..."
    
    # Setup test environment (creates venv if needed)
    setup_test_environment
    
    detect_platform
    
    # Determine library extension
    if [ "$PLATFORM" = "macosx" ]; then
        LIB_FILE="$BUILD_DIR/bin/$CONFIG/libMatchEngine.dylib"
        export DYLD_LIBRARY_PATH="$SCRIPT_DIR/$BUILD_DIR/bin/$CONFIG:$DYLD_LIBRARY_PATH"
    elif [ "$PLATFORM" = "linux" ]; then
        LIB_FILE="$BUILD_DIR/bin/$CONFIG/libMatchEngine.so"
        export LD_LIBRARY_PATH="$SCRIPT_DIR/$BUILD_DIR/bin/$CONFIG:$LD_LIBRARY_PATH"
    else
        LIB_FILE="$BUILD_DIR/bin/$CONFIG/MatchEngine.dll"
    fi
    
    if [ ! -f "$LIB_FILE" ]; then
        print_error "Library not found: $LIB_FILE"
        print_info "Building library first..."
        build
    fi
    
    print_info "Using library: $LIB_FILE"
    
    cd tests/
    
    echo ""
    if [ "$VERBOSE" = true ]; then
        pytest -v --tb=short
    else
        pytest -v
    fi
    
    cd "$SCRIPT_DIR"
    
    # Deactivate virtual environment
    deactivate 2>/dev/null || true
    
    print_success "Tests complete"
}

run_examples() {
    print_info "Running examples..."
    
    detect_platform
    EXAMPLE_DIR="$SCRIPT_DIR/$BUILD_DIR/bin/$CONFIG"
    
    if [ ! -d "$EXAMPLE_DIR" ]; then
        print_error "Examples not found: $EXAMPLE_DIR"
        print_info "Building first..."
        build
    fi
    
    # Set library path
    if [ "$PLATFORM" = "macosx" ]; then
        export DYLD_LIBRARY_PATH="$EXAMPLE_DIR:$DYLD_LIBRARY_PATH"
    else
        export LD_LIBRARY_PATH="$EXAMPLE_DIR:$LD_LIBRARY_PATH"
    fi
    
    echo ""
    # Run BasicExample
    if [ -f "$EXAMPLE_DIR/BasicExample" ]; then
        print_info "Running BasicExample..."
        echo "----------------------------------------"
        "$EXAMPLE_DIR/BasicExample"
        echo "----------------------------------------"
        echo ""
    else
        print_warning "BasicExample not found at: $EXAMPLE_DIR/BasicExample"
    fi
    
    # Run AdvancedExample
    if [ -f "$EXAMPLE_DIR/AdvancedExample" ]; then
        print_info "Running AdvancedExample..."
        echo "----------------------------------------"
        "$EXAMPLE_DIR/AdvancedExample"
        echo "----------------------------------------"
        echo ""
    else
        print_warning "AdvancedExample not found at: $EXAMPLE_DIR/AdvancedExample"
    fi
    
    print_success "Examples complete"
}

run_benchmark() {
    print_info "Running benchmark..."
    
    detect_platform
    EXAMPLE_DIR="$SCRIPT_DIR/$BUILD_DIR/bin/$CONFIG"
    
    if [ ! -f "$EXAMPLE_DIR/Benchmark" ]; then
        print_error "Benchmark not found at: $EXAMPLE_DIR/Benchmark"
        print_info "Building first..."
        build
    fi
    
    # Set library path
    if [ "$PLATFORM" = "macosx" ]; then
        export DYLD_LIBRARY_PATH="$EXAMPLE_DIR:$DYLD_LIBRARY_PATH"
    else
        export LD_LIBRARY_PATH="$EXAMPLE_DIR:$LD_LIBRARY_PATH"
    fi
    
    echo ""
    echo "========================================"
    "$EXAMPLE_DIR/Benchmark"
    echo "========================================"
    echo ""
    
    print_success "Benchmark complete"
}

# Parse arguments
if [ $# -eq 0 ]; then
    usage
fi

while [ $# -gt 0 ]; do
    case "$1" in
        --clean) CLEAN=true ;;
        --generate) ACTION="generate" ;;
        --build) ACTION="build" ;;
        --rebuild) CLEAN=true; ACTION="build" ;;
        --test) RUN_TESTS=true ;;
        --examples) RUN_EXAMPLES=true ;;
        --benchmark) ACTION="benchmark" ;;
        --all) ACTION="build"; RUN_TESTS=true; RUN_EXAMPLES=true ;;
        --debug) CONFIG="debug" ;;
        --release) CONFIG="release" ;;
        --gcc) COMPILER="gcc" ;;
        --clang) COMPILER="clang" ;;
        -j|--jobs) shift; JOBS="$1" ;;
        -v|--verbose) VERBOSE=true ;;
        -h|--help) usage ;;
        gmake2|vs2022|vs2019|xcode4)
            check_premake
            premake5 "$1"
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            echo ""
            usage
            ;;
    esac
    shift
done

# Execute
echo ""
print_info "==================================="
print_info "  MatchX Build Script"
print_info "==================================="
print_info "Configuration: $CONFIG"
print_info "Compiler: $COMPILER"
print_info "Jobs: $JOBS"
echo ""

if [ "$CLEAN" = true ]; then
    clean
    echo ""
fi

if [ "$ACTION" = "generate" ]; then
    generate
elif [ "$ACTION" = "build" ]; then
    generate
    echo ""
    build
elif [ "$ACTION" = "benchmark" ]; then
    if [ ! -d "$BUILD_DIR" ]; then
        generate
        echo ""
    fi
    build
    echo ""
    run_benchmark
fi

if [ "$RUN_TESTS" = true ]; then
    echo ""
    run_tests
fi

if [ "$RUN_EXAMPLES" = true ]; then
    echo ""
    run_examples
fi

echo ""
print_success "==================================="
print_success "  All done!"
print_success "==================================="
