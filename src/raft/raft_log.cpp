#include "raft_log.h"

#include "spdlog/spdlog.h"

// Reasons append entries could return false
// entry term is

bool RaftLogInMem::AppendEntries(int term, int leader_id, int prev_log_idx, int prev_log_term, std::vector<LogEntry> entries, int leader_commit) {
    // if the current term on this machine is less than the entry
    if (term < curr_term_) {
        spdlog::warn("term [{}] < curr_term_ [{}]", term, curr_term_);
        return false;
    }

    if (buffer.size() < prev_log_idx || buffer.size() ==  0) {
        return false;
    }

    // TODO: This currently segfaults
    spdlog::warn("buffer size: {}", buffer.size());
    for (auto entry : buffer) {
        spdlog::warn("current buffer: term={}", buffer.size(), entry.term);
    }
    if (buffer[prev_log_idx].term != prev_log_term) {
        spdlog::warn("buffer[prev_log_idx].term [{}] != prev_log_term [{}]", buffer[prev_log_idx].term, prev_log_term);
        return false;
    }

    // So now we need to check for existing entries (index + term pair)
    if (curr_prev_log_idx_ > prev_log_idx && buffer[prev_log_idx].term == term) {
        return true;
    }

    // Okay, but what if we have a conflict! Truncation time.
    if (curr_prev_log_idx_ > prev_log_idx && buffer[prev_log_idx].term != term) {
        Truncate(prev_log_idx);
    }

    for (const auto& entry : entries) {
        buffer.push_back(entry);
    }
    size_ = buffer.size();
    curr_term_ = term;
    curr_leader_id_ = leader_id;
    curr_prev_log_idx_ = int(buffer.size() - 2) <= 0 ? buffer.size() - 2 : 0;
    return true;
}

void RaftLogInMem::Truncate(int offset) {
    int index = 0;
    std::vector<LogEntry> new_vec;
    for (auto entry : buffer) {
        if (index >= offset) {
            break;
        }
        new_vec.push_back(entry);
        index++;
    }
    buffer = std::move(new_vec);
}

int RaftLogInMem::GetTerm() {
    return curr_term_;
}

int RaftLogInMem::GetPrevLogIdx() {
    return buffer.size() - 1;
}

int RaftLogInMem::GetPrevLogTerm() {
    auto prev_log_idx = buffer.size() - 1;
    return buffer[prev_log_idx].term;
}

int RaftLogInMem::GetLogSize() {
    return buffer.size();
}

void RaftLogInMem::Commit() {}

void RaftLogInMem::Restore() {}

RaftLogBase::LogEntry RaftLogInMem::GetEntryAt(int offset) {
    return buffer[offset];
}
