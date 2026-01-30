#include "raft_node.h"

#include "spdlog/spdlog.h"

void RaftNode::SetState(const NodeState state) {
    state_ = state;
}

void RaftNode::ResetVotes() {
    for (auto &peer : peers_) {
        peer.second.vote_granted_ = false;
    }
}

RaftConfig RaftNode::Config() const {
    return config_;
}

void RaftNode::Send(
    std::optional<std::vector<std::string> > commands,
    std::optional<AppendEntriesResponseRPC> response,
    std::optional<RequestVoteRpc> request_vote
) {
    switch (state_) {
        case LEADER: {
            std::vector<RaftLogBase::LogEntry> entries;
            if (commands.has_value()) {
                for (auto &entry: commands.value()) {
                    entries.push_back(RaftLogInMem::NewLogEntry(my_term_, entry));
                }
            }

            AppendEntriesRPC rpc = {
                .term = my_term_,
                .leader_id = my_id_,
                .prev_log_term = my_term_-1,
                .entries = entries,
            };

            // This is a heartbeat I don't need to append it
            if (!entries.empty()) {
                auto ok = log_.AppendEntries(my_term_, my_id_, log_.GetPrevLogIdx(), log_.GetPrevLogTerm(), entries,
                                             commit_index_);
                if (!ok) {
                    spdlog::error("failed to insert log entry");
                }
            }


            rpc.log_size = log_.GetLogSize();
            rpc.prev_log_idx = log_.GetPrevLogIdx();
            rpc.prev_log_term = log_.GetPrevLogTerm();
            rpc.leader_commit = log_.GetPrevLogIdx();

            // Update myself
            peers_[my_id_].match_index_ = log_.GetLogSize();
            peers_[my_id_].next_log_idx_ = log_.GetLogSize() + 1;

            for (auto &peer: peers_) {
                rpc.dest = peer.first;
                if (peer.first != my_id_) append_entries_buffer_.push_back(rpc);
            }
            break;
        }
        case FOLLOWER: {
            if (response.has_value()) {
                append_entries_response_buffer_.push_back(response.value());
            }
            break;
        }
        // TODO: Leader election
        case CANDIDATE: {
            if (request_vote.has_value()) {
                auto request_vote_value = request_vote.value();
                // Vote for myself
                peers_[my_id_].vote_granted_ = true;
                request_vote_value.term = my_term_;
                request_vote_value.last_log_index = log_.GetPrevLogIdx();
                request_vote_value.last_log_term = log_.GetPrevLogTerm();
                request_vote_buffer_.push_back(request_vote_value);
            }
            break;
        }
    }
}

void RaftNode::Replay(int match_index, int dest) {
    // Okay need to generate a queue of retryable entries starting at the match index
    // of the failed insertion.
    for (int i = match_index; i < log_.GetLogSize(); i++) {
        auto log_entry = log_.GetEntryAt(i);
        auto prev_log_entry = log_.GetEntryAt(i - 1);
        auto rpc = AppendEntriesRPC{
            .term = log_entry.term,
            .leader_id = my_leader_id_,
            .prev_log_term = prev_log_entry.term,
            .entries = std::vector({log_entry}),
            .prev_log_idx = i - 1,
            .log_size = i,
            .dest = dest,
            .leader_commit = commit_index_,
        };

        append_entries_buffer_.push_back(rpc);
    }
}


