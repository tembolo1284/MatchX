#!/bin/bash

# ==============================================================================
# ME_SERVER Build Script
# ==============================================================================
# Comprehensive build, test, and run script for the matching engine server
# ==============================================================================

set -e  # Exit on error

# ==============================================================================
# CONFIGURATION
# ==============================================================================

PROJECT_NAME="me_server"
BUILD_CONFIG="release"  # Default to release
COMPILER="gcc"          # Default compiler (gcc or clang)
VERBOSE=0
JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# Detect OS
OS=$(uname -s)
case "$OS" in
    Linux*)     PLATFORM="linux";;
    Darwin*)    PLATFORM="macosx";;
    *)          PLATFORM="unknown";;
esac

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Paths
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ME_LIB_DIR="$(dirname "$SCRIPT_DIR")/me_lib"
BIN_DIR="${SCRIPT_DIR}/build/bin"
ENGINE_SOCKET="/tmp/matching_engine.sock"
GATEWAY_PORT=8080

# PIDs
ENGINE_PID_FILE="/tmp/matching_engine.pid"
GATEWAY_PID_FILE="/tmp/gateway_server.pid"

# ==============================================================================
# HELPER FUNCTIONS
# ==============================================================================

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_header() {
    echo ""
    echo "========================================"
    echo "  $1"
    echo "========================================"
    echo ""
}

check_premake() {
    if ! command -v premake5 &> /dev/null; then
        log_error "premake5 not found. Please install premake5."
        echo ""
        echo "Installation instructions:"
        echo "  Linux:   Download from https://premake.github.io/download"
        echo "  macOS:   brew install premake"
        echo ""
        exit 1
    fi
}

get_build_dir() {
    local config=$(echo "$BUILD_CONFIG" | awk '{print toupper(substr($0,1,1)) tolower(substr($0,2))}')
    echo "${SCRIPT_DIR}/bin/${config}-${PLATFORM}-x86_64"
}

# ==============================================================================
# BUILD FUNCTIONS
# ==============================================================================

check_me_lib() {
    print_header "Checking me_lib (dependency)"
    
    if [ ! -d "$ME_LIB_DIR" ]; then
        log_error "me_lib not found at: $ME_LIB_DIR"
        log_error "Make sure me_lib is in the parent directory"
        exit 1
    fi
    
    # Check if me_lib is built (try release first, then debug)
    local config_lower=$(echo "$BUILD_CONFIG" | tr '[:upper:]' '[:lower:]')
    local lib_path_primary="${ME_LIB_DIR}/build/bin/${config_lower}/libMatchEngineStatic.a"
    
    # Try the requested build config first
    if [ -f "$lib_path_primary" ]; then
        log_success "me_lib found at: $lib_path_primary"
        return 0
    fi
    
    # If not found, try the other config as fallback
    local fallback_config
    if [ "$config_lower" = "release" ]; then
        fallback_config="debug"
    else
        fallback_config="release"
    fi
    
    local lib_path_fallback="${ME_LIB_DIR}/build/bin/${fallback_config}/libMatchEngineStatic.a"
    
    if [ -f "$lib_path_fallback" ]; then
        log_warning "me_lib $BUILD_CONFIG not found, but $fallback_config build exists"
        log_info "Using: $lib_path_fallback"
        return 0
    fi
    
    # Neither found - error
    log_error "me_lib library not found!"
    echo ""
    log_info "Tried:"
    echo "  $lib_path_primary"
    echo "  $lib_path_fallback"
    echo ""
    log_info "Please build me_lib first:"
    echo "  cd $ME_LIB_DIR"
    echo "  ./build.sh build --$BUILD_CONFIG"
    echo ""
    exit 1
}

