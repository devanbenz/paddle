#pragma once

#include <future>
#include <map>
#include <optional>
#include <queue>

#include "raft_node.h"
#include "../../include/net.hpp"

class RaftServer {
public:
    RaftServer(RaftNode node);

    ~RaftServer() = default;

    void Start();

    enum COMMAND {
        AE = 1,
    };

    void TryElection();

    static uint8_t* PackageAppendEntries(RaftNode::AppendEntriesRPC rpc, int& total_size);

    static RaftNode::AppendEntriesRPC UnpackageAppendEntries(std::vector<uint8_t> msg);

    static uint8_t* PackageAppendEntriesResponse(RaftNode::AppendEntriesResponseRPC rpc, int& total_size);

    static RaftNode::AppendEntriesResponseRPC UnpackageAppendEntriesResponse(std::vector<uint8_t> msg);

    static uint8_t* PackageRequestVote(RaftNode::RequestVoteRpc rpc, int& total_size);

    static RaftNode::RequestVoteRpc UnpackageRequestVote(std::vector<uint8_t> msg);

    static uint8_t* PackageRequestVoteResponse(RaftNode::RequestVoteResponseRPC rpc, int& total_size);

    static RaftNode::RequestVoteResponseRPC UnpackageRequestVoteResponse(std::vector<uint8_t> msg);

    RaftNode *Node() { return &node_; };

    void SendMessage(int node_id, uint8_t* msg, int total_size);

    void ReceiveMessageByType(
        std::vector<uint8_t> msg,
        RaftNode::AppendEntriesRPC* append_entries_rpc,
        RaftNode::AppendEntriesResponseRPC* append_entries_response_rpc,
        RaftNode::RequestVoteRpc* request_vote_rpc,
        RaftNode::RequestVoteResponseRPC* request_vote_response_rpc
        );

    void ReceiveConsoleMessage(std::vector<uint8_t> msg);

    void BecomeLeader();

    void BecomeFollower(int leader_id);

    ClientAcceptor Accept();

    struct Metadata {
        int current_term;
        int candidate_id;
        bool in_memory_log;
        std::string log_file;
    };
private:
    RaftNode node_;
    TcpListener *listener_;
    TcpStream* stream_;
    std::atomic<bool> running{true};
    std::vector<std::future<void> > clients;
    std::mutex latch_;
    // Open file descriptors where the map contains
    // (node_id, file descriptor)
    std::map<int, int> open_fds_{};
    // Buffer to send messages back to console
    std::deque<std::pair<uint8_t*, int>> console_buffer_{};
};