#pragma once

#include "../../include/net.hpp"

#include <future>
#include <map>
#include <optional>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <shared_mutex>
#include <sstream>
#include <utility>

enum Operation {
    GET,
    SET,
    SNAP,
    REST
};

struct OperationExecution {
    Operation op{};
    std::optional<std::string> key;
    std::optional<std::string> value;
};

std::string get_op(OperationExecution op) {
    switch (op.op) {
        case GET:
            return "get";
        case SET:
            return "set";
        case SNAP:
            return "snapshot";
        case REST:
            return "restore";
    }
}

inline void set_key(std::map<std::string, std::string> &internal_store, const std::string &key, const std::string &value) {
    internal_store.insert(std::pair(key, value));
}

inline std::string get_value(std::map<std::string, std::string> &internal_store, const std::string &key) {
    auto it = internal_store.find(key);
    if (it == internal_store.end()) {
        return "not found";
    }
    return it->second;
}

inline OperationExecution parse_message(const std::string &message) {
    std::istringstream iss(message);
    std::string command, key, value;

    iss >> command;
    for (char &c: command) {
        c = toupper(c);
    }

    OperationExecution result;

    if (command == "GET") {
        result.op = GET;
        iss >> key;
        result.key = key;
        result.value = std::nullopt;
    } else if (command == "SET") {
        result.op = SET;
        iss >> key;
        iss >> value;
        result.key = key;
        result.value = value;
    } else if (command == "SNAPSHOT") {
        result.op = SNAP;
        result.key = std::nullopt;
        result.value = std::nullopt;
    } else {
        result.op = GET;
        result.key = "";
        result.value = std::nullopt;
    }

    return result;
}



// I think for snapshotting I may just do the sstable style approach where I just will append all keys.
// when reading from the snapshot I'll just start from the end--if a key has been seen then I'll skip it each time.
// Might add garbage collection/compaction in the future.
class Snapshotter {
public:
    explicit Snapshotter(std::filesystem::path path) : path_(std::move(path)) {
        open_file_.open(path_, std::ios::out | std::ios::in | std::ios::binary | std::ios::ate | std::ios::app);
    }

    void snapshot(std::map<std::string, std::string> &internal_store) {
        for (auto & it : internal_store) {
            size_t key_size = it.first.size();
            size_t value_size = it.second.size();
            open_file_.write(reinterpret_cast<char *>(&key_size), sizeof(size_t));
            open_file_.write(reinterpret_cast<char *>(&value_size), sizeof(size_t));
            open_file_.write(it.first.c_str(), key_size);
            open_file_.write(it.second.c_str(), value_size);
            open_file_.flush();
        }
    }

void restore(std::map<std::string, std::string> &internal_store) {
    internal_store.clear();
    open_file_.seekg(0, std::ios::beg);

    open_file_.seekg(0, std::ios::end);
    unsigned long file_size = open_file_.tellg();
    open_file_.seekg(0, std::ios::beg);

    unsigned long curr_pos = 0;

    while (curr_pos < file_size) {
        size_t key_size = 0;
        size_t value_size = 0;

        open_file_.read(reinterpret_cast<char *>(&key_size), sizeof(size_t));
        curr_pos += sizeof(size_t);

        open_file_.read(reinterpret_cast<char *>(&value_size), sizeof(size_t));
        curr_pos += sizeof(size_t);

        std::vector<char> key_buffer(key_size + 1, '\0');
        std::vector<char> value_buffer(value_size + 1, '\0');

        open_file_.read(key_buffer.data(), key_size);
        curr_pos += key_size;

        open_file_.read(value_buffer.data(), value_size);
        curr_pos += value_size;

        std::string key(key_buffer.data(), key_size);
        std::string value(value_buffer.data(), value_size);

        internal_store[key] = value;
    }
}
private:
    std::filesystem::path path_;
    std::fstream open_file_;
};

inline void run_command(
        OperationExecution data,
        std::map<std::string, std::string> &internal_store,
        const ClientAcceptor &acceptor,
        Snapshotter &snapshotter
) {
    switch (data.op) {
        case GET: {
            std::string value = get_value(
                    internal_store,
                    data.key.value());
            acceptor.SendMessage(value);
            break;
        }
        case SET:
            set_key(internal_store,
                    data.key.value(),
                    data.value.value());
            acceptor.SendMessage(
                    data.key.value());
            break;
        case SNAP:
            snapshotter.
                    snapshot(internal_store);
            acceptor.SendMessage(
                    "wrote snapshot");
            break;
        case REST:
            snapshotter.restore(internal_store);
            acceptor.SendMessage(
                    "restored from snapshot");
            break;
    }
}
