#include <iostream>
#include <gflags/gflags.h>
#include <vector>

#include "net.hpp"

DEFINE_int32(port, 0, "Port for TCP listener");

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, false);
    if (FLAGS_port == 0) {
        std::cerr << "Port not set" << std::endl;
        return -1;
    }
    auto tcp_stream = TcpStream("localhost", FLAGS_port);
    std::cout << "Connected to server. Type messages and press Enter to send\n";

    std::string data;
    while (true) {
        std::cout << "> ";
        std::getline(std::cin >> std::ws, data);

        if (data == "exit") {
            break;
        }

        tcp_stream.SendMessage(data);
        std::string response = tcp_stream.ReceiveMessage();
        std::cout << "server said: " << response << std::endl;
    }

    tcp_stream.CloseSocket();
}