#pragma once

#include <sys/socket.h>
#include <netdb.h>
#include <cstdio>
#include <iostream>
#include <cstring>
#include <thread>
#include <unistd.h>
#include <vector>

class ClientAcceptor {
public:
    ClientAcceptor(int acceptor_fd) : acceptor_(acceptor_fd) {}

    void CloseAcceptor() const {
        close(acceptor_);
    }

    [[nodiscard]] std::vector<uint8_t> ReceiveBuffer() const {
        uint32_t size_net;
        recv(acceptor_, &size_net, sizeof(size_net), 0);
        uint32_t size_buffer = ntohl(size_net);
        if (size_buffer <= 0) {
            return std::vector<uint8_t>();
        }
        std::vector<uint8_t> buffer(size_buffer);
        int total_read = 0;
        while (total_read < size_buffer) {
            int bytes_read = recv(acceptor_, buffer.data() + total_read, size_buffer - total_read, 0);
            if (bytes_read <= 0) {
                std::cerr << "recv() failed" << std::endl;
                return {};
            }
            total_read += bytes_read;
        }

        return buffer;
    }

    void Send(const std::string& message) const {
        size_t msg_len = message.size();
        write(acceptor_, message.c_str(), sizeof(char) * msg_len);
    }

    [[nodiscard]] std::string ReceiveMessage() const {
        auto size_str = recv_exactly(acceptor_, 10);
        auto size = std::stoi(size_str);
        return recv_exactly(acceptor_, size);
    }

    void SendMessage(const std::string &message) const {
        size_t msg_len = message.size();
        std::string msg_str = std::to_string(msg_len);
        auto ms = msg_str.c_str();

        write(acceptor_, ms, 10);
        write(acceptor_, message.c_str(), sizeof(char) * msg_len);
    }

    void SendBuffer(uint8_t* data, int size) const {
        uint32_t size_network = htonl(size);
        write(acceptor_, &size_network, sizeof(size_network));
        write(acceptor_, data, size);
    }

    int GetSocket() const { return acceptor_; }
private:
    int acceptor_;

    static std::string recv_exactly(int sock, unsigned long n_bytes) {
        char buffer[n_bytes];
        while (n_bytes > 0) {
            recv(sock, &buffer, n_bytes, 0);
            n_bytes -= n_bytes;
        }

        std::string data(buffer);
        return data;
    }
};

class TcpListener {
public:
    TcpListener(int port) : socket_(socket(AF_INET, SOCK_STREAM, 0)) {
        sockaddr_in saddr{};
        saddr.sin_port = ntohs(port);
        saddr.sin_family = AF_INET;
        saddr.sin_addr.s_addr = INADDR_ANY;

        addrlen_ = sizeof(saddr);
        saddr_ = saddr;
        port_ = port;

        setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &saddr_, addrlen_);

        if (const auto b = bind(socket_, reinterpret_cast<const sockaddr *>(&saddr_), addrlen_); b == -1) {
            throw std::system_error(errno, std::generic_category(), "socket bind failed");
        }

        if (const auto l = listen(socket_, 10); l == -1) {
            throw std::system_error(errno, std::generic_category(), "socket listen failed");
        }
    }

    ~TcpListener() = default;

    ClientAcceptor Accept() {
        auto acc = accept(socket_, reinterpret_cast<sockaddr *>(&saddr_), &addrlen_);
        if (acc == -1) {
            throw std::system_error(errno, std::generic_category(), "socket accept failed");
        }
        return {acc };
    }

    void CloseSocket() const {
        close(socket_);
    }

private:
    [[maybe_unused]] int port_;
    const int socket_;
    sockaddr_in saddr_;
    socklen_t addrlen_;
};

class TcpStream {
public:
    TcpStream(const std::string& address, int port) : socket_(socket(AF_INET, SOCK_STREAM, 0)) {
        struct addrinfo hints{}, *addrs;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = 0;
        hints.ai_flags = 0;
        if (const int status = getaddrinfo(address.c_str(), nullptr, &hints, &addrs); status != 0) {
            throw std::system_error(errno, std::generic_category(), "address lookup failure");
        }

        auto *psai = reinterpret_cast<struct sockaddr_in *>(addrs->ai_addr);
        psai->sin_port = htons(port);

        setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &hints, sizeof(hints));

