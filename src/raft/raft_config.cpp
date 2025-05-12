#include "raft_config.h"

#include <memory>

RaftConfig::RaftConfig() {
    int port = 15000;
    auto data = std::vector<Server>();
    for (int i = 0; i < 2; i++) {
        auto server = Server {
            .id = i,
            .address = "localhost",
            .port = port,
        };
        data.emplace_back(server);
        port += 1000;
    }

    servers_ = std::move(data);
}

RaftConfig::RaftConfig(const int cluster_size, int starting_port) {
    auto data = std::vector<Server>();
    for (int i = 0; i < cluster_size; i++) {
        auto server = Server {
        .id = i,
        .address = "localhost",
        .port = starting_port,
        };
        data.emplace_back(server);
        starting_port += 1000;
    }

    servers_ = std::move(data);
}

Server RaftConfig::GetOneByIndex(int index) {
    if (index >= servers_.size()) {
        throw std::runtime_error("index out of range");
    }
    return servers_[index];
}

std::vector<Server> RaftConfig::GetAll() {
    return servers_;
}
