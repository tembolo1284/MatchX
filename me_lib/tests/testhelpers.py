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
        lib_name = 'libMatchEngine.dylib'
    elif sys.platform == 'win32':
        lib_name = 'MatchEngine.dll'
    else:
        lib_name = 'libMatchEngine.so'
    
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
    Parse the header file and prepare it for CFFI.
    Handles multi-line declarations properly.
    """
    with open(header_path, 'r') as f:
        content = f.read()
    
    # Remove C++ blocks
    content = re.sub(r'#ifdef __cplusplus.*?#endif', '', content, flags=re.DOTALL)
    
    # Remove comments
    content = re.sub(r'/\*.*?\*/', '', content, flags=re.DOTALL)
    content = re.sub(r'//.*$', '', content, flags=re.MULTILINE)
    
    # Remove preprocessor directives
    content = re.sub(r'#.*$', '', content, flags=re.MULTILINE)
    
    # Remove MX_API macro
    content = content.replace('MX_API', '')
    
    # DON'T replace stdint types - CFFI understands them natively!
    # Just make sure we include the standard definitions
    
    # Extract complete declarations (handle multi-line)
    declarations = []
    current_decl = []
    brace_depth = 0
    paren_depth = 0
    
    for line in content.split('\n'):
        line = line.strip()
        if not line:
            continue
        
        current_decl.append(line)
        
        # Track braces and parentheses
        brace_depth += line.count('{') - line.count('}')
        paren_depth += line.count('(') - line.count(')')
        
        # A declaration is complete when we hit a semicolon at depth 0
        if ';' in line and brace_depth == 0 and paren_depth == 0:
            full_decl = ' '.join(current_decl)
            # Clean up extra spaces
            full_decl = re.sub(r'\s+', ' ', full_decl)
            declarations.append(full_decl)
            current_decl = []
    
    # Primitive C types that should NOT be treated as opaque types
    primitive_types = {
        'void', 'char', 'short', 'int', 'long', 'float', 'double',
        'signed', 'unsigned', 'const', 'struct', 'enum', 'union',
        'size_t', 'ssize_t', 'ptrdiff_t', 'wchar_t',
        'uint8_t', 'uint16_t', 'uint32_t', 'uint64_t',
        'int8_t', 'int16_t', 'int32_t', 'int64_t'
    }
    
    # Now organize declarations
    opaque_types = set()
    enums = []
    structs = []
    typedefs = []
    functions = []
    
    for decl in declarations:
        # Skip empty declarations
        if not decl.strip():
            continue
        
        # Categorize
        if 'typedef enum' in decl:
            enums.append(decl)
            # Extract the typedef name
            match = re.search(r'typedef\s+enum.*?}\s*(\w+);', decl)
            if match:
                type_name = match.group(1)
                if type_name not in primitive_types:
                    opaque_types.add(type_name)
        elif 'typedef struct' in decl and '{' not in decl:
            # Opaque struct typedef like: typedef struct xyz xyz_t;
            match = re.match(r'typedef\s+struct\s+\w+\s+(\w+);', decl)
            if match:
                type_name = match.group(1)
                if type_name not in primitive_types:
                    opaque_types.add(type_name)
        elif 'typedef struct' in decl:
            structs.append(decl)
        elif 'typedef' in decl:
            typedefs.append(decl)
        elif '(' in decl and not decl.startswith('typedef'):
            functions.append(decl)
            # Find pointer types in function signatures
            # Match word followed by * but not preceded by )
            matches = re.findall(r'(?<![)])\b(\w+)\s*\*', decl)
            for match in matches:
                if match not in primitive_types and match not in ['char', 'void']:
                    opaque_types.add(match)
    
    # Remove any enum/struct types that are actually defined (not opaque)
    defined_types = set()
    for enum in enums:
        match = re.search(r'typedef\s+enum.*?}\s*(\w+);', enum)
        if match:
            defined_types.add(match.group(1))
    for struct in structs:
        match = re.search(r'typedef\s+struct.*?}\s*(\w+);', struct)
        if match:
            defined_types.add(match.group(1))
    
    # Only keep truly opaque types (not defined in this header)
    opaque_types = opaque_types - defined_types
    
    # Build final CFFI-compatible header
    result = []
    
    # Add stdint types that CFFI needs
    result.append("typedef unsigned char uint8_t;")
    result.append("typedef unsigned short uint16_t;")
    result.append("typedef unsigned int uint32_t;")
    result.append("typedef unsigned long long uint64_t;")
    result.append("typedef signed char int8_t;")
    result.append("typedef short int16_t;")
    result.append("typedef int int32_t;")
    result.append("typedef long long int64_t;")
    result.append("typedef unsigned long size_t;")
    result.append("")
    
    # Declare opaque types first
    for otype in sorted(opaque_types):
        result.append(f"typedef struct {otype} {otype};")
    
    if opaque_types:
        result.append("")
    
    # Add enums
    for enum in enums:
        result.append(enum)
    if enums:
        result.append("")
    
    # Add structs
    for struct in structs:
        result.append(struct)
    if structs:
        result.append("")
    
    # Add typedefs
    for typedef in typedefs:
        result.append(typedef)
    if typedefs:
        result.append("")
    
    # Add functions
    for func in functions:
        result.append(func)
    
    return '\n'.join(result)

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

# Price conversion helpers
def price_to_ticks(price_float):
    """Convert float price to integer ticks (e.g., $100.50 -> 10050)"""
    return int(price_float * 100)

def ticks_to_price(ticks):
    """Convert integer ticks to float price (e.g., 10050 -> $100.50)"""
    return ticks / 100.0

# Callback storage
_callback_storage = {}

def create_trade_callback(func):
    """Create a C callback for trades"""
    @ffi.callback("void(void*, unsigned long long, unsigned long long, unsigned int, unsigned int, unsigned long long)")
    def callback(user_data, aggressive_id, passive_id, price, quantity, timestamp):
        func(aggressive_id, passive_id, price, quantity, timestamp)
    _callback_storage[id(func)] = callback
    return callback

def create_order_callback(func):
    """Create a C callback for order events"""
    @ffi.callback("void(void*, unsigned long long, mx_order_event_t, unsigned int, unsigned int)")
    def callback(user_data, order_id, event, filled_qty, remaining_qty):
        func(order_id, event, filled_qty, remaining_qty)
    _callback_storage[id(func)] = callback
    return callback

# Safe constant getter
def safe_get_constant(name, default=0):
    """Safely get a constant from the library"""
    try:
        return getattr(lib, name)
    except AttributeError:
        return default

# Try to get constants (using the enum values directly)
SIDE_BUY = 0  # MX_SIDE_BUY
SIDE_SELL = 1  # MX_SIDE_SELL
ORDER_TYPE_LIMIT = 0  # MX_ORDER_TYPE_LIMIT
ORDER_TYPE_MARKET = 1  # MX_ORDER_TYPE_MARKET
ORDER_TYPE_STOP = 2  # MX_ORDER_TYPE_STOP
ORDER_TYPE_STOP_LIMIT = 3  # MX_ORDER_TYPE_STOP_LIMIT
TIF_GTC = 0  # MX_TIF_GTC
TIF_IOC = 1  # MX_TIF_IOC
TIF_FOK = 2  # MX_TIF_FOK
TIF_DAY = 3  # MX_TIF_DAY
TIF_GTD = 4  # MX_TIF_GTD
FLAG_NONE = 0  # MX_ORDER_FLAG_NONE
FLAG_POST_ONLY = 1  # MX_ORDER_FLAG_POST_ONLY
FLAG_HIDDEN = 2  # MX_ORDER_FLAG_HIDDEN
FLAG_AON = 4  # MX_ORDER_FLAG_AON
STATUS_OK = 0  # MX_STATUS_OK
STATUS_ERROR = -1  # MX_STATUS_ERROR
STATUS_INVALID_PARAM = -2  # MX_STATUS_INVALID_PARAM
STATUS_OUT_OF_MEMORY = -3  # MX_STATUS_OUT_OF_MEMORY
STATUS_ORDER_NOT_FOUND = -4  # MX_STATUS_ORDER_NOT_FOUND
STATUS_INVALID_PRICE = -5
STATUS_INVALID_QUANTITY = -6
STATUS_DUPLICATE_ORDER = -7  # MX_STATUS_DUPLICATE_ORDER
STATUS_WOULD_MATCH = -8  # MX_STATUS_WOULD_MATCH
STATUS_CANNOT_FILL = -9  # MX_STATUS_CANNOT_FILL
STATUS_STOP_NOT_TRIGGERED = -10
EVENT_ACCEPTED = 0  # MX_EVENT_ORDER_ACCEPTED
EVENT_REJECTED = 1  # MX_EVENT_ORDER_REJECTED
EVENT_FILLED = 2  # MX_EVENT_ORDER_FILLED
EVENT_PARTIAL = 3  # MX_EVENT_ORDER_PARTIAL
EVENT_CANCELLED = 4  # MX_EVENT_ORDER_CANCELLED
EVENT_EXPIRED = 5  # MX_EVENT_ORDER_EXPIRED
EVENT_TRIGGERED = 6  # MX_EVENT_ORDER_TRIGGERED

print("✓ Test helpers initialized successfully")
