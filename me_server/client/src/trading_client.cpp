#include "../../common/protocol.h"
#include <iostream>
#include <string>
#include <cstring>
#include <thread>
#include <atomic>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <csignal>
#include <vector>

using namespace matching::protocol;

// =============================================================================
// GLOBAL STATE
// =============================================================================

std::atomic<bool> g_running(true);
int g_socket_fd = -1;

// =============================================================================
// SIGNAL HANDLERS
// =============================================================================

void signal_handler(int signal) {
    std::cout << "\n[Client] Shutting down..." << std::endl;
    (void)signal;
    g_running = false;
    if (g_socket_fd >= 0) {
        close(g_socket_fd);
    }
}

void setup_signal_handlers() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);
}

// =============================================================================
// UTILITY FUNCTIONS
// =============================================================================

uint64_t get_timestamp() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

uint64_t generate_order_id() {
    static uint64_t order_id_counter = 1;
    return order_id_counter++;
}

const char* message_type_to_string(MessageType type) {
    switch (type) {
        case MessageType::ORDER_ACK: return "ORDER_ACK";
        case MessageType::ORDER_REJECT: return "ORDER_REJECT";
        case MessageType::ORDER_CANCELLED: return "ORDER_CANCELLED";
        case MessageType::EXECUTION: return "EXECUTION";
        case MessageType::TRADE: return "TRADE";
        case MessageType::QUOTE: return "QUOTE";
        default: return "UNKNOWN";
    }
}

// =============================================================================
// CONNECTION
// =============================================================================

bool connect_to_gateway(const std::string& host, int port) {
    g_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_socket_fd < 0) {
        std::cerr << "[Client] Failed to create socket: " << strerror(errno) << std::endl;
        return false;
    }
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0) {
        std::cerr << "[Client] Invalid address: " << host << std::endl;
        close(g_socket_fd);
        return false;
    }
    
    if (connect(g_socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "[Client] Failed to connect to " << host << ":" << port 
                 << " - " << strerror(errno) << std::endl;
        close(g_socket_fd);
        return false;
    }
    
    std::cout << "[Client] Connected to gateway at " << host << ":" << port << std::endl;
    return true;
}

bool send_message(const void* data, size_t size) {
    ssize_t sent = send(g_socket_fd, data, size, 0);
    if (sent != static_cast<ssize_t>(size)) {
        std::cerr << "[Client] Failed to send message: " << strerror(errno) << std::endl;
        return false;
    }
    return true;
}

// =============================================================================
// MESSAGE HANDLERS
// =============================================================================

void handle_order_ack(const OrderAckMessage& msg) {
    std::cout << "\n✓ ORDER ACCEPTED" << std::endl;
    std::cout << "  Client Order ID:   " << msg.client_order_id << std::endl;
    std::cout << "  Exchange Order ID: " << msg.exchange_order_id << std::endl;
    std::cout << "  User ID:           " << msg.user_id << std::endl;
    std::cout << std::endl;
}

void handle_order_reject(const OrderRejectMessage& msg) {
    std::cout << "\n✗ ORDER REJECTED" << std::endl;
    std::cout << "  Client Order ID: " << msg.client_order_id << std::endl;
    std::cout << "  User ID:         " << msg.user_id << std::endl;
    std::cout << "  Reason:          " << msg.text << std::endl;
    std::cout << std::endl;
}

void handle_execution(const ExecutionMessage& msg) {
    std::cout << "\n★ EXECUTION" << std::endl;
    std::cout << "  Symbol:          " << msg.get_symbol() << std::endl;
    std::cout << "  Client Order ID: " << msg.client_order_id << std::endl;
    std::cout << "  Exchange Order:  " << msg.exchange_order_id << std::endl;
    std::cout << "  Execution ID:    " << msg.execution_id << std::endl;
    std::cout << "  Side:            " << (msg.get_side() == Side::BUY ? "BUY" : "SELL") << std::endl;
    std::cout << "  Fill Price:      $" << (msg.fill_price / 100.0) << std::endl;
    std::cout << "  Fill Quantity:   " << msg.fill_quantity << std::endl;
    std::cout << "  Leaves Quantity: " << msg.leaves_quantity << std::endl;
    std::cout << std::endl;
}

void handle_trade(const TradeMessage& msg) {
    std::cout << "\n▶ TRADE" << std::endl;
    std::cout << "  Symbol:   " << msg.symbol << std::endl;
    std::cout << "  Trade ID: " << msg.trade_id << std::endl;
    std::cout << "  Price:    $" << (msg.price / 100.0) << std::endl;
    std::cout << "  Quantity: " << msg.quantity << std::endl;
    std::cout << std::endl;
}

