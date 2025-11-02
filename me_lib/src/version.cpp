/**
 * Version information and compatibility checking
 */

#include "matchengine.h"

/* ============================================================================
 * C API Implementation
 * ========================================================================= */

extern "C" {

unsigned int mx_get_version(void) {
    return MX_VERSION;
}

int mx_is_compatible_dll(void) {
    // Check if the DLL major version matches the header major version
    unsigned int dll_version = mx_get_version();
    unsigned int dll_major = (dll_version >> 16) & 0xFF;
    unsigned int header_major = MX_VERSION_MAJOR;
    
    // Compatible if major versions match
    return (dll_major == header_major) ? 1 : 0;
}

const char* mx_status_message(mx_status_t status) {
    switch (status) {
        case MX_STATUS_OK:
            return "Success";
        case MX_STATUS_ERROR:
            return "General error";
        case MX_STATUS_INVALID_PARAM:
            return "Invalid parameter";
        case MX_STATUS_OUT_OF_MEMORY:
            return "Out of memory";
        case MX_STATUS_ORDER_NOT_FOUND:
            return "Order not found";
        case MX_STATUS_INVALID_PRICE:
            return "Invalid price";
        case MX_STATUS_INVALID_QUANTITY:
            return "Invalid quantity";
        case MX_STATUS_DUPLICATE_ORDER:
            return "Duplicate order ID";
        case MX_STATUS_WOULD_MATCH:
            return "POST_ONLY order would have matched";
        case MX_STATUS_CANNOT_FILL:
            return "FOK/AON order cannot be filled";
        case MX_STATUS_STOP_NOT_TRIGGERED:
            return "Stop order not triggered yet";
        default:
            return "Unknown status";
    }
}

const char* mx_order_type_name(mx_order_type_t type) {
    switch (type) {
        case MX_ORDER_TYPE_LIMIT:
            return "LIMIT";
        case MX_ORDER_TYPE_MARKET:
            return "MARKET";
        case MX_ORDER_TYPE_STOP:
            return "STOP";
        case MX_ORDER_TYPE_STOP_LIMIT:
            return "STOP_LIMIT";
        default:
            return "UNKNOWN";
    }
}

const char* mx_tif_name(mx_time_in_force_t tif) {
    switch (tif) {
        case MX_TIF_GTC:
            return "GTC";
        case MX_TIF_IOC:
            return "IOC";
        case MX_TIF_FOK:
            return "FOK";
        case MX_TIF_DAY:
            return "DAY";
        case MX_TIF_GTD:
            return "GTD";
        default:
            return "UNKNOWN";
    }
}

} // extern "C"
