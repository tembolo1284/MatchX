#include "../../common/protocol.h"
#include <iostream>
#include <vector>
#include <map>
#include <set>
#include <cstring>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

using namespace matching::protocol;

// =============================================================================
// GLOBAL STATE
// =============================================================================

std::atomic<bool> g_running(true);

// =============================================================================
// USAGE & VERSION
// =============================================================================

void print_usage(const char* program) {
    std::cout << "========================================\n"
              << "   GATEWAY SERVER v1.0\n"
              << "========================================\n\n"
              << "Usage: " << program << " [OPTIONS] [port] [engine_socket]\n\n"
              << "Arguments:\n"
              << "  port             TCP port to listen on (default: 8080)\n"
              << "  engine_socket    Path to engine's Unix socket\n"
              << "                   (default: /tmp/matching_engine.sock)\n\n"
              << "Options:\n"
              << "  -h, --help       Show this help message\n"
              << "  -v, --version    Show version information\n\n"
              << "Examples:\n"
              << "  " << program << " 8080 /tmp/engine.sock\n"
              << "  " << program << " 9000\n"
              << "  " << program << " --version\n"
              << std::endl;
}

void print_version() {
    std::cout << "Gateway Server v1.0.0\n"
              << "Build: " << __DATE__ << " " << __TIME__ << "\n"
              << "Copyright (c) 2024\n"
              << std::endl;
}

// =============================================================================
// SIGNAL HANDLERS
// =============================================================================

void signal_handler(int signal) {
    (void)signal;
    std::cout << "\n[Gateway] Received signal, shutting down..." << std::endl;
    g_running = false;
}

void setup_signal_handlers() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN); // Ignore broken pipe (client disconnect)
}

// =============================================================================
// CLIENT SESSION
// =============================================================================

class ClientSession {
public:
    ClientSession(int fd, const std::string& address)
        : fd_(fd)
        , address_(address)
        , connected_(true)
        , sequence_(0)
    {
        buffer_.resize(4096);
        std::cout << "[Gateway] New client connected: " << address_ << " (fd=" << fd_ << ")" << std::endl;
    }
    
    ~ClientSession() {
        disconnect();
    }
    
    int get_fd() const { return fd_; }
    const std::string& get_address() const { return address_; }
    bool is_connected() const { return connected_; }
    
    // Read message from client
    ssize_t read_message(MessageHeader& header, std::vector<uint8_t>& message) {
        // Read header first
        ssize_t bytes_read = recv(fd_, &header, sizeof(header), MSG_PEEK);
        
        if (bytes_read <= 0) {
            return bytes_read;
        }
        
        if (bytes_read < static_cast<ssize_t>(sizeof(header))) {
            return 0; // Not enough data yet
        }
        
        // Validate header
        if (header.version != PROTOCOL_VERSION) {
            std::cerr << "[Gateway] Invalid protocol version from " << address_ << std::endl;
            return -1;
        }
        
        if (header.length > 4096) {
            std::cerr << "[Gateway] Message too large from " << address_ << std::endl;
            return -1;
        }
        
        // Check if full message is available
        bytes_read = recv(fd_, nullptr, 0, MSG_PEEK | MSG_TRUNC);
        if (bytes_read < static_cast<ssize_t>(header.length)) {
            return 0; // Wait for more data
        }
        
        // Read the full message
        message.resize(header.length);
        bytes_read = recv(fd_, message.data(), header.length, 0);
        
        if (bytes_read != static_cast<ssize_t>(header.length)) {
            std::cerr << "[Gateway] Failed to read full message from " << address_ << std::endl;
            return -1;
        }
        
        // Copy header from message buffer
        memcpy(&header, message.data(), sizeof(header));
        
        return bytes_read;
    }
    
    // Send message to client
    bool send_message(const void* data, size_t size) {
        if (!connected_) {
            return false;
        }
        
        ssize_t sent = send(fd_, data, size, MSG_NOSIGNAL);
        
        if (sent != static_cast<ssize_t>(size)) {
            std::cerr << "[Gateway] Failed to send to " << address_ 
                     << ": " << strerror(errno) << std::endl;
            disconnect();
            return false;
        }
        
        return true;
    }
    
    void disconnect() {
        if (connected_) {
            std::cout << "[Gateway] Client disconnected: " << address_ << " (fd=" << fd_ << ")" << std::endl;
            close(fd_);
            connected_ = false;
        }
    }
    
    uint64_t get_next_sequence() {
        return ++sequence_;
    }
    
private:
    int fd_;
    std::string address_;
    bool connected_;
    uint64_t sequence_;
    std::vector<uint8_t> buffer_;
};

// =============================================================================
// ENGINE CONNECTION (IPC)
// =============================================================================

class EngineConnection {
public:
    EngineConnection(const std::string& socket_path)
        : socket_path_(socket_path)
        , fd_(-1)
        , connected_(false)
    {}
    
    ~EngineConnection() {
        disconnect();
    }
    