        saddr_ = reinterpret_cast<const sockaddr *>(psai);
        addrlen_ = addrs->ai_addrlen;
    }

    ~TcpStream() = default;

    void Connect() const {
        if (const int conn = connect(socket_, reinterpret_cast<const sockaddr *>(saddr_), addrlen_); conn == -1) {
            throw std::system_error(errno, std::generic_category(), "connection failed");
        }
    }

    void Send(const std::string &message) const {
        size_t msg_len = message.size();
        write(socket_, message.c_str(), sizeof(char) * msg_len);
    }

    [[nodiscard]] std::string Receive(int size) const {
        char buf[size];
        recv(socket_, &buf, size, 0);
        auto data = std::string(buf);
        return data;
    }

    [[nodiscard]] std::string ReceiveMessage() const {
        auto size_str = recv_exactly(socket_, 10);
        auto size = std::stoi(size_str);
        return recv_exactly(socket_, size);
    }

    [[nodiscard]] std::vector<uint8_t> ReceiveBuffer() const {
        uint32_t size_net;
        recv(socket_, &size_net, sizeof(size_net), 0);
        uint32_t size_buffer = ntohl(size_net);
        std::vector<uint8_t> buffer(size_buffer);
        int total_read = 0;
        while (total_read < size_buffer) {
            int bytes_read = recv(socket_, buffer.data() + total_read, size_buffer - total_read, 0);
            if (bytes_read <= 0) {
                std::cerr << "recv() failed" << std::endl;
                return {};
            }
            total_read += bytes_read;
        }

        return buffer;
    }

    void SendBuffer(uint8_t* data, int size) {
        uint32_t size_network = htonl(size);
        write(socket_, &size_network, sizeof(size_network));
        write(socket_, data, size);
    }

    void SendMessage(const std::string &message) const {
        size_t msg_len = message.size();
        std::string msg_str = std::to_string(msg_len);
        auto ms = msg_str.c_str();

        write(socket_, ms, 10);
        write(socket_, message.c_str(), sizeof(char) * msg_len);
    }

    [[nodiscard]] int GetSocket() const {
        return socket_;
    }

    void CloseSocket() const {
        close(socket_);
    }

private:
    [[maybe_unused]] int port_;
    const int socket_;
    const sockaddr *saddr_;
    socklen_t addrlen_;

    static std::string recv_exactly(int sock, unsigned long n_bytes) {
        char buffer[n_bytes];
        while (n_bytes > 0) {
            recv(sock, &buffer, n_bytes, 0);
            n_bytes -= n_bytes;
        }

        std::string data(buffer);
        return data;
    }
};

class UdpStream {
public:
    UdpStream(const std::string& address, int port) : socket_(socket(AF_INET, SOCK_DGRAM, 0)) {
        struct addrinfo hints{}, *addrs;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = 0;
        hints.ai_flags = 0;
        if (const int status = getaddrinfo(address.c_str(), nullptr, &hints, &addrs); status != 0) {
            throw std::system_error(errno, std::generic_category(), "address lookup failure");
        }

        auto *psai = reinterpret_cast<struct sockaddr_in *>(addrs->ai_addr);
        psai->sin_port = htons(port);

        setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &hints, sizeof(hints));

        if (const int conn = connect(socket_, reinterpret_cast<const sockaddr *>(psai), addrs->ai_addrlen); conn == -1) {
            throw std::system_error(errno, std::generic_category(), "connection failed");
        }

    }

    ~UdpStream() = default;

    [[nodiscard]] std::string ReceiveMessage() const {
        auto size_str = recv_exactly(socket_, 10);
        auto size = std::stoi(size_str);
        return recv_exactly(socket_, size);
    }

    void Send(const std::string& message) const {
        size_t msg_len = message.size();
        write(socket_, message.c_str(), sizeof(char) * msg_len);
    }

    void SendMessage(const std::string &message) const {
        size_t msg_len = message.size();
        std::string msg_str = std::to_string(msg_len);
        auto ms = msg_str.c_str();

        write(socket_, ms, 10);
        write(socket_, message.c_str(), sizeof(char) * msg_len);
    }

    void CloseSocket() const {
        close(socket_);
    }

private:
    [[maybe_unused]] int port_;
    const int socket_;

    static std::string recv_exactly(int sock, unsigned long n_bytes) {
        char buffer[n_bytes];
        while (n_bytes > 0) {
            recv(sock, &buffer, n_bytes, 0);
            n_bytes -= n_bytes;
        }

        std::string data(buffer);
        return data;
    }
};

class UdpListener {
public:
    UdpListener(int port) : socket_(socket(AF_INET, SOCK_DGRAM, 0)) {
        sockaddr_in saddr{};
        saddr.sin_port = ntohs(port);
        saddr.sin_family = AF_INET;
        saddr.sin_addr.s_addr = INADDR_ANY;

        addrlen_ = sizeof(saddr);
        saddr_ = saddr;
        port_ = port;

        setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &saddr_, addrlen_);

        if (const auto b = bind(socket_, reinterpret_cast<const sockaddr *>(&saddr_), addrlen_); b == -1) {
            throw std::system_error(errno, std::generic_category(), "socket bind failed");
        }
    }

    ~UdpListener() = default;

    [[nodiscard]] std::string Recv() const {
        char buf[1024];
        recv(socket_, &buf, 1024, 0);
        auto data = std::string(buf);
        return data;
    }

    void CloseSocket() const {
        close(socket_);
    }

private:
    [[maybe_unused]] int port_;
    const int socket_;
    sockaddr_in saddr_;
    socklen_t addrlen_;
};