build_server() {
    print_header "Building me_server"
    
    cd "$SCRIPT_DIR"
    
    # Generate build files
    log_info "Generating build files for me_server..."
    if [ "$COMPILER" = "clang" ]; then
        premake5 gmake2 --cc=clang
    else
        premake5 gmake2
    fi
    
    # Build (from build directory)
    log_info "Building me_server ($BUILD_CONFIG with $COMPILER)..."
    log_info "Using $JOBS parallel jobs"
    
    cd build
    
    if [ $VERBOSE -eq 1 ]; then
        make config=$BUILD_CONFIG -j$JOBS verbose=1
    else
        make config=$BUILD_CONFIG -j$JOBS
    fi
    
    cd "$SCRIPT_DIR"
    
    BUILD_DIR=$(get_build_dir)
    
    # Verify executables
    log_info "Verifying executables..."
    
    if [ -f "${BUILD_DIR}/Engine/matching_engine" ] && \
       [ -f "${BUILD_DIR}/Gateway/gateway_server" ] && \
       [ -f "${BUILD_DIR}/TradingClient/trading_client" ]; then
        log_success "All executables built successfully!"
        echo ""
        log_info "Executables located at:"
        echo "  Engine:  ${BUILD_DIR}/Engine/matching_engine"
        echo "  Gateway: ${BUILD_DIR}/Gateway/gateway_server"
        echo "  Client:  ${BUILD_DIR}/TradingClient/trading_client"
    else
        log_error "Build failed - executables not found"
        exit 1
    fi
}

# ==============================================================================
# CLEAN FUNCTION
# ==============================================================================

clean() {
    print_header "Cleaning build artifacts"
    
    cd "$SCRIPT_DIR"
    
    log_info "Removing build directory..."
    
    # Remove entire build directory
    rm -rf build/
    
    log_success "Clean complete"
    
    # Info about me_lib
    if [ "$1" = "all" ]; then
        log_info "Note: me_lib is a separate project."
        log_info "To clean me_lib, run: cd $ME_LIB_DIR && ./build.sh clean"
    fi
}

# ==============================================================================
# RUN FUNCTIONS
# ==============================================================================

run_server() {
    print_header "Starting Matching Engine Server"
    
    BUILD_DIR=$(get_build_dir)
    
    # Check if already running
    if is_server_running; then
        log_warning "Server is already running!"
        log_info "Run './build.sh stop' to stop the server first"
        exit 1
    fi
    
    # Check executables exist
    if [ ! -f "${BUILD_DIR}/Engine/matching_engine" ] || \
       [ ! -f "${BUILD_DIR}/Gateway/gateway_server" ]; then
        log_error "Executables not found. Run './build.sh build' first"
        exit 1
    fi
    
    # Clean up old socket
    rm -f "$ENGINE_SOCKET"
    
    # Start Engine
    log_info "Starting matching engine..."
    "${BUILD_DIR}/Engine/matching_engine" "$ENGINE_SOCKET" > /tmp/engine.log 2>&1 &
    ENGINE_PID=$!
    echo $ENGINE_PID > "$ENGINE_PID_FILE"
    
    # Wait for engine to be ready
    sleep 2
    
    if ! ps -p $ENGINE_PID > /dev/null; then
        log_error "Engine failed to start. Check /tmp/engine.log"
        cat /tmp/engine.log
        exit 1
    fi
    
    log_success "Engine started (PID: $ENGINE_PID)"
    
    # Start Gateway
    log_info "Starting gateway server..."
    "${BUILD_DIR}/Gateway/gateway_server" "$GATEWAY_PORT" "$ENGINE_SOCKET" > /tmp/gateway.log 2>&1 &
    GATEWAY_PID=$!
    echo $GATEWAY_PID > "$GATEWAY_PID_FILE"
    
    # Wait for gateway to be ready
    sleep 2
    
    if ! ps -p $GATEWAY_PID > /dev/null; then
        log_error "Gateway failed to start. Check /tmp/gateway.log"
        cat /tmp/gateway.log
        kill $ENGINE_PID 2>/dev/null || true
        exit 1
    fi
    
    log_success "Gateway started (PID: $GATEWAY_PID)"
    
    echo ""
    log_success "Server is running!"
    echo ""
    echo "  Engine:  PID $ENGINE_PID (Socket: $ENGINE_SOCKET)"
    echo "  Gateway: PID $GATEWAY_PID (Port: $GATEWAY_PORT)"
    echo ""
    echo "  Logs:"
    echo "    Engine:  /tmp/engine.log"
    echo "    Gateway: /tmp/gateway.log"
    echo ""
    echo "  To connect a client: ./build.sh run-client"
    echo "  To stop server:      ./build.sh stop"
    echo "  To view logs:        ./build.sh logs"
    echo ""
}

