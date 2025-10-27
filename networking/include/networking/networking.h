#pragma once

#ifdef _WIN32
    #ifdef NETWORKING_EXPORTS
        #define NETWORKING_API __declspec(dllexport)
    #elif defined(NETWORKING_IMPORTS)
        #define NETWORKING_API __declspec(dllimport)
    #else
        #define NETWORKING_API
    #endif
#else
    #define NETWORKING_API
#endif

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cstdint>

namespace jupiter {
namespace networking {

/**
 * @brief Network address representation
 */
struct NETWORKING_API Address {
    std::string host;
    uint16_t port;

    Address() : port(0) {}
    Address(const std::string& host, uint16_t port) : host(host), port(port) {}

    /**
     * @brief Convert to string representation
     * @return String in format "host:port"
     */
    std::string toString() const;

    /**
     * @brief Parse address from string
     * @param str String in format "host:port"
     * @return Parsed address
     */
    static Address fromString(const std::string& str);
};

/**
 * @brief Network message
 */
struct NETWORKING_API Message {
    uint32_t type;           // Message type identifier
    std::vector<uint8_t> data; // Message payload
    Address sender;          // Sender address (for received messages)

    Message() : type(0) {}
    Message(uint32_t type) : type(type) {}
    Message(uint32_t type, const std::vector<uint8_t>& data) : type(type), data(data) {}

    /**
     * @brief Get message size in bytes
     * @return Total message size
     */
    size_t getSize() const { return sizeof(type) + data.size(); }
};

/**
 * @brief Network socket interface
 */
class NETWORKING_API ISocket {
public:
    virtual ~ISocket() = default;

    /**
     * @brief Bind socket to address
     * @param address Address to bind to
     * @return true on success
     */
    virtual bool bind(const Address& address) = 0;

    /**
     * @brief Send message to address
     * @param message Message to send
     * @param address Destination address
     * @return true on success
     */
    virtual bool send(const Message& message, const Address& address) = 0;

    /**
     * @brief Receive message
     * @param message Out parameter for received message
     * @param timeoutMs Timeout in milliseconds (0 = non-blocking)
     * @return true if message was received
     */
    virtual bool receive(Message& message, uint32_t timeoutMs = 0) = 0;

    /**
     * @brief Close the socket
     */
    virtual void close() = 0;

    /**
     * @brief Check if socket is valid
     * @return true if socket is valid
     */
    virtual bool isValid() const = 0;

    /**
     * @brief Get bound address
     * @return Bound address
     */
    virtual Address getBoundAddress() const = 0;
};

/**
 * @brief TCP socket implementation
 */
class NETWORKING_API TCPSocket : public ISocket {
public:
    TCPSocket();
    ~TCPSocket() override;

    TCPSocket(const TCPSocket&) = delete;
    TCPSocket& operator=(const TCPSocket&) = delete;

    bool bind(const Address& address) override;
    bool send(const Message& message, const Address& address) override;
    bool receive(Message& message, uint32_t timeoutMs = 0) override;
    void close() override;
    bool isValid() const override;
    Address getBoundAddress() const override;

    /**
     * @brief Connect to remote address
     * @param address Remote address to connect to
     * @return true on success
     */
    bool connect(const Address& address);

    /**
     * @brief Accept incoming connection
     * @return New connected socket, or nullptr on failure
     */
    std::unique_ptr<TCPSocket> accept();

    /**
     * @brief Set blocking mode
     * @param blocking true for blocking, false for non-blocking
     */
    void setBlocking(bool blocking);

private:
    void* m_socket; // Platform-specific socket handle
    bool m_blocking;
    Address m_boundAddress;
};

/**
 * @brief UDP socket implementation
 */
class NETWORKING_API UDPSocket : public ISocket {
public:
    UDPSocket();
    ~UDPSocket() override;

    UDPSocket(const UDPSocket&) = delete;
    UDPSocket& operator=(const UDPSocket&) = delete;

    bool bind(const Address& address) override;
    bool send(const Message& message, const Address& address) override;
    bool receive(Message& message, uint32_t timeoutMs = 0) override;
    void close() override;
    bool isValid() const override;
    Address getBoundAddress() const override;

    /**
     * @brief Set blocking mode
     * @param blocking true for blocking, false for non-blocking
     */
    void setBlocking(bool blocking);

private:
    void* m_socket; // Platform-specific socket handle
    bool m_blocking;
    Address m_boundAddress;
};

/**
 * @brief Network client for connecting to servers
 */
class NETWORKING_API NetworkClient {
public:
    NetworkClient();
    ~NetworkClient();

    NetworkClient(const NetworkClient&) = delete;
    NetworkClient& operator=(const NetworkClient&) = delete;

    /**
     * @brief Connect to server
     * @param address Server address
     * @return true on success
     */
    bool connect(const Address& address);

    /**
     * @brief Disconnect from server
     */
    void disconnect();

    /**
     * @brief Send message to server
     * @param message Message to send
     * @return true on success
     */
    bool sendMessage(const Message& message);

    /**
     * @brief Receive message from server
     * @param message Out parameter for received message
     * @param timeoutMs Timeout in milliseconds
     * @return true if message was received
     */
    bool receiveMessage(Message& message, uint32_t timeoutMs = 0);

    /**
     * @brief Check if connected to server
     * @return true if connected
     */
    bool isConnected() const;

private:
    std::unique_ptr<TCPSocket> m_socket;
    bool m_connected;
};

/**
 * @brief Network server for accepting client connections
 */
class NETWORKING_API NetworkServer {
public:
    NetworkServer();
    ~NetworkServer();

    NetworkServer(const NetworkServer&) = delete;
    NetworkServer& operator=(const NetworkServer&) = delete;

    /**
     * @brief Start server on address
     * @param address Address to listen on
     * @return true on success
     */
    bool start(const Address& address);

    /**
     * @brief Stop server
     */
    void stop();

    /**
     * @brief Accept new client connection
     * @param clientSocket Out parameter for new client socket
     * @return true if client was accepted
     */
    bool acceptClient(std::unique_ptr<TCPSocket>& clientSocket);

    /**
     * @brief Check if server is running
     * @return true if running
     */
    bool isRunning() const;

private:
    std::unique_ptr<TCPSocket> m_listenSocket;
    bool m_running;
};

/**
 * @brief Initialize the networking subsystem
 * @return true if initialization was successful, false otherwise
 */
NETWORKING_API bool initialize();

/**
 * @brief Shutdown the networking subsystem
 */
NETWORKING_API void shutdown();

/**
 * @brief Get local IP addresses
 * @return Vector of local IP addresses
 */
NETWORKING_API std::vector<std::string> getLocalAddresses();

} // namespace networking
} // namespace jupiter
