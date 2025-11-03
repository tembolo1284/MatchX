"""
Test helpers - CFFI setup for loading the MatchX library
Following the article's pattern for beautiful native library testing
"""
import os
import sys
import re
from cffi import FFI

# Determine the library path based on platform
def get_library_path():
    """Get the path to the compiled library"""
    here = os.path.abspath(os.path.dirname(__file__))
    project_root = os.path.dirname(here)
    
    if sys.platform == 'darwin':
        # macOS
        lib_name = 'libMatchEngine.dylib'
    elif sys.platform == 'win32':
        # Windows
        lib_name = 'MatchEngine.dll'
    else:
        # Linux
        lib_name = 'libMatchEngine.so'
    
    # Try multiple possible build locations
    possible_paths = [
        os.path.join(project_root, 'build', 'bin', 'release', lib_name),
        os.path.join(project_root, 'build', 'bin', 'debug', lib_name),
        os.path.join(project_root, 'build', 'bin', 'Release', lib_name),
        os.path.join(project_root, 'build', 'bin', 'Debug', lib_name),
        os.path.join(project_root, 'bin', 'release', lib_name),
        os.path.join(project_root, 'bin', 'debug', lib_name),
        os.path.join(project_root, 'bin', 'Release', lib_name),
        os.path.join(project_root, 'bin', 'Debug', lib_name),
    ]
    
    for path in possible_paths:
        if os.path.exists(path):
            return path
    
    raise RuntimeError(
        f"Could not find library {lib_name} in any of:\n" + 
        "\n".join(f"  - {p}" for p in possible_paths) +
        "\n\nPlease build the library first: ./build.sh --build"
    )

def parse_header_for_cffi(header_path):
    """
    Parse the header file and prepare it for CFFI
    """
    with open(header_path, 'r') as f:
        content = f.read()
    
    # Remove C++ extern blocks
    content = re.sub(r'#ifdef __cplusplus\s*extern "C" \{\s*#endif', '', content)
    content = re.sub(r'#ifdef __cplusplus\s*\}\s*#endif', '', content)
    
    # Remove all preprocessor directives
    lines = []
    in_ifdef_block = 0
    typedef_lines = []
    enum_lines = []
    struct_lines = []
    function_lines = []
    
    for line in content.split('\n'):
        stripped = line.strip()
        
        # Track ifdef blocks
        if stripped.startswith('#if'):
            in_ifdef_block += 1
            continue
        elif stripped.startswith('#endif'):
            in_ifdef_block -= 1
            continue
        elif stripped.startswith('#'):
            # Skip all other preprocessor directives
            continue
        
        # Skip lines inside ifdef blocks (unless they contain important declarations)
        if in_ifdef_block > 0 and not any(x in line for x in ['typedef', 'enum', 'struct']):
            continue
        
        # Remove MX_API and other macros
        line = line.replace('MX_API', '')
        
        # Skip empty lines and comments
        if not stripped or stripped.startswith('//') or stripped.startswith('/*'):
            continue
        
        # Categorize declarations
        if 'typedef' in line:
            typedef_lines.append(line)
        elif 'enum' in line:
            enum_lines.append(line)
        elif 'struct' in line:
            struct_lines.append(line)
        elif '(' in line and ');' in line:
            function_lines.append(line)
        else:
            lines.append(line)
    
    # Build the header content for CFFI
    result_lines = []
    
    # First, find all opaque types (types used as pointers but not fully defined)
    # Look for patterns like "typedef struct xyz xyz_t;"
    opaque_types = set()
    for line in typedef_lines:
        # Match: typedef struct/enum name name_t;
        match = re.match(r'typedef\s+(struct|enum)\s+(\w+)\s+(\w+);', line.strip())
        if match:
            opaque_types.add(match.group(3))
    
    # Also scan function signatures for pointer types
    for line in function_lines:
        # Find all type names followed by *
        matches = re.findall(r'(\w+)\s*\*', line)
        for match in matches:
            if match not in ['char', 'void', 'int', 'const']:
                opaque_types.add(match)
    
    # Declare opaque types first
    for otype in sorted(opaque_types):
        result_lines.append(f"typedef struct {otype} {otype};")
    
    result_lines.append("")
    
    # Add enums
    result_lines.extend(enum_lines)
    if enum_lines:
        result_lines.append("")
    
    # Add structs
    result_lines.extend(struct_lines)
    if struct_lines:
        result_lines.append("")
    
    # Add other typedefs (skip the opaque struct ones we already did)
    for line in typedef_lines:
        if not any(f"typedef struct {ot}" in line for ot in opaque_types):
            if not any(f"typedef enum {ot}" in line for ot in opaque_types):
                result_lines.append(line)
    
    if typedef_lines:
        result_lines.append("")
    
    # Add function declarations
    result_lines.extend(function_lines)
    
    result = '\n'.join(result_lines)
    
    # Replace stdint types with CFFI-compatible types
    type_replacements = {
        'uint8_t': 'unsigned char',
        'uint16_t': 'unsigned short',
        'uint32_t': 'unsigned int',
        'uint64_t': 'unsigned long long',
        'int8_t': 'signed char',
        'int16_t': 'short',
        'int32_t': 'int',
        'int64_t': 'long long',
    }
    
    for old, new in type_replacements.items():
        result = result.replace(old, new)
    
    return result

# Initialize CFFI
ffi = FFI()

# Get header path
here = os.path.abspath(os.path.dirname(__file__))
project_root = os.path.dirname(here)
header_path = os.path.join(project_root, 'include', 'matchengine.h')