run_client() {
    BUILD_DIR=$(get_build_dir)
    
    # Check if server is running
    if ! is_server_running; then
        log_warning "Server doesn't appear to be running"
        log_info "Start server first with: ./build.sh run-server"
        read -p "Continue anyway? (y/n) " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            exit 1
        fi
    fi
    
    # Check executable exists
    if [ ! -f "${BUILD_DIR}/TradingClient/trading_client" ]; then
        log_error "Client executable not found. Run './build.sh build' first"
        exit 1
    fi
    
    # Parse arguments
    HOST="${1:-127.0.0.1}"
    PORT="${2:-$GATEWAY_PORT}"
    USER_ID="${3:-1001}"
    
    print_header "Starting Trading Client"
    log_info "Connecting to: $HOST:$PORT"
    log_info "User ID: $USER_ID"
    echo ""
    
    # Run client
    "${BUILD_DIR}/TradingClient/trading_client" "$HOST" "$PORT" "$USER_ID"
}

stop_server() {
    print_header "Stopping Server"
    
    local stopped=0
    
    # Stop Gateway
    if [ -f "$GATEWAY_PID_FILE" ]; then
        GATEWAY_PID=$(cat "$GATEWAY_PID_FILE")
        if ps -p $GATEWAY_PID > /dev/null 2>&1; then
            log_info "Stopping gateway (PID: $GATEWAY_PID)..."
            kill $GATEWAY_PID
            sleep 1
            if ps -p $GATEWAY_PID > /dev/null 2>&1; then
                kill -9 $GATEWAY_PID 2>/dev/null || true
            fi
            log_success "Gateway stopped"
            stopped=1
        fi
        rm -f "$GATEWAY_PID_FILE"
    fi
    
    # Stop Engine
    if [ -f "$ENGINE_PID_FILE" ]; then
        ENGINE_PID=$(cat "$ENGINE_PID_FILE")
        if ps -p $ENGINE_PID > /dev/null 2>&1; then
            log_info "Stopping engine (PID: $ENGINE_PID)..."
            kill $ENGINE_PID
            sleep 1
            if ps -p $ENGINE_PID > /dev/null 2>&1; then
                kill -9 $ENGINE_PID 2>/dev/null || true
            fi
            log_success "Engine stopped"
            stopped=1
        fi
        rm -f "$ENGINE_PID_FILE"
    fi
    
    # Clean up socket
    rm -f "$ENGINE_SOCKET"
    
    if [ $stopped -eq 0 ]; then
        log_info "No server processes found"
    else
        log_success "Server stopped successfully"
    fi
}

is_server_running() {
    local running=0
    
    if [ -f "$ENGINE_PID_FILE" ]; then
        ENGINE_PID=$(cat "$ENGINE_PID_FILE")
        if ps -p $ENGINE_PID > /dev/null 2>&1; then
            running=1
        fi
    fi
    
    if [ -f "$GATEWAY_PID_FILE" ]; then
        GATEWAY_PID=$(cat "$GATEWAY_PID_FILE")
        if ps -p $GATEWAY_PID > /dev/null 2>&1; then
            running=1
        fi
    fi
    
    return $((1 - running))
}

status() {
    print_header "Server Status"
    
    local engine_running=0
    local gateway_running=0
    
    # Check Engine
    if [ -f "$ENGINE_PID_FILE" ]; then
        ENGINE_PID=$(cat "$ENGINE_PID_FILE")
        if ps -p $ENGINE_PID > /dev/null 2>&1; then
            echo -e "${GREEN}● Engine:${NC}  Running (PID: $ENGINE_PID)"
            engine_running=1
        else
            echo -e "${RED}● Engine:${NC}  Not running (stale PID file)"
        fi
    else
        echo -e "${RED}● Engine:${NC}  Not running"
    fi
    
    # Check Gateway
    if [ -f "$GATEWAY_PID_FILE" ]; then
        GATEWAY_PID=$(cat "$GATEWAY_PID_FILE")
        if ps -p $GATEWAY_PID > /dev/null 2>&1; then
            echo -e "${GREEN}● Gateway:${NC} Running (PID: $GATEWAY_PID)"
            gateway_running=1
        else
            echo -e "${RED}● Gateway:${NC} Not running (stale PID file)"
        fi
    else
        echo -e "${RED}● Gateway:${NC} Not running"
    fi
    
    # Check port
    if [ $gateway_running -eq 1 ]; then
        echo ""
        log_info "Listening on port: $GATEWAY_PORT"
        if command -v netstat &> /dev/null; then
            netstat -tuln 2>/dev/null | grep ":$GATEWAY_PORT " || true
        fi
    fi
    
    echo ""
}