    bool connect() {
        fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd_ < 0) {
            std::cerr << "[Gateway] Failed to create socket: " << strerror(errno) << std::endl;
            return false;
        }
        
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);
        
        if (::connect(fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "[Gateway] Failed to connect to engine: " << strerror(errno) << std::endl;
            close(fd_);
            fd_ = -1;
            return false;
        }
        
        connected_ = true;
        std::cout << "[Gateway] Connected to engine at " << socket_path_ << std::endl;
        return true;
    }
    
    void disconnect() {
        if (fd_ >= 0) {
            close(fd_);
            fd_ = -1;
        }
        connected_ = false;
    }
    
    bool is_connected() const {
        return connected_;
    }
    
    int get_fd() const {
        return fd_;
    }
    
    bool send_message(const void* data, size_t size) {
        if (!connected_) {
            return false;
        }
        
        ssize_t sent = send(fd_, data, size, 0);
        
        if (sent != static_cast<ssize_t>(size)) {
            std::cerr << "[Gateway] Failed to send to engine: " << strerror(errno) << std::endl;
            disconnect();
            return false;
        }
        
        return true;
    }
    
    ssize_t read_message(MessageHeader& header, std::vector<uint8_t>& message) {
        // Read header
        ssize_t bytes_read = recv(fd_, &header, sizeof(header), MSG_PEEK);
        
        if (bytes_read <= 0) {
            return bytes_read;
        }
        
        if (bytes_read < static_cast<ssize_t>(sizeof(header))) {
            return 0; // Not enough data yet
        }
        
        // Read full message
        message.resize(header.length);
        bytes_read = recv(fd_, message.data(), header.length, 0);
        
        if (bytes_read != static_cast<ssize_t>(header.length)) {
            return -1;
        }
        
        memcpy(&header, message.data(), sizeof(header));
        return bytes_read;
    }
    
private:
    std::string socket_path_;
    int fd_;
    bool connected_;
};

// =============================================================================
// GATEWAY SERVER
// =============================================================================

class GatewayServer {
public:
    GatewayServer(int port, const std::string& engine_socket)
        : port_(port)
        , engine_socket_path_(engine_socket)
        , listen_fd_(-1)
        , engine_(engine_socket)
    {}
    
    ~GatewayServer() {
        stop();
    }
    
    bool start() {
        // Connect to engine first
        if (!engine_.connect()) {
            std::cerr << "[Gateway] Failed to connect to engine" << std::endl;
            return false;
        }
        
        // Create TCP listening socket
        listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0) {
            std::cerr << "[Gateway] Failed to create socket: " << strerror(errno) << std::endl;
            return false;
        }
        
        // Set socket options
        int opt = 1;
        if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            std::cerr << "[Gateway] Failed to set SO_REUSEADDR: " << strerror(errno) << std::endl;
        }
        
        // Bind
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port_);
        
        if (bind(listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "[Gateway] Failed to bind to port " << port_ 
                     << ": " << strerror(errno) << std::endl;
            close(listen_fd_);
            return false;
        }
        
        // Listen
        if (listen(listen_fd_, 128) < 0) {
            std::cerr << "[Gateway] Failed to listen: " << strerror(errno) << std::endl;
            close(listen_fd_);
            return false;
        }
        
        std::cout << "[Gateway] Listening on port " << port_ << std::endl;
        return true;
    }
    
    void run() {
        std::cout << "[Gateway] Server started, waiting for connections..." << std::endl;
        
        while (g_running) {
            fd_set read_fds;
            FD_ZERO(&read_fds);
            
            // Add listen socket
            FD_SET(listen_fd_, &read_fds);
            int max_fd = listen_fd_;
            
            // Add engine socket
            if (engine_.is_connected()) {
                FD_SET(engine_.get_fd(), &read_fds);
                max_fd = std::max(max_fd, engine_.get_fd());
            }
            
            // Add client sockets
            for (auto& pair : clients_) {
                ClientSession* client = pair.second.get();
                if (client->is_connected()) {
                    FD_SET(client->get_fd(), &read_fds);
                    max_fd = std::max(max_fd, client->get_fd());
                }
            }
            
            // Wait for activity (1 second timeout)
            struct timeval timeout;
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;
            
            int activity = select(max_fd + 1, &read_fds, nullptr, nullptr, &timeout);
            
            if (activity < 0 && errno != EINTR) {
                std::cerr << "[Gateway] Select error: " << strerror(errno) << std::endl;
                break;
            }
            
            if (activity <= 0) {
                continue; // Timeout or interrupt
            }
            
            // Check for new connection
            if (FD_ISSET(listen_fd_, &read_fds)) {
                accept_client();
            }
            
            // Check for engine messages
            if (engine_.is_connected() && FD_ISSET(engine_.get_fd(), &read_fds)) {
                handle_engine_message();
            }
            
            // Check client sockets
            std::vector<int> to_remove;
            for (auto& pair : clients_) {
                int fd = pair.first;
                ClientSession* client = pair.second.get();
                
                if (client->is_connected() && FD_ISSET(fd, &read_fds)) {
                    if (!handle_client_message(client)) {
                        to_remove.push_back(fd);
                    }
                }
            }
            
            // Remove disconnected clients
            for (int fd : to_remove) {
                clients_.erase(fd);
            }
        }
    }
    
    void stop() {
        std::cout << "[Gateway] Stopping server..." << std::endl;
        
        clients_.clear();
        engine_.disconnect();
        
        if (listen_fd_ >= 0) {
            close(listen_fd_);
            listen_fd_ = -1;
        }
    }
    