void handle_quote(const QuoteMessage& msg) {
    std::cout << "\n📊 QUOTE: " << msg.symbol << std::endl;
    std::cout << "  Bid: $" << (msg.bid_price / 100.0) << " x " << msg.bid_quantity << std::endl;
    std::cout << "  Ask: $" << (msg.ask_price / 100.0) << " x " << msg.ask_quantity << std::endl;
    std::cout << std::endl;
}

void handle_cancel_ack(const OrderRejectMessage& msg) {
    std::cout << "\n✓ ORDER CANCELLED" << std::endl;
    std::cout << "  Client Order ID: " << msg.client_order_id << std::endl;
    std::cout << std::endl;
}

// =============================================================================
// MESSAGE RECEIVER THREAD
// =============================================================================

void message_receiver_thread() {
    std::vector<uint8_t> buffer(4096);
    
    while (g_running) {
        // Read message header
        MessageHeader header;
        ssize_t bytes_read = recv(g_socket_fd, &header, sizeof(header), MSG_PEEK);
        
        if (bytes_read <= 0) {
            if (bytes_read < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            std::cout << "[Client] Connection closed by server" << std::endl;
            g_running = false;
            break;
        }
        
        if (bytes_read < static_cast<ssize_t>(sizeof(header))) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        
        // Read full message
        if (header.length > buffer.size()) {
            buffer.resize(header.length);
        }
        
        bytes_read = recv(g_socket_fd, buffer.data(), header.length, 0);
        if (bytes_read != static_cast<ssize_t>(header.length)) {
            std::cerr << "[Client] Failed to read full message" << std::endl;
            continue;
        }
        
        // Process message
        MessageType msg_type = header.get_type();
        
        switch (msg_type) {
            case MessageType::ORDER_ACK:
                handle_order_ack(*reinterpret_cast<const OrderAckMessage*>(buffer.data()));
                break;
                
            case MessageType::ORDER_REJECT:
                handle_order_reject(*reinterpret_cast<const OrderRejectMessage*>(buffer.data()));
                break;
                
            case MessageType::ORDER_CANCELLED:
                handle_cancel_ack(*reinterpret_cast<const OrderRejectMessage*>(buffer.data()));
                break;
                
            case MessageType::EXECUTION:
                handle_execution(*reinterpret_cast<const ExecutionMessage*>(buffer.data()));
                break;
                
            case MessageType::TRADE:
                handle_trade(*reinterpret_cast<const TradeMessage*>(buffer.data()));
                break;
                
            case MessageType::QUOTE:
                handle_quote(*reinterpret_cast<const QuoteMessage*>(buffer.data()));
                break;
                
            default:
                std::cout << "[Client] Unknown message type: " 
                         << message_type_to_string(msg_type) << std::endl;
                break;
        }
    }
}

// =============================================================================
// ORDER OPERATIONS
// =============================================================================

void send_new_order(const std::string& symbol, Side side, uint64_t price, uint64_t quantity, uint64_t user_id) {
    NewOrderMessage msg;
    msg.set_symbol(symbol);
    msg.client_order_id = generate_order_id();
    msg.user_id = user_id;
    msg.side = static_cast<uint8_t>(side);
    msg.order_type = static_cast<uint8_t>(OrderType::LIMIT);
    msg.price = price;
    msg.quantity = quantity;
    msg.timestamp = get_timestamp();
    
    std::cout << "\n→ Sending NEW_ORDER:" << std::endl;
    std::cout << "  Order ID: " << msg.client_order_id << std::endl;
    std::cout << "  Symbol:   " << symbol << std::endl;
    std::cout << "  Side:     " << (side == Side::BUY ? "BUY" : "SELL") << std::endl;
    std::cout << "  Price:    $" << (price / 100.0) << std::endl;
    std::cout << "  Quantity: " << quantity << std::endl;
    
    if (!send_message(&msg, sizeof(msg))) {
        std::cerr << "[Client] Failed to send order" << std::endl;
    }
}

void send_cancel_order(uint64_t client_order_id, const std::string& symbol, uint64_t user_id) {
    CancelOrderMessage msg;
    msg.set_symbol(symbol);
    msg.client_order_id = client_order_id;
    msg.user_id = user_id;
    msg.timestamp = get_timestamp();
    
    std::cout << "\n→ Sending CANCEL_ORDER:" << std::endl;
    std::cout << "  Order ID: " << client_order_id << std::endl;
    std::cout << "  Symbol:   " << symbol << std::endl;
    
    if (!send_message(&msg, sizeof(msg))) {
        std::cerr << "[Client] Failed to send cancel" << std::endl;
    }
}

// =============================================================================
// INTERACTIVE MENU
// =============================================================================

void print_menu() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "         TRADING CLIENT MENU" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "1. Buy Order" << std::endl;
    std::cout << "2. Sell Order" << std::endl;
    std::cout << "3. Cancel Order" << std::endl;
    std::cout << "4. Market Maker (auto orders)" << std::endl;
    std::cout << "5. Stress Test" << std::endl;
    std::cout << "0. Quit" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Choice: ";
}

