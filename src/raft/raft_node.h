#pragma once
#include <map>
#include <optional>
#include <queue>

#include "raft_config.h"
#include "raft_log.h"

enum NodeState {
    LEADER = 0,
    FOLLOWER = 1,
    CANDIDATE = 2,
};

/*
 * RaftNode will handle all consensus and store the state of the current node + its known state for
 * followers as well. It's state could be either Leader, Follower, or Candidate.
 */
class RaftNode {
public:
    RaftNode(RaftLogInMem&& log, const RaftConfig& config, int my_id, int my_term, NodeState node_state = FOLLOWER) :
        state_(node_state), log_(std::move(log)), config_(config), my_id_(my_id), my_term_(my_term) {
        std::map<int, peer_> peers;
        for (const auto& node : config_.GetAll()) {
            peers[node.id] = peer_ {.match_index_ = 0, .next_log_idx_ = 1, .vote_granted_ = false };
        }

        peers_ = std::move(peers);
    };

    ~RaftNode() = default;

    RaftNode(RaftNode&& other) noexcept : log_(std::move(other.log_)) {
        state_ = other.state_;
        my_id_ = other.my_id_;
        config_ = other.config_;
        my_term_ = other.my_term_;
        my_leader_id_ = other.my_leader_id_;
        append_entries_buffer_ = other.append_entries_buffer_;
        append_entries_response_buffer_ = other.append_entries_response_buffer_;
        peers_ = other.peers_;
        commit_index_ = other.commit_index_;
        last_applied_index_ = other.last_applied_index_;
    };

    struct AppendEntriesRPC {
        int type = 1;
        int term, leader_id, prev_log_idx, prev_log_term, leader_commit, log_size, dest;
        std::vector<RaftLogBase::LogEntry> entries;
    };

    struct AppendEntriesResponseRPC {
        bool success;
        int term, match_index, from_node_id, to_node_id;
    };

    struct RequestVoteRpc {
        int from_node_id, to_node_id, term, last_log_index, last_log_term;
        bool vote_granted;
    };

    struct RequestVoteResponseRPC {
        int from_node_id, to_node_id, term;
        bool vote_granted;
    };

    void Send(
        std::optional<std::vector<std::string>> commands,
        std::optional<AppendEntriesResponseRPC> response,
        std::optional<RequestVoteRpc> request_vote
        );

    void Replay(int match_index, int dest);

    void Receive(
        std::optional<AppendEntriesResponseRPC> res,
        std::optional<AppendEntriesRPC> app,
        std::optional<RequestVoteRpc> request_vote,
        std::optional<RequestVoteResponseRPC> request_vote_response
        );

    void SetState(NodeState state);

    void HelloLeader(const int leader_id) { my_leader_id_ = leader_id; };

    [[nodiscard]] RaftConfig Config() const;

    [[nodiscard]] int ID() const { return my_id_; };

    [[nodiscard]] int& MyTerm() { return my_term_; }

    [[nodiscard]] NodeState State() const { return state_; }

    [[nodiscard]] int MyLeader() const { return my_leader_id_; };

    [[nodiscard]] int MyCommitIndex() const { return commit_index_; }

    std::deque<AppendEntriesRPC>& GetEntriesBuffer() { return append_entries_buffer_; };

    std::deque<AppendEntriesResponseRPC>& GetResponsBuffer() { return append_entries_response_buffer_; };

    std::deque<RequestVoteRpc>& GetVotesBuffer() { return request_vote_buffer_; };

    std::deque<RequestVoteResponseRPC>& GetVotesResponseBuffer() { return request_vote_response_buffer_; };

    // FOR IN MEMORY DEBUGGING
    [[nodiscard]] std::vector<RaftLogBase::LogEntry> GetLogBuffer() const { return log_.GetEntries(); }
private:
    NodeState state_;
    RaftLogInMem log_;
    RaftConfig config_;
    int my_id_;
    int my_term_;
    int my_leader_id_;
    int commit_index_ = 0;
    int last_applied_index_ = 0;
    // Only LEADER will use this buffer
    std::deque<AppendEntriesRPC> append_entries_buffer_{};
    // Only FOLLOWER will use these buffers
    std::deque<AppendEntriesResponseRPC> append_entries_response_buffer_{};
    std::deque<RequestVoteRpc> request_vote_buffer_{};
    // Only CANDIDATE will use this buffer
    std::deque<RequestVoteResponseRPC> request_vote_response_buffer_{};
    struct peer_ {
        int next_log_idx_{};
        int match_index_{};
        bool vote_granted_ = false;
    };
    std::map<int, peer_> peers_{};
    int election_timeout_ = 0;
    int heartbeat_time_ = 0;
};