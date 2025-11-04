#include "order_manager.h"
#include "../../common/protocol.h"
#include <iostream>
#include <thread>
#include <atomic>
#include <csignal>
#include <cstring>
#include <vector>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>

using namespace matching::engine;
using namespace matching::protocol;

// =============================================================================
// GLOBAL STATE
// =============================================================================

std::atomic<bool> g_running(true);
OrderManager* g_order_manager = nullptr;

// =============================================================================
// USAGE & VERSION
// =============================================================================

void print_usage(const char* program) {
    std::cout << "========================================\n"
              << "   MATCHING ENGINE v1.0\n"
              << "========================================\n\n"
              << "Usage: " << program << " [OPTIONS] [socket_path]\n\n"
              << "Arguments:\n"
              << "  socket_path      Unix domain socket path for IPC\n"
              << "                   (default: /tmp/matching_engine.sock)\n\n"
              << "Options:\n"
              << "  -h, --help       Show this help message\n"
              << "  -v, --version    Show version information\n\n"
              << "Examples:\n"
              << "  " << program << " /tmp/engine.sock\n"
              << "  " << program << " --version\n"
              << std::endl;
}

void print_version() {
    std::cout << "Matching Engine v1.0.0\n"
              << "Build: " << __DATE__ << " " << __TIME__ << "\n"
              << "Copyright (c) 2024\n"
              << std::endl;
}

// =============================================================================
// SIGNAL HANDLERS
// =============================================================================

void signal_handler(int signal) {
    (void)signal;
    std::cout << "\n[Engine] Received signal, shutting down..." << std::endl;
    g_running = false;
}

void setup_signal_handlers() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
}

// =============================================================================
// IPC COMMUNICATION
// =============================================================================

class IPCServer {
public:
    IPCServer(const std::string& socket_path)
        : socket_path_(socket_path)
        , server_fd_(-1)
        , client_fd_(-1)
    {}
    
    ~IPCServer() {
        stop();
    }
    
    bool start() {
        // Create Unix domain socket
        server_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (server_fd_ < 0) {
            std::cerr << "[IPC] Failed to create socket: " << strerror(errno) << std::endl;
            return false;
        }
        
        // Remove existing socket file if it exists
        unlink(socket_path_.c_str());
        
        // Bind socket
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);
        
        if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "[IPC] Failed to bind socket: " << strerror(errno) << std::endl;
            close(server_fd_);
            return false;
        }
        
        // Listen for connections
        if (listen(server_fd_, 1) < 0) {
            std::cerr << "[IPC] Failed to listen: " << strerror(errno) << std::endl;
            close(server_fd_);
            return false;
        }
        
        std::cout << "[IPC] Listening on " << socket_path_ << std::endl;
        return true;
    }
    
    bool accept_connection() {
        std::cout << "[IPC] Waiting for gateway connection..." << std::endl;
        
        client_fd_ = accept(server_fd_, nullptr, nullptr);
        if (client_fd_ < 0) {
            std::cerr << "[IPC] Failed to accept connection: " << strerror(errno) << std::endl;
            return false;
        }
        
        std::cout << "[IPC] Gateway connected!" << std::endl;
        return true;
    }
    
    ssize_t read_message(void* buffer, size_t size) {
        if (client_fd_ < 0) {
            return -1;
        }
        
        return recv(client_fd_, buffer, size, 0);
    }
    
    ssize_t write_message(const void* buffer, size_t size) {
        if (client_fd_ < 0) {
            return -1;
        }
        
        return send(client_fd_, buffer, size, 0);
    }
    
    void stop() {
        if (client_fd_ >= 0) {
            close(client_fd_);
            client_fd_ = -1;
        }
        
        if (server_fd_ >= 0) {
            close(server_fd_);
            server_fd_ = -1;
        }
        
        unlink(socket_path_.c_str());
    }
    
    bool is_connected() const {
        return client_fd_ >= 0;
    }
    
private:
    std::string socket_path_;
    int server_fd_;
    int client_fd_;
};

