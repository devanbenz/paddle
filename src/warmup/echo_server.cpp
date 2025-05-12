#include "net.hpp"
#include <future>

int main() {
    std::vector<std::future<void>> clients;
    std::atomic<bool> running = true;
    auto tcp_server = TcpListener(8088);

    auto acceptor = std::async(std::launch::async, [&tcp_server, &running, &clients] {
        while (running) {
            auto acceptor = tcp_server.Accept();

            clients.push_back(std::async(std::launch::async, [acceptor] {
                while (true) {
                    auto msg = acceptor.ReceiveMessage();
                    if (std::strlen(msg.data()) > 0) {
                        std::cout << "client said: " << msg << std::endl;
                        if (msg == "exit") {
                            acceptor.CloseAcceptor();
                            break;
                        }
                    }
                    auto bytes = msg.data();
                    acceptor.SendMessage(bytes);
                }
                acceptor.CloseAcceptor();
            }));

        }
    });

    for (auto &handle: clients) {
        handle.wait();
    }

    return 0;
}