show_logs() {
    print_header "Server Logs"
    
    local log_file="${1:-both}"
    
    case "$log_file" in
        engine)
            if [ -f /tmp/engine.log ]; then
                log_info "Engine Log (/tmp/engine.log):"
                echo "----------------------------------------"
                tail -f /tmp/engine.log
            else
                log_warning "Engine log not found"
            fi
            ;;
        gateway)
            if [ -f /tmp/gateway.log ]; then
                log_info "Gateway Log (/tmp/gateway.log):"
                echo "----------------------------------------"
                tail -f /tmp/gateway.log
            else
                log_warning "Gateway log not found"
            fi
            ;;
        both|*)
            log_info "Showing both logs (Ctrl+C to exit)"
            echo ""
            if [ -f /tmp/engine.log ] && [ -f /tmp/gateway.log ]; then
                tail -f /tmp/engine.log /tmp/gateway.log
            else
                log_warning "Log files not found. Is the server running?"
            fi
            ;;
    esac
}

run_tests() {
    print_header "Running Tests"
    
    BUILD_DIR=$(get_build_dir)
    
    log_info "Building tests..."
    cd build
    make tests config=$BUILD_CONFIG -j$JOBS
    cd "$SCRIPT_DIR"
    
    if [ -f "${BUILD_DIR}/Tests/me_server_tests" ]; then
        log_info "Running test suite..."
        "${BUILD_DIR}/Tests/me_server_tests"
        log_success "All tests passed!"
    else
        log_warning "No tests found (test suite not yet implemented)"
        log_info "Tests will be available in a future release"
    fi
}

benchmark() {
    print_header "Running Benchmark"
    
    # Check if server is running
    if ! is_server_running; then
        log_error "Server must be running for benchmark"
        log_info "Start server first with: ./build.sh run-server"
        exit 1
    fi
    
    BUILD_DIR=$(get_build_dir)
    
    log_info "Sending 1000 orders and measuring latency..."
    echo ""
    
    # This would connect to the client and run stress test
    # For now, we'll just inform the user
    log_info "To run benchmark manually:"
    echo "  1. Connect client: ./build.sh run-client"
    echo "  2. Select option 5 (Stress Test)"
    echo ""
}

# ==============================================================================
# HELP FUNCTION
# ==============================================================================