// =============================================================================
// MESSAGE PROCESSING
// =============================================================================

void process_message(OrderManager& manager, const MessageHeader& header, 
                     const std::vector<uint8_t>& buffer) {
    MessageType msg_type = header.get_type();
    
    switch (msg_type) {
        case MessageType::NEW_ORDER: {
            if (buffer.size() >= sizeof(NewOrderMessage)) {
                const NewOrderMessage* msg = reinterpret_cast<const NewOrderMessage*>(buffer.data());
                std::cout << "[Engine] Processing NEW_ORDER: client_id=" << msg->client_order_id 
                         << " symbol=" << msg->get_symbol() 
                         << " side=" << (msg->get_side() == Side::BUY ? "BUY" : "SELL")
                         << " price=" << msg->price 
                         << " qty=" << msg->quantity << std::endl;
                manager.handle_new_order(*msg);
            }
            break;
        }
        
        case MessageType::CANCEL_ORDER: {
            if (buffer.size() >= sizeof(CancelOrderMessage)) {
                const CancelOrderMessage* msg = reinterpret_cast<const CancelOrderMessage*>(buffer.data());
                std::cout << "[Engine] Processing CANCEL_ORDER: client_id=" << msg->client_order_id << std::endl;
                manager.handle_cancel_order(*msg);
            }
            break;
        }
        
        case MessageType::HEARTBEAT: {
            // Echo heartbeat back
            std::cout << "[Engine] Received HEARTBEAT" << std::endl;
            break;
        }
        
        default:
            std::cout << "[Engine] Unknown message type: " << static_cast<int>(msg_type) << std::endl;
            break;
    }
}

// =============================================================================
// MESSAGE LOOP
// =============================================================================

void run_message_loop(OrderManager& manager, IPCServer& ipc) {
    std::vector<uint8_t> buffer;
    buffer.resize(4096); // 4KB buffer for messages
    
    while (g_running && ipc.is_connected()) {
        // Read message header first
        MessageHeader header;
        ssize_t bytes_read = ipc.read_message(&header, sizeof(header));
        
        if (bytes_read < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                continue;
            }
            std::cerr << "[Engine] Error reading header: " << strerror(errno) << std::endl;
            break;
        }
        
        if (bytes_read == 0) {
            std::cout << "[Engine] Gateway disconnected" << std::endl;
            break;
        }
        
        if (bytes_read != sizeof(header)) {
            std::cerr << "[Engine] Incomplete header received" << std::endl;
            continue;
        }
        
        // Validate protocol version
        if (header.version != PROTOCOL_VERSION) {
            std::cerr << "[Engine] Invalid protocol version: " << (int)header.version << std::endl;
            continue;
        }
        
        // Read rest of message if needed
        size_t remaining = header.length - sizeof(header);
        if (remaining > 0) {
            if (remaining > buffer.size()) {
                buffer.resize(remaining);
            }
            
            // Copy header to buffer
            memcpy(buffer.data(), &header, sizeof(header));
            
            // Read remaining bytes
            ssize_t remaining_read = ipc.read_message(buffer.data() + sizeof(header), remaining);
            if (remaining_read != static_cast<ssize_t>(remaining)) {
                std::cerr << "[Engine] Incomplete message body" << std::endl;
                continue;
            }
        } else {
            // Just the header
            memcpy(buffer.data(), &header, sizeof(header));
        }
        
        // Process the message
        process_message(manager, header, buffer);
    }
}

// =============================================================================
// STATISTICS REPORTER
// =============================================================================