void RaftNode::Receive(
    std::optional<AppendEntriesResponseRPC> res,
    std::optional<AppendEntriesRPC> app,
    std::optional<RequestVoteRpc> request_vote,
    std::optional<RequestVoteResponseRPC> request_vote_response
) {
    int incoming_term = 0;
    if (res.has_value()) incoming_term = res.value().term;
    else if (app.has_value()) incoming_term = app.value().term;
    else if (request_vote.has_value()) incoming_term = request_vote.value().term;
    else if (request_vote_response.has_value()) incoming_term = request_vote_response.value().term;

    if (incoming_term > my_term_) {
        spdlog::info("Stepping down: saw term {} > my term {}", incoming_term, my_term_);
        my_term_ = incoming_term;
        SetState(FOLLOWER);
        ResetVotes();
    }

    switch (state_) {
        case LEADER: {
            if (res.has_value()) {
                auto response = res.value();

                // TODO: Find out how to compute the commit index should we wait for all peers?
                if (response.success && response.match_index == log_.GetLogSize()) {
                    int nodes_at_or_above_index = 0;
                    int amount_of_peers = peers_.size();
                    int current_index = log_.GetLogSize();
                    peers_[response.from_node_id].match_index_ = log_.GetLogSize();
                    peers_[response.from_node_id].next_log_idx_ = log_.GetLogSize() + 1;

                    // Check commit index for leader
                    for (auto peer: peers_) {
                        if (peer.second.match_index_ >= current_index) {
                            nodes_at_or_above_index++;
                        }
                    }

                    if ((amount_of_peers / nodes_at_or_above_index) == 1) {
                        commit_index_ = log_.GetLogSize();
                        // TODO: Send a message that we are ready to commit log to application
                    }
                }

                if (!response.success) {
                    // Alright lets try and redo the log!

                    // Need to update my peers in memory just in case something happens
                    // during message passaging.
                    peers_[response.from_node_id].match_index_ = response.match_index - 1;
                    peers_[response.from_node_id].next_log_idx_ = response.match_index;

                    // Going to get existing log entries.
                    auto log_entry = log_.GetEntryAt(peers_[response.from_node_id].match_index_);
                    auto prev_log_entry = log_.GetEntryAt(peers_[response.from_node_id].match_index_ - 1);

                    auto rpc = AppendEntriesRPC{
                        .term = log_entry.term,
                        .leader_id = my_leader_id_,
                        .prev_log_term = prev_log_entry.term,
                        .entries = {},
                        // Need to do an AppendEntriesRPC with our decremented match_index_
                        .prev_log_idx = peers_[response.from_node_id].match_index_ - 1,
                        .log_size = peers_[response.from_node_id].match_index_,
                        .dest = response.from_node_id,
                        .leader_commit = commit_index_,
                    };

                    append_entries_buffer_.push_back(rpc);
                } else if (response.match_index < log_.GetLogSize()) {
                    Replay(response.match_index, response.from_node_id);
                }
            }
            if (request_vote.has_value()) {
                spdlog::warn("As a leader I recieved a vote request from node={}, should respond and tell them I am leader.", request_vote.value().from_node_id);
            }
            if (request_vote_response.has_value()) {
                spdlog::warn("As a leader I recieved a vote response from node={}, should respond and tell them I am leader.", request_vote_response.value().from_node_id);
            }
            break;
        }
        case FOLLOWER: {
            if (request_vote.has_value()) {
                // TODO: Check vote information against myself, if all good return true
                auto vote_value = request_vote.value();
                auto vote_response_rpc = RequestVoteResponseRPC{
                    .from_node_id = my_id_,
                    .to_node_id = vote_value.from_node_id,
                    .term = my_term_,
                    .vote_granted = true,
                };

                if (vote_value.term < my_term_) {
                    // DO NOT VOTE TRUE
                    vote_response_rpc.vote_granted = false;
                }

                if (peers_[my_id_].vote_granted_) {
                    // DO NOT VOTE TRUE
                    vote_response_rpc.vote_granted = false;
                }

                if (vote_value.last_log_index < log_.GetPrevLogIdx()) {
                    // DO NOT VOTE TRUE
                    vote_response_rpc.vote_granted = false;
                }

                if (vote_response_rpc.vote_granted) {
                    peers_[my_id_].vote_granted_ = true;
                    my_term_ = vote_value.term;
                }

                spdlog::info("Sending vote response to buffer, granted={}", vote_response_rpc.vote_granted);
                request_vote_response_buffer_.push_back(vote_response_rpc);
            }
            if (app.has_value()) {
                const auto &append_entries = app.value();
                auto ok = log_.AppendEntries(
                    append_entries.term,
                    append_entries.leader_id,
                    append_entries.prev_log_idx,
                    append_entries.prev_log_term,
                    append_entries.entries,
                    append_entries.leader_commit
                );

                auto res = AppendEntriesResponseRPC{
                    .success = ok,
                    .from_node_id = my_id_,
                    .to_node_id = my_leader_id_,
                };

                if (ok) {
                    res.term = append_entries.term,
                            res.match_index = log_.GetLogSize();
                    commit_index_ = append_entries.leader_commit;
                } else {
                    res.term = append_entries.prev_log_term,
                            res.match_index = append_entries.prev_log_idx;
                }

                Send(std::nullopt, std::make_optional(res), std::nullopt);
            }
            break;
        }
        case CANDIDATE: {
            spdlog::info("Receiving message as CANDIDATE...");
            //TODO the case where a candidate receives a AppendEntries (Should turn it in to a follower)
            if (app.has_value()) {
                const auto &append_entries = app.value();
                SetState(FOLLOWER);
                my_leader_id_ = append_entries.leader_id;
                auto ok = log_.AppendEntries(
                    append_entries.term,
                    append_entries.leader_id,
                    append_entries.prev_log_idx,
                    append_entries.prev_log_term,
                    append_entries.entries,
                    append_entries.leader_commit
                );

                auto res = AppendEntriesResponseRPC{
                    .success = ok,
                    .from_node_id = my_id_,
                    .to_node_id = my_leader_id_,
                };

                if (ok) {
                    res.term = append_entries.term,
                            res.match_index = log_.GetLogSize();
                    commit_index_ = append_entries.leader_commit;
                } else {
                    res.term = append_entries.prev_log_term,
                            res.match_index = append_entries.prev_log_idx;
                }

                Send(std::nullopt, std::make_optional(res), std::nullopt);
                break;
            }

            if (request_vote.has_value()) {
                auto vote_value = request_vote.value();
                auto vote_response_rpc = RequestVoteResponseRPC{
                    .from_node_id = my_id_,
                    .to_node_id = vote_value.from_node_id,
                    .term = my_term_,
                    .vote_granted = false,
                };
                request_vote_response_buffer_.push_back(vote_response_rpc);
                break;
            }

            if (request_vote_response.has_value()) {
                int nodes_granted_vote = 0;
                auto vote_response = request_vote_response.value();
                assert(vote_response.to_node_id == my_id_);
                peers_[vote_response.from_node_id].vote_granted_ = vote_response.vote_granted;

                for (auto peer: peers_) {
                    if (peer.second.vote_granted_) {
                        nodes_granted_vote++;
                    }
                }

                if ((peers_.size() / nodes_granted_vote) == 1) {
                    HelloLeader(my_id_);
                    SetState(LEADER);

                    for (auto peer: peers_) {
                        // Should send a heartbeat to all peers
                        spdlog::warn("Sending heartbeat to peer = {}", peer.first);
                        Send({}, std::nullopt, std::nullopt);
                    }
                }
            }
            break;
        }
    }
}
