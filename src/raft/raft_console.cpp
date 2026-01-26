#include <iostream>
#include <raft_generated.h>
#include <gflags/gflags.h>
#include <vector>
#include <sstream>

#include "raft_config.h"
#include "../../include/net.hpp"
#include "spdlog/spdlog.h"

DEFINE_int32(node, -1, "Node number");

class RaftConsole {
public:
    explicit RaftConsole(const RaftConfig &config) : config_(config) {
    }

    ~RaftConsole() = default;

    Server Get(const int node_id) {
        return config_.GetOneByIndex(node_id);
    };

private:
    RaftConfig config_;
};

int main(int argc, char *argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, false);
    if (FLAGS_node == -1) {
        std::cerr << "Please set a node number with --node" << std::endl;
        return -1;
    }

    // TODO lookup raft config
    auto config = RaftConfig(5 , 10000);
    auto console = RaftConsole(config);
    auto server = console.Get(FLAGS_node);
    auto tcp_stream = TcpStream(server.address, server.port);

    while (true) {
        try {
            tcp_stream.Connect();
            break;
        } catch (const std::exception &e) {
            spdlog::error(e.what());
            spdlog::info("Will attempt connection again...");
            sleep(1);
        }
    }


    std::cout << "Connected to server. Type messages and press Enter to send\n";
    std::stringstream ss;
    ss << "Node=" << server.address << ":" << server.port << " > ";

    std::string data;
    while (true) {
        std::cout << ss.str();
        std::getline(std::cin >> std::ws, data);

        flatbuffers::FlatBufferBuilder builder(512);
        auto command = builder.CreateString(data);
        auto request = RaftSchema::CreateConsoleRequest(builder, command);
        auto message = RaftSchema::CreateMessage(
            builder,
            RaftSchema::MessageType_ConsoleRequest,
            request.Union()
        );
        builder.Finish(message);

        uint8_t *buffer = builder.GetBufferPointer();
        int size = builder.GetSize();

        tcp_stream.SendBuffer(buffer, size);
        auto response = tcp_stream.ReceiveBuffer();

        auto msg = RaftSchema::GetMessage(response.data());

        switch (msg->message_type()) {
            case RaftSchema::MessageType_NONE:
                break;
            case RaftSchema::MessageType_ConsoleRequest:
                break;
            case RaftSchema::MessageType_ConsoleResponse:
                std::cout << "server said: " << msg->message_as_ConsoleResponse()->ok()->str() << std::endl;
                if (msg->message_as_ConsoleResponse()->ok()->str() == "Goodbye") {
                    return EXIT_SUCCESS;
                }
                break;
            default:
                break;
        }
    }

    tcp_stream.CloseSocket();
}
