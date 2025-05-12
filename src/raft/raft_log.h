#pragma once
#include <cassert>
#include <filesystem>
#include <utility>
#include <vector>

class RaftLogBase {
public:
    struct LogEntry {
        int term;
        std::string command;
    } typedef LogEntry;

    RaftLogBase() = default;

    virtual ~RaftLogBase() = default;

    virtual bool AppendEntries(int term, int leader_id, int prev_log_idx, int prev_log_term, std::vector<LogEntry> entries, int leader_commit) = 0;

    [[nodiscard]] virtual std::vector<LogEntry> GetEntries() const = 0;

    virtual int GetTerm() = 0;

    virtual int GetPrevLogIdx() = 0;

    virtual int GetLogSize() = 0;

    virtual LogEntry GetEntryAt(int offset) = 0;

    virtual int GetPrevLogTerm() = 0;

    virtual void Commit() = 0;

    virtual void Restore() = 0;

    virtual void Truncate(int offset) = 0;
};

class RaftLogInMem : public RaftLogBase {
public:
    RaftLogInMem() = default;

    explicit RaftLogInMem(const LogEntry& entry) {
        buffer.push_back(entry);
        assert(buffer.size() == size_);
    };

    ~RaftLogInMem() override = default;

    RaftLogInMem(const RaftLogInMem&) = delete;

    RaftLogInMem(RaftLogInMem&& other)  noexcept {
        buffer = other.buffer;
        size_ = other.size_;
        curr_term_ = other.curr_term_;
        curr_prev_log_idx_ = other.curr_prev_log_idx_;
        curr_leader_id_ = other.curr_leader_id_;
    }

    static LogEntry NewLogEntry(int term, std::string command) {
        return {term, std::move(command)};
    }

    bool AppendEntries(int term, int leader_id, int prev_log_idx, int prev_log_term, std::vector<LogEntry> entries, int leader_commit) override;

    [[nodiscard]] std::vector<LogEntry> GetEntries() const override {
        return buffer;
    };

    void Truncate(int offset) override;

    int GetTerm() override;

    int GetPrevLogIdx() override;

    int GetLogSize() override;

    int GetPrevLogTerm() override;

    LogEntry GetEntryAt(int offset) override;

    // Unused for in memory
    void Commit() override;

    // Unused for in memory
    void Restore() override;
private:
    std::vector<LogEntry> buffer;
    int size_{1};
    int curr_term_{0};
    int curr_leader_id_{0};
    int curr_prev_log_idx_{1};
};

