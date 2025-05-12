#include <chrono>
#include <sys/socket.h>
#include <netdb.h>
#include <cstdio>
#include <iostream>
#include <cstring>
#include <unistd.h>

void send_message(int sock_fd, char *message) {
    size_t msg_len = strlen(message);
    send(sock_fd, reinterpret_cast<char *>(msg_len), sizeof(size_t), 0);
    send(sock_fd, message, msg_len, 0);
}

int main() {
    int err;
    struct sockaddr_in saddr{};
    saddr.sin_port = ntohs(8088);
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = INADDR_ANY;
    socklen_t addrlen = sizeof(saddr);

    auto sock = socket(AF_INET, SOCK_STREAM, 0);

    auto b = bind(sock, reinterpret_cast<const sockaddr *>(&saddr), sizeof(saddr));
    if(b == -1) {
        err = errno;
        fprintf(stderr, "error connecting: %d\n", err);
        auto a = strerror(err);
        std::cout << a << std::endl;
    }

    auto l = listen(sock, 3);
    if(l == -1) {
        err = errno;
        fprintf(stderr, "error connecting: %d\n", err);
        auto a = strerror(err);
        std::cout << a << std::endl;
    }
    while (1) {
        char bytes_read[512];
        auto acc = accept(sock, reinterpret_cast<sockaddr *>(&saddr), &addrlen);
        if (acc == -1) {
            err = errno;
            fprintf(stderr, "error connecting: %d\n", err);
            auto a = strerror(err);
            std::cout << a << std::endl;
        }

        int bytes = read(acc, bytes_read, 512);
        if (bytes > 0) {
            std::cout << bytes_read << std::endl;
            const auto now = std::chrono::system_clock::now();
            const std::time_t t_c = std::chrono::system_clock::to_time_t(now);

            send_message(sock, std::ctime(&t_c));
        }
    }
}
