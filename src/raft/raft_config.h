#pragma once
#include <string>
#include <vector>

struct Server {
    int id;
    std::string address;
    int port;
};

class RaftConfig {
public:
    RaftConfig();

    explicit RaftConfig(int cluster_size, int starting_port);

    ~RaftConfig() = default;

    Server GetOneByIndex(int index);

    std::vector<Server> GetAll();
private:
    std::vector<Server> servers_;
};