# Parse the header
try:
    header_content = parse_header_for_cffi(header_path)
    ffi.cdef(header_content)
except Exception as e:
    print("=" * 70)
    print("ERROR: Failed to parse header for CFFI")
    print("=" * 70)
    print(f"Error: {e}")
    print("\nHeader content that was prepared for CFFI:")
    print("-" * 70)
    for i, line in enumerate(header_content.split('\n'), 1):
        print(f"{i:3}: {line}")
    print("-" * 70)
    raise

# Load the library
lib_path = get_library_path()
lib = ffi.dlopen(lib_path)

print(f"✓ Loaded MatchX library from: {lib_path}")

# Try to get version
try:
    version = lib.mx_get_version()
    print(f"✓ Library version: {version}")
except Exception as e:
    print(f"✓ Library loaded (version: unknown)")

# Helper functions for testing
def create_context():
    """Create a new context"""
    return lib.mx_context_new()

def free_context(ctx):
    """Free a context"""
    if ctx and ctx != ffi.NULL:
        lib.mx_context_free(ctx)

def create_order_book(ctx, symbol):
    """Create a new order book"""
    symbol_bytes = symbol.encode('utf-8') if isinstance(symbol, str) else symbol
    return lib.mx_order_book_new(ctx, symbol_bytes)

def free_order_book(book):
    """Free an order book"""
    if book and book != ffi.NULL:
        lib.mx_order_book_free(book)

# Price conversion helpers (for readability in tests)
def price_to_ticks(price_float):
    """Convert float price to integer ticks (e.g., $100.50 -> 10050)"""
    return int(price_float * 100)

def ticks_to_price(ticks):
    """Convert integer ticks to float price (e.g., 10050 -> $100.50)"""
    return ticks / 100.0

# Callback storage for tests
_callback_storage = {}

def create_trade_callback(func):
    """Create a C callback for trades"""
    @ffi.callback("void(void*, unsigned long long, unsigned long long, unsigned int, unsigned int, unsigned long long)")
    def callback(user_data, aggressive_id, passive_id, price, quantity, timestamp):
        func(aggressive_id, passive_id, price, quantity, timestamp)
    
    # Store to prevent garbage collection
    _callback_storage[id(func)] = callback
    return callback

def create_order_callback(func):
    """Create a C callback for order events"""
    @ffi.callback("void(void*, unsigned long long, int, unsigned int, unsigned int)")
    def callback(user_data, order_id, event, filled_qty, remaining_qty):
        func(order_id, event, filled_qty, remaining_qty)
    
    # Store to prevent garbage collection
    _callback_storage[id(func)] = callback
    return callback

# Safe constant getter
def safe_get_constant(name, default=0):
    """Safely get a constant from the library"""
    try:
        return getattr(lib, name)
    except AttributeError:
        return default

# Try to get constants
SIDE_BUY = safe_get_constant('MX_SIDE_BUY', 0)
SIDE_SELL = safe_get_constant('MX_SIDE_SELL', 1)
ORDER_TYPE_LIMIT = safe_get_constant('MX_ORDER_TYPE_LIMIT', 0)
ORDER_TYPE_MARKET = safe_get_constant('MX_ORDER_TYPE_MARKET', 1)
ORDER_TYPE_STOP = safe_get_constant('MX_ORDER_TYPE_STOP', 2)
ORDER_TYPE_STOP_LIMIT = safe_get_constant('MX_ORDER_TYPE_STOP_LIMIT', 3)
TIF_GTC = safe_get_constant('MX_TIF_GTC', 0)
TIF_IOC = safe_get_constant('MX_TIF_IOC', 1)
TIF_FOK = safe_get_constant('MX_TIF_FOK', 2)
TIF_DAY = safe_get_constant('MX_TIF_DAY', 3)
TIF_GTD = safe_get_constant('MX_TIF_GTD', 4)
FLAG_NONE = safe_get_constant('MX_ORDER_FLAG_NONE', 0)
FLAG_POST_ONLY = safe_get_constant('MX_ORDER_FLAG_POST_ONLY', 1)
FLAG_HIDDEN = safe_get_constant('MX_ORDER_FLAG_HIDDEN', 2)
FLAG_AON = safe_get_constant('MX_ORDER_FLAG_AON', 4)
STATUS_OK = safe_get_constant('MX_STATUS_OK', 0)
STATUS_ORDER_NOT_FOUND = safe_get_constant('MX_STATUS_ORDER_NOT_FOUND', -1)
STATUS_DUPLICATE_ORDER = safe_get_constant('MX_STATUS_DUPLICATE_ORDER', -2)
STATUS_WOULD_MATCH = safe_get_constant('MX_STATUS_WOULD_MATCH', -3)
STATUS_CANNOT_FILL = safe_get_constant('MX_STATUS_CANNOT_FILL', -4)
EVENT_ACCEPTED = safe_get_constant('MX_EVENT_ORDER_ACCEPTED', 0)
EVENT_REJECTED = safe_get_constant('MX_EVENT_ORDER_REJECTED', 1)
EVENT_FILLED = safe_get_constant('MX_EVENT_ORDER_FILLED', 2)
EVENT_PARTIAL = safe_get_constant('MX_EVENT_ORDER_PARTIAL', 3)
EVENT_CANCELLED = safe_get_constant('MX_EVENT_ORDER_CANCELLED', 4)
EVENT_EXPIRED = safe_get_constant('MX_EVENT_ORDER_EXPIRED', 5)
EVENT_TRIGGERED = safe_get_constant('MX_EVENT_ORDER_TRIGGERED', 6)

print("✓ Test helpers initialized successfully")