void run_statistics_reporter(OrderManager& manager) {
    auto last_stats = manager.get_statistics();
    auto last_time = std::chrono::steady_clock::now();
    
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        
        auto current_stats = manager.get_statistics();
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(current_time - last_time).count();
        
        if (elapsed > 0) {
            uint64_t orders_per_sec = (current_stats.total_orders_received - last_stats.total_orders_received) / elapsed;
            uint64_t executions_per_sec = (current_stats.total_executions - last_stats.total_executions) / elapsed;
            
            std::cout << "\n========== ENGINE STATISTICS ==========" << std::endl;
            std::cout << "Total Orders:     " << current_stats.total_orders_received << std::endl;
            std::cout << "Accepted:         " << current_stats.total_orders_accepted << std::endl;
            std::cout << "Rejected:         " << current_stats.total_orders_rejected << std::endl;
            std::cout << "Cancelled:        " << current_stats.total_orders_cancelled << std::endl;
            std::cout << "Executions:       " << current_stats.total_executions << std::endl;
            std::cout << "Total Volume:     " << current_stats.total_volume << std::endl;
            std::cout << "Orders/sec:       " << orders_per_sec << std::endl;
            std::cout << "Executions/sec:   " << executions_per_sec << std::endl;
            std::cout << "========================================\n" << std::endl;
            
            last_stats = current_stats;
            last_time = current_time;
        }
    }
}

// =============================================================================
// MAIN
// =============================================================================

int main(int argc, char* argv[]) {
    // Parse command line arguments
    std::string socket_path = "/tmp/matching_engine.sock";
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }
        
        if (arg == "--version" || arg == "-v") {
            print_version();
            return 0;
        }
        
        // If it's not a flag, treat it as socket path
        if (arg[0] != '-') {
            socket_path = arg;
        }
    }
    
    std::cout << "========================================" << std::endl;
    std::cout << "   MATCHING ENGINE v1.0" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    // Setup signal handlers
    setup_signal_handlers();
    
    // Create order manager
    OrderManager manager;
    g_order_manager = &manager;
    
    // Setup IPC server
    IPCServer ipc(socket_path);
    
    // Set message callback to send via IPC
    manager.set_message_callback([&ipc](const void* data, size_t size) {
        if (ipc.is_connected()) {
            ssize_t sent = ipc.write_message(data, size);
            if (sent != static_cast<ssize_t>(size)) {
                std::cerr << "[Engine] Failed to send message: " << strerror(errno) << std::endl;
            }
        }
    });
    
    // Add default trading symbols
    std::vector<std::string> symbols = {"AAPL", "GOOGL", "MSFT", "AMZN", "TSLA"};
    for (const auto& symbol : symbols) {
        manager.add_symbol(symbol);
    }
    
    std::cout << "[Engine] Configured with " << symbols.size() << " symbols" << std::endl;
    
    // Start IPC server
    if (!ipc.start()) {
        std::cerr << "[Engine] Failed to start IPC server" << std::endl;
        return 1;
    }
    
    // Wait for gateway connection
    if (!ipc.accept_connection()) {
        std::cerr << "[Engine] Failed to accept gateway connection" << std::endl;
        return 1;
    }
    
    // Start statistics reporter thread
    std::thread stats_thread(run_statistics_reporter, std::ref(manager));
    
    // Run message loop (blocking)
    std::cout << "[Engine] Starting message loop..." << std::endl;
    run_message_loop(manager, ipc);
    
    // Cleanup
    std::cout << "[Engine] Shutting down..." << std::endl;
    g_running = false;
    
    if (stats_thread.joinable()) {
        stats_thread.join();
    }
    
    // Print final statistics
    auto final_stats = manager.get_statistics();
    std::cout << "\n========== FINAL STATISTICS ==========" << std::endl;
    std::cout << "Total Orders:     " << final_stats.total_orders_received << std::endl;
    std::cout << "Accepted:         " << final_stats.total_orders_accepted << std::endl;
    std::cout << "Rejected:         " << final_stats.total_orders_rejected << std::endl;
    std::cout << "Cancelled:        " << final_stats.total_orders_cancelled << std::endl;
    std::cout << "Executions:       " << final_stats.total_executions << std::endl;
    std::cout << "Total Volume:     " << final_stats.total_volume << std::endl;
    std::cout << "======================================\n" << std::endl;
    
    std::cout << "[Engine] Shutdown complete" << std::endl;
    return 0;
}