private:
    void accept_client() {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        
        int client_fd = accept(listen_fd_, (struct sockaddr*)&client_addr, &addr_len);
        if (client_fd < 0) {
            std::cerr << "[Gateway] Failed to accept client: " << strerror(errno) << std::endl;
            return;
        }
        
        // Get client address
        char addr_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, addr_str, sizeof(addr_str));
        std::string address = std::string(addr_str) + ":" + std::to_string(ntohs(client_addr.sin_port));
        
        // Create session
        auto session = std::make_unique<ClientSession>(client_fd, address);
        clients_[client_fd] = std::move(session);
        
        std::cout << "[Gateway] Total clients: " << clients_.size() << std::endl;
    }
    
    bool handle_client_message(ClientSession* client) {
        MessageHeader header;
        std::vector<uint8_t> message;
        
        ssize_t result = client->read_message(header, message);
        
        if (result < 0) {
            // Error or disconnect
            return false;
        }
        
        if (result == 0) {
            // No complete message yet
            return true;
        }
        
        // Log message
        std::cout << "[Gateway] Received " << get_message_type_name(header.get_type()) 
                 << " from " << client->get_address() << std::endl;
        
        // Forward to engine
        if (!engine_.send_message(message.data(), message.size())) {
            std::cerr << "[Gateway] Failed to forward message to engine" << std::endl;
            return false;
        }
        
        return true;
    }
    
    void handle_engine_message() {
        MessageHeader header;
        std::vector<uint8_t> message;
        
        ssize_t result = engine_.read_message(header, message);
        
        if (result <= 0) {
            std::cerr << "[Gateway] Lost connection to engine" << std::endl;
            g_running = false;
            return;
        }
        
        // Broadcast to all clients (in real system, route by user_id)
        MessageType msg_type = header.get_type();
        
        std::cout << "[Gateway] Broadcasting " << get_message_type_name(msg_type) 
                 << " to " << clients_.size() << " clients" << std::endl;
        
        for (auto& pair : clients_) {
            ClientSession* client = pair.second.get();
            if (client->is_connected()) {
                client->send_message(message.data(), message.size());
            }
        }
    }
    
    const char* get_message_type_name(MessageType type) {
        switch (type) {
            case MessageType::NEW_ORDER: return "NEW_ORDER";
            case MessageType::CANCEL_ORDER: return "CANCEL_ORDER";
            case MessageType::ORDER_ACK: return "ORDER_ACK";
            case MessageType::ORDER_REJECT: return "ORDER_REJECT";
            case MessageType::ORDER_CANCELLED: return "ORDER_CANCELLED";
            case MessageType::EXECUTION: return "EXECUTION";
            case MessageType::TRADE: return "TRADE";
            case MessageType::QUOTE: return "QUOTE";
            case MessageType::HEARTBEAT: return "HEARTBEAT";
            default: return "UNKNOWN";
        }
    }
    
    int port_;
    std::string engine_socket_path_;
    int listen_fd_;
    EngineConnection engine_;
    std::map<int, std::unique_ptr<ClientSession>> clients_;
};

// =============================================================================
// MAIN
// =============================================================================

int main(int argc, char* argv[]) {
    // Parse command line arguments
    int port = 8080;
    std::string engine_socket = "/tmp/matching_engine.sock";
    
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
        
        // First non-flag argument is port
        if (arg[0] != '-' && i == 1) {
            port = std::atoi(arg.c_str());
        }
        // Second non-flag argument is socket path
        else if (arg[0] != '-' && i == 2) {
            engine_socket = arg;
        }
    }
    
    std::cout << "========================================" << std::endl;
    std::cout << "   GATEWAY SERVER v1.0" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    setup_signal_handlers();
    
    std::cout << "[Gateway] Configuration:" << std::endl;
    std::cout << "  Port: " << port << std::endl;
    std::cout << "  Engine socket: " << engine_socket << std::endl;
    std::cout << std::endl;
    
    // Create and start gateway
    GatewayServer gateway(port, engine_socket);
    
    if (!gateway.start()) {
        std::cerr << "[Gateway] Failed to start server" << std::endl;
        return 1;
    }
    
    // Run server
    gateway.run();
    
    // Cleanup
    gateway.stop();
    
    std::cout << "[Gateway] Shutdown complete" << std::endl;
    return 0;
}
