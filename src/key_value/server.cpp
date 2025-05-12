#include "server.h"
#include <spdlog/spdlog.h>

#include "gflags/gflags.h"

DEFINE_string(snapshot_file, "", "Snapshot file path on disk");
DEFINE_int32(port, 0, "Port for TCP listener");

int main(int argc, char *argv[]) {
    std::mutex latch;
    std::map<std::string, std::string> internal_store;
    std::vector<std::future<void> > clients;
    std::atomic<bool> running = true;
    gflags::ParseCommandLineFlags(&argc, &argv, false);
    if (FLAGS_port == 0) {
        std::cerr << "ERROR: --port is required" << std::endl;
        exit(-1);
    }

    if (FLAGS_snapshot_file.empty()) {
        std::cerr << "ERROR: --snapshot_file is required" << std::endl;
        exit(-1);
    }

    auto tcp_server = TcpListener(FLAGS_port);
    auto snapshotter = Snapshotter(FLAGS_snapshot_file);
    spdlog::info("server is accepting connections...");

    auto acceptor = std::async(std::launch::async, [&]() {
        while (running) {
            auto acceptor = tcp_server.Accept();
            spdlog::info("client connected...");

            clients.push_back(
                    std::async(
                            std::launch::async, [acceptor, &internal_store, &snapshotter, &latch]() {
                                while (true) {
                                    auto msg = acceptor.ReceiveMessage();
                                    if (std::strlen(msg.data()) > 0) {
                                        if (msg == "exit") {
                                            acceptor.CloseAcceptor();
                                            break;
                                        }
                                    } else {
                                        continue;
                                    }

                                    auto bytes = msg.data();
                                    auto data = parse_message(bytes);
                                    spdlog::info(bytes);
                                    spdlog::info(get_op(data));

                                    latch.lock();
                                    run_command(data, internal_store, acceptor, snapshotter);
                                    latch.unlock();
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