void run_interactive_mode(uint64_t user_id) {
    std::string input;
    
    while (g_running) {
        print_menu();
        std::getline(std::cin, input);
        
        if (input.empty()) continue;
        
        int choice = std::atoi(input.c_str());
        
        switch (choice) {
            case 0:
                g_running = false;
                break;
                
            case 1: { // Buy order
                std::cout << "Symbol (AAPL/GOOGL/MSFT/AMZN/TSLA): ";
                std::string symbol;
                std::getline(std::cin, symbol);
                
                std::cout << "Price (e.g., 150.50): ";
                std::getline(std::cin, input);
                double price_dollars = std::atof(input.c_str());
                uint64_t price = static_cast<uint64_t>(price_dollars * 100);
                
                std::cout << "Quantity: ";
                std::getline(std::cin, input);
                uint64_t quantity = std::atoll(input.c_str());
                
                send_new_order(symbol, Side::BUY, price, quantity, user_id);
                break;
            }
            
            case 2: { // Sell order
                std::cout << "Symbol (AAPL/GOOGL/MSFT/AMZN/TSLA): ";
                std::string symbol;
                std::getline(std::cin, symbol);
                
                std::cout << "Price (e.g., 150.50): ";
                std::getline(std::cin, input);
                double price_dollars = std::atof(input.c_str());
                uint64_t price = static_cast<uint64_t>(price_dollars * 100);
                
                std::cout << "Quantity: ";
                std::getline(std::cin, input);
                uint64_t quantity = std::atoll(input.c_str());
                
                send_new_order(symbol, Side::SELL, price, quantity, user_id);
                break;
            }
            
            case 3: { // Cancel order
                std::cout << "Order ID to cancel: ";
                std::getline(std::cin, input);
                uint64_t order_id = std::atoll(input.c_str());
                
                std::cout << "Symbol: ";
                std::string symbol;
                std::getline(std::cin, symbol);
                
                send_cancel_order(order_id, symbol, user_id);
                break;
            }
            
            case 4: { // Market maker
                std::cout << "Running market maker for AAPL (10 orders each side)..." << std::endl;
                
                // Buy side
                for (int i = 0; i < 10; i++) {
                    uint64_t price = 15000 - (i * 10); // $150.00, $149.90, ...
                    send_new_order("AAPL", Side::BUY, price, 100, user_id);
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                
                // Sell side
                for (int i = 0; i < 10; i++) {
                    uint64_t price = 15100 + (i * 10); // $151.00, $151.10, ...
                    send_new_order("AAPL", Side::SELL, price, 100, user_id);
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                
                std::cout << "Market maker orders sent!" << std::endl;
                break;
            }
            
            case 5: { // Stress test
                std::cout << "Stress test: sending 100 orders..." << std::endl;
                auto start = std::chrono::steady_clock::now();
                
                for (int i = 0; i < 100; i++) {
                    Side side = (i % 2 == 0) ? Side::BUY : Side::SELL;
                    uint64_t price = 15000 + (rand() % 200);
                    send_new_order("AAPL", side, price, 10, user_id);
                }
                
                auto end = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
                
                std::cout << "Sent 100 orders in " << duration << "ms" << std::endl;
                std::cout << "Rate: " << (100000.0 / duration) << " orders/sec" << std::endl;
                break;
            }
            
            default:
                std::cout << "Invalid choice!" << std::endl;
                break;
        }
    }
}

// =============================================================================
// MAIN
// =============================================================================

int main(int argc, char* argv[]) {
    std::cout << "========================================" << std::endl;
    std::cout << "   TRADING CLIENT v1.0" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    setup_signal_handlers();
    
    // Configuration
    std::string host = "127.0.0.1";
    int port = 8080;
    uint64_t user_id = 1001;
    
    if (argc > 1) {
        host = argv[1];
    }
    if (argc > 2) {
        port = std::atoi(argv[2]);
    }
    if (argc > 3) {
        user_id = std::atoll(argv[3]);
    }
    
    std::cout << "[Client] Configuration:" << std::endl;
    std::cout << "  Server:  " << host << ":" << port << std::endl;
    std::cout << "  User ID: " << user_id << std::endl;
    std::cout << std::endl;
    
    // Connect to gateway
    if (!connect_to_gateway(host, port)) {
        return 1;
    }
    
    // Start message receiver thread
    std::thread receiver(message_receiver_thread);
    
    // Give receiver time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Run interactive mode
    std::cout << "\n[Client] Ready to trade!\n" << std::endl;
    run_interactive_mode(user_id);
    
    // Cleanup
    g_running = false;
    
    if (receiver.joinable()) {
        receiver.join();
    }
    
    if (g_socket_fd >= 0) {
        close(g_socket_fd);
    }
    
    std::cout << "\n[Client] Disconnected" << std::endl;
    return 0;
}
