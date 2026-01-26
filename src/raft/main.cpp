#include <gflags/gflags.h>
#include <spdlog/spdlog.h>
#include <future>
#include <csignal>
#include <optional>
#include <execinfo.h>

#include "raft_server.h"
#include "../../cmake-build-debug/generated/console_generated.h"
#include "spdlog/fmt/bundled/chrono.h"

DEFINE_int32(node, 0, "Node to use when starting up server (default: 0)");
DEFINE_int32(leader_id, 0, "Force this node to have a leader (debugging)");
DEFINE_int32(is_leader, 1, "Force this node to be a leader (debugging 1 true, 0 false)");
DEFINE_string(host_address, "localhost", "Address for host application");
DEFINE_int32(host_port, 8088, "Port for host application");

// TODO
///
/// 1. Finish replication across network
/// 2. Finish leader election
/// 3. Begin working on client -> leader -> replication -> app

std::atomic<bool> running{true};

void handle(int signal) {
    spdlog::info("shutting down server...");
    running.store(false);
    exit(0);
}

void handler(int sig) {
    void *array[20];
    size_t size;
    char **strings;

    size = backtrace(array, 20);
    strings = backtrace_symbols(array, size);

    fprintf(stderr, "Error: signal %d\n", sig);

    for (size_t i = 0; i < size; i++) {
        fprintf(stderr, "#%zu %s\n", i, strings[i]);
    }

    free(strings);
    exit(1);
}

int main(int argc, char *argv[]) {
    std::queue<RaftLogBase::LogEntry> entries_queue;
    std::vector<std::future<void> > clients;
    std::mutex latch;

    gflags::ParseCommandLineFlags(&argc, &argv, true);
    int node = FLAGS_node;
    int leader_id = FLAGS_leader_id;
    bool is_leader = FLAGS_is_leader;

    signal(SIGSEGV, handler);
    struct sigaction sigIntAction{};
    sigIntAction.sa_handler = handle;
    sigemptyset(&sigIntAction.sa_mask);
    sigIntAction.sa_flags = 0;
    sigaction(SIGINT, &sigIntAction, nullptr);

    RaftLogInMem log(RaftLogInMem::NewLogEntry(0, ""));
    auto config = RaftConfig(3, 10000);
    auto raft = RaftNode(std::move(log), config, node, 1,FOLLOWER);


    RaftServer raft_server(std::move(raft));
    raft_server.Start();

    return 0;
}