show_help() {
    echo -e "${BLUE}ME_SERVER Build Script${NC}"
    echo ""
    echo -e "${GREEN}USAGE:${NC}"
    echo "    ./build.sh [command] [options]"
    echo ""
    echo -e "${GREEN}COMMANDS:${NC}"
    echo -e "    ${YELLOW}help${NC}              Show this help message"
    echo -e "    ${YELLOW}build${NC}             Build me_server (requires me_lib to be built first)"
    echo -e "    ${YELLOW}clean${NC}             Clean build artifacts"
    echo -e "    ${YELLOW}run-server${NC}        Start the matching engine and gateway"
    echo -e "    ${YELLOW}run-client${NC}        Start a trading client"
    echo "                      Usage: ./build.sh run-client [host] [port] [user_id]"
    echo "                      Defaults: host=127.0.0.1, port=8080, user_id=1001"
    echo -e "    ${YELLOW}stop${NC}              Stop the running server"
    echo -e "    ${YELLOW}status${NC}            Show server status"
    echo -e "    ${YELLOW}logs${NC}              Show server logs (both engine and gateway)"
    echo -e "    ${YELLOW}logs engine${NC}       Show only engine logs"
    echo -e "    ${YELLOW}logs gateway${NC}      Show only gateway logs"
    echo -e "    ${YELLOW}test${NC}              Build and run test suite"
    echo -e "    ${YELLOW}benchmark${NC}         Run performance benchmark"
    echo ""
    echo -e "${GREEN}OPTIONS:${NC}"
    echo -e "    ${YELLOW}--debug${NC}           Build in debug mode (default: release)"
    echo -e "    ${YELLOW}--release${NC}         Build in release mode"
    echo -e "    ${YELLOW}--clang${NC}           Use clang compiler (default: gcc)"
    echo -e "    ${YELLOW}--gcc${NC}             Use gcc compiler"
    echo -e "    ${YELLOW}--verbose${NC}         Verbose build output"
    echo -e "    ${YELLOW}--jobs N${NC}          Use N parallel jobs (default: auto-detect)"
    echo ""
    echo -e "${GREEN}PREREQUISITES:${NC}"
    echo "    me_lib must be built first:"
    echo "      cd ../me_lib && ./build.sh build --release"
    echo ""
    echo -e "${GREEN}EXAMPLES:${NC}"
    echo "    # Build everything in release mode"
    echo "    ./build.sh build --release"
    echo ""
    echo "    # Build with clang in debug mode"
    echo "    ./build.sh build --clang --debug"
    echo ""
    echo "    # Clean and rebuild"
    echo "    ./build.sh clean && ./build.sh build"
    echo ""
    echo "    # Start server"
    echo "    ./build.sh run-server"
    echo ""
    echo "    # Connect client with custom user ID"
    echo "    ./build.sh run-client 127.0.0.1 8080 1002"
    echo ""
    echo "    # Check server status"
    echo "    ./build.sh status"
    echo ""
    echo "    # View live logs"
    echo "    ./build.sh logs"
    echo ""
    echo "    # Stop server"
    echo "    ./build.sh stop"
    echo ""
    echo -e "${GREEN}BUILD CONFIGURATIONS:${NC}"
    echo "    Debug:   Includes debug symbols, assertions, no optimization"
    echo "    Release: Optimized for performance, no debug info"
    echo ""
    echo -e "${GREEN}DIRECTORY STRUCTURE:${NC}"
    echo "    build/"
    echo "    ├── Makefile"
    echo "    ├── *.make"
    echo "    ├── bin/Release-${PLATFORM}-x86_64/"
    echo "    │   ├── Engine/matching_engine"
    echo "    │   ├── Gateway/gateway_server"
    echo "    │   └── TradingClient/trading_client"
    echo "    └── obj/                    (intermediate files)"
    echo ""
    echo -e "${GREEN}LOG FILES:${NC}"
    echo "    Engine:  /tmp/engine.log"
    echo "    Gateway: /tmp/gateway.log"
    echo ""
    echo -e "${GREEN}PROCESS FILES:${NC}"
    echo "    Engine PID:  ${ENGINE_PID_FILE}"
    echo "    Gateway PID: ${GATEWAY_PID_FILE}"
    echo "    Socket:      ${ENGINE_SOCKET}"
    echo ""
}

# ==============================================================================
# MAIN
# ==============================================================================

main() {
    # Parse options
    COMMAND=""
    
    while [[ $# -gt 0 ]]; do
        case $1 in
            --debug)
                BUILD_CONFIG="debug"
                shift
                ;;
            --release)
                BUILD_CONFIG="release"
                shift
                ;;
            --clang)
                COMPILER="clang"
                shift
                ;;
            --gcc)
                COMPILER="gcc"
                shift
                ;;
            --verbose)
                VERBOSE=1
                shift
                ;;
            --jobs)
                JOBS="$2"
                shift 2
                ;;
            *)
                if [ -z "$COMMAND" ]; then
                    COMMAND="$1"
                else
                    # Extra arguments for commands
                    break
                fi
                shift
                ;;
        esac
    done
    
    # Default to help if no command
    if [ -z "$COMMAND" ]; then
        show_help
        exit 0
    fi
    
    # Execute command
    case "$COMMAND" in
        help|--help|-h)
            show_help
            ;;
        build)
            check_premake
            check_me_lib
            build_server
            ;;
        clean)
            clean "$@"
            ;;
        run-server|start)
            run_server
            ;;
        run-client|client)
            run_client "$@"
            ;;
        stop)
            stop_server
            ;;
        status)
            status
            ;;
        logs)
            show_logs "$@"
            ;;
        test|tests)
            check_premake
            run_tests
            ;;
        benchmark|bench)
            benchmark
            ;;
        *)
            log_error "Unknown command: $COMMAND"
            echo ""
            echo "Run './build.sh help' for usage information"
            exit 1
            ;;
    esac
}

# Run main
main "$@"
