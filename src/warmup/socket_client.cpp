#include <chrono>
#include <sys/socket.h>
#include <netdb.h>
#include <cstdio>
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <vector>

char* recv_exactly(int sock, size_t n_bytes) {
    char* buffer{};
    while (n_bytes > 0) {
        recv(sock, &buffer, n_bytes, 0);
        n_bytes -= n_bytes;
    }

    return buffer;
}

char* recv_message(int sock) {
    auto size = reinterpret_cast<size_t>(recv_exactly(sock, sizeof(size_t)));
    std::cout << "received " << size << " bytes" <<std::endl;
    return recv_exactly(sock, size);
}

int main() {
    int status;
    struct addrinfo hints{}, *addrs;

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = 0;
    hints.ai_flags = 0;
    status = getaddrinfo("localhost", nullptr, &hints, &addrs);
    if (status != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        exit(1);
    }

    struct sockaddr_in *psai = (struct sockaddr_in*)addrs->ai_addr;
    psai->sin_port = htons(8088);

    int sfd, err;
    sfd = socket(addrs->ai_family, addrs->ai_socktype, addrs->ai_protocol);
    if (sfd == -1) {
        err = errno;
    }

    int conn;
    if ((conn = connect(sfd, reinterpret_cast<const sockaddr *>(psai), addrs->ai_addrlen)) == -1) {
        err = errno;
        fprintf(stderr, "error connecting: %d\n", err);
        auto a = strerror(err);
        std::cout << a << std::endl;
        exit(-1);
    }

    const char* request = "hello";
    send(sfd, request, strlen(request), 0);

    std::vector<char> parts;
    char buffer[512];

    memset(buffer, 0, sizeof(buffer));
    int bytes_received = read(sfd, buffer, 512);
    parts.insert(parts.end(), buffer, buffer + bytes_received);

    std::string resp(parts.begin(), parts.end());
    std::cout << resp << std::endl;

    freeaddrinfo(addrs);
    close(sfd);
}