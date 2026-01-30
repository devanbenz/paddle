#include "raft_server.h"

#include <console_generated.h>
#include <random>
#include <spdlog/spdlog.h>

#include "raft_generated.h"

// TODO: Currently when a candidate becomes a follower the log doesn't persist
// the current term. I need to fix this issue because it's causing election failure.
RaftServer::RaftServer(RaftNode node) : node_(std::move(node)), listener_(nullptr), stream_(nullptr) {
}

// For debugging
void RaftServer::BecomeLeader() {
    spdlog::info("Becoming leader");
    node_.HelloLeader(node_.ID());
    node_.SetState(LEADER);
}

// For debugging
void RaftServer::BecomeFollower(int leader_id) {
    spdlog::info("Becoming follower for {}", leader_id);
    node_.HelloLeader(leader_id);
    node_.SetState(FOLLOWER);
}

ClientAcceptor RaftServer::Accept() {
    return listener_->Accept();
}

void RaftServer::Start() {
    std::atomic<bool> running{true};
    std::atomic<bool> is_leader{false};
    std::condition_variable condition;

    spdlog::info("Starting Raft Server...");
    spdlog::info("Node info: id: {}, curr_log_size: {}, curr_term: {}, curr_last_index: {}", node_.ID(), Node()->MyTerm(), Node()->GetLogBuffer().size(), Node()->GetLogBuffer().size() -1);
    auto my_config = node_.Config().GetOneByIndex(node_.ID());
    listener_ = new TcpListener(my_config.port);

    std::thread message_receiver([&] {
        while (running) {
            // DEBUGGING TO SEE CURRENT LOGS
                std::thread leader_msg([&running, this] {
                    while (running) {
                        sleep(10);
                        switch (Node()->State()) {
                            case LEADER:
                                spdlog::info("Node State: LEADER");
                                break;
                            case FOLLOWER:
                                spdlog::info("Node State: FOLLOWER");
                                break;
                            case CANDIDATE:
                                spdlog::info("Node State: CANDIDATE");
                                break;
                        }
                        if (Node()->GetLogBuffer().empty()) {
                            spdlog::info("Log: [ no entries ]");
                        }
                        for (auto log: Node()->GetLogBuffer()) {
                            spdlog::info("LogEntry: [ {}, {} ]", log.term, log.command);
                        }
                    }
                });
                leader_msg.detach();

            if (is_leader) {
                std::thread leader_msg([&running, this] {
                    while (running) {
                        if (Node()->State() != LEADER) {
                            return;
                        }
                        sleep(5);
                        spdlog::warn("Node {} is currently leader...", node_.ID());
                    }
                });
                leader_msg.detach();
                spdlog::info("I have become the leader. My ID {}", node_.ID());
                auto receiver = std::async(std::launch::async, [&]() {
                    while (running) {
                        spdlog::info("Waiting for new connection...");
                        auto acceptor = Accept();
                        clients.push_back(std::async(std::launch::async, [acc = std::move(acceptor), &my_config, &running, this]() mutable {
                            auto msg = acc.ReceiveBuffer();

                            latch_.lock();
                            RaftNode::AppendEntriesRPC append_entries_rpc;
                            RaftNode::AppendEntriesResponseRPC append_entries_response_rpc{};
                            RaftNode::RequestVoteRpc request_vote_rpc{};
                            RaftNode::RequestVoteResponseRPC request_vote_response_rpc{};
                            ReceiveMessageByType(msg, &append_entries_rpc, &append_entries_response_rpc,
                                                 &request_vote_rpc,
                                                 &request_vote_response_rpc);

                            if (!console_buffer_.empty()) {
                                for (int i = 0; i < console_buffer_.size(); ++i) {
                                    auto data = console_buffer_.back();
                                    console_buffer_.pop_back();
                                    acc.SendBuffer(data.first, data.second);
                                }
                            }
                            latch_.unlock();
                        }));
                    }
                });

                for (auto &handle: clients) {
                    handle.wait();
                }
            }
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dist(100, 500);

            while (!is_leader) {
                if (Node()->State() == LEADER) {
                    is_leader.store(true);
                    continue;
                }

                int timeout_ms = dist(gen);

                std::future<ClientAcceptor> future_acceptor = std::async(std::launch::async, [&]() {
                    return Accept();
                });

                std::future_status status = future_acceptor.wait_for(std::chrono::milliseconds(timeout_ms));

                if (status == std::future_status::timeout) {
                    spdlog::info("Accept() timed out after {} ms, becoming a candidate", timeout_ms);
                    TryElection();
                    spdlog::info("Election finished");
                    continue;
                }

                auto acceptor = future_acceptor.get();
                auto msg = acceptor.ReceiveBuffer();
                latch_.lock();
                RaftNode::AppendEntriesRPC append_entries_rpc;
                RaftNode::AppendEntriesResponseRPC append_entries_response_rpc{};
                RaftNode::RequestVoteRpc request_vote_rpc{};
                RaftNode::RequestVoteResponseRPC request_vote_response_rpc{};
                ReceiveMessageByType(msg, &append_entries_rpc, &append_entries_response_rpc, &request_vote_rpc,
                                     &request_vote_response_rpc);
                acceptor.CloseAcceptor();
                latch_.unlock();
            }
        }
    });

    std::thread message_producer([&] {
        while (running) {
            if (!Node()->GetEntriesBuffer().empty()) {
                for (int i = 0; i < Node()->GetEntriesBuffer().size(); ++i) {
                    latch_.lock();
                    auto rpc = Node()->GetEntriesBuffer().front();
                    Node()->GetEntriesBuffer().pop_front();
                    if (rpc.entries.empty()) {
                    } else {
                        for (auto entry: rpc.entries) {
                            spdlog::info("Sending entry = (term {}, command {})", entry.term, entry.command);
                        }
                    }
                    int total_size = 0;
                    auto buffer = RaftServer::PackageAppendEntries(rpc, total_size);


                    auto destination = Node()->Config().GetOneByIndex(rpc.dest);
                    latch_.unlock();
                    std::thread sender([buffer, destination, total_size, this]() {
                        if (total_size <= 0) {
                            spdlog::warn("Attempting to send a 0 length append_entry buffer from node {}",
                                         Node()->ID());
                            delete[] buffer;
                            return;
                        }
                        SendMessage(destination.id, buffer, total_size);
                    });
                    sender.detach();
                }
            }
            if (!Node()->GetVotesResponseBuffer().empty()) {
                for (int i = 0; i < Node()->GetVotesResponseBuffer().size(); ++i) {
                    latch_.lock();
                    auto rpc = Node()->GetVotesResponseBuffer().front();
                    Node()->GetVotesResponseBuffer().pop_front();
                    spdlog::info("I have a response to send, did this vote succeed? {}, sending to {}",
                                 rpc.vote_granted, rpc.to_node_id);

                    auto send_to = Node()->Config().GetOneByIndex(rpc.to_node_id);
                    int total_size = 0;
                    auto buffer = RaftServer::PackageRequestVoteResponse(rpc, total_size);
                    latch_.unlock();
                    std::thread sender([send_to, buffer, total_size, this]() {
                        if (total_size <= 0) {
                            spdlog::warn("Attempting to send a 0 length vote response buffer from node {}",
                                         Node()->ID());
                            delete[] buffer;
                            return;
                        }
                        SendMessage(send_to.id, buffer, total_size);
                    });
                    sender.detach();
                }
            }
        }
    });

    std::thread heartbeat_producer([&] {
        while (running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (is_leader.load()) {
                latch_.lock();
                Node()->Send(std::vector<std::string>({}), std::nullopt, std::nullopt);
                latch_.unlock();
            }
        }
    });

    message_producer.detach();
    heartbeat_producer.detach();
    message_receiver.join();
}

void RaftServer::TryElection() {
    std::atomic<bool> election(true);
    Node()->SetState(CANDIDATE);
    Node()->ResetVotes();
    Node()->MyTerm()++;
    for (auto &server: Node()->Config().GetAll()) {
        if (server.id != Node()->ID()) {
            auto vote = RaftNode::RequestVoteRpc{
                .from_node_id = Node()->ID(),
                .to_node_id = server.id,
                .term = Node()->MyTerm(),
            };
            spdlog::info("TryElection() triggered, sending a vote");
            Node()->Send(std::nullopt, std::nullopt, vote);
        }
    }

    std::thread election_vote_producer([&] {
        while (election) {
            if (Node()->State() == LEADER || Node()->State() == FOLLOWER) {
                election.store(false);
                spdlog::info("Became a leader or follower (producer)");
                return;
            }
            if (!Node()->GetVotesBuffer().empty()) {
                latch_.lock();
                auto vote = Node()->GetVotesBuffer().back();
                spdlog::info("voting from: {} to: {} term: {}", vote.from_node_id, vote.to_node_id, vote.term);
                Node()->GetVotesBuffer().pop_back();

                auto send_to = Node()->Config().GetOneByIndex(vote.to_node_id);

                int total_size = 0;
                auto request_buf = PackageRequestVote(vote, total_size);
                latch_.unlock();

                if (total_size <= 0) {
                    spdlog::warn("Attempting to send a 0 length vote buffer from {}", Node()->ID());
                    delete[] request_buf;
                    continue;
                }

                try {
                    TcpStream tmp_stream(send_to.address, send_to.port);
                    tmp_stream.Connect();
                    tmp_stream.SendBuffer(request_buf, total_size);
                } catch (const std::exception &e) {
                    spdlog::error("RaftServer::TryElection: {}", e.what());
                }
                delete[] request_buf;
            }
        }
    });

    std::thread election_vote_consumer([&] {
        while (election) {
            auto acceptor = Accept();
            auto msg = acceptor.ReceiveBuffer();
            RaftNode::AppendEntriesRPC append_entries_rpc;
            RaftNode::AppendEntriesResponseRPC append_entries_response_rpc{};
            RaftNode::RequestVoteRpc request_vote_rpc{};
            RaftNode::RequestVoteResponseRPC request_vote_response_rpc{};
            ReceiveMessageByType(msg, &append_entries_rpc, &append_entries_response_rpc, &request_vote_rpc,
                                 &request_vote_response_rpc);
            if (Node()->State() == LEADER) {
                spdlog::info("I AM A LEADER");
                election.store(false);
            } else if (Node()->State() == FOLLOWER) {
                spdlog::info("Stepped down to FOLLOWER during election");
                election.store(false);
            }
            acceptor.CloseAcceptor();
        }
    });

    election_vote_producer.join();
    election_vote_consumer.join();
}

/// Please see schema/raft.fbs for message schema information
uint8_t *RaftServer::PackageAppendEntries(RaftNode::AppendEntriesRPC rpc, int &total_size) {
    flatbuffers::FlatBufferBuilder builder(1024);

    std::vector<flatbuffers::Offset<RaftSchema::LogEntry> > fb_entries;

    fb_entries.reserve(rpc.entries.size());

    for (const auto &entry: rpc.entries) {
        auto command_string = builder.CreateString(entry.command);
        auto fb_entry = RaftSchema::CreateLogEntry(
            builder,
            entry.term,
            command_string
        );
        fb_entries.push_back(fb_entry);
    }

    auto entries_vector = builder.CreateVector(fb_entries);

    auto append_request = RaftSchema::CreateAppendEntriesRequest(
        builder,
        rpc.type,
        rpc.term,
        rpc.leader_id,
        rpc.prev_log_idx,
        rpc.prev_log_term,
        rpc.leader_commit,
        rpc.dest,
        entries_vector
    );
    auto message = RaftSchema::CreateMessage(
        builder,
        RaftSchema::MessageType_AppendEntriesRequest,
        append_request.Union()
    );

    builder.Finish(message);

    total_size = builder.GetSize();

    uint8_t *buffer_pointer = builder.GetBufferPointer();

    auto *return_buffer = new uint8_t[total_size];
    memcpy(return_buffer, buffer_pointer, total_size);

    return return_buffer;
}

RaftNode::AppendEntriesRPC RaftServer::UnpackageAppendEntries(std::vector<uint8_t> msg) {
    RaftNode::AppendEntriesRPC rpc;
    auto message = RaftSchema::GetMessage(msg.data());

    switch (message->message_type()) {
        case RaftSchema::MessageType_AppendEntriesRequest: {
            auto raw_rpc = message->message_as_AppendEntriesRequest();
            rpc.type = raw_rpc->type();
            rpc.term = raw_rpc->term();
            rpc.leader_id = raw_rpc->leader_id();
            rpc.prev_log_idx = raw_rpc->prev_log_idx();
            rpc.prev_log_term = raw_rpc->prev_log_term();
            rpc.leader_commit = raw_rpc->leader_commit();
            rpc.dest = raw_rpc->dest();
            std::vector<RaftLogBase::LogEntry> entries_vector;
            if (raw_rpc->entries()->empty()) {
                entries_vector = std::vector<RaftLogBase::LogEntry>();
            } else {
                entries_vector.reserve(raw_rpc->entries()->size());
                for (const auto &entry: *raw_rpc->entries()) {
                    auto new_entry = RaftLogInMem::NewLogEntry(entry->term(), entry->command()->str());
                    entries_vector.push_back(new_entry);
                }
            }
            rpc.entries = entries_vector;
            return rpc;
        }
        default:
            break;
    }
    return rpc;
}

uint8_t *RaftServer::PackageAppendEntriesResponse(RaftNode::AppendEntriesResponseRPC rpc, int &total_size) {
    flatbuffers::FlatBufferBuilder builder(1024);

    auto append_request = RaftSchema::CreateAppendEntriesResponse(
        builder,
        rpc.success,
        rpc.term,
        rpc.match_index,
        rpc.from_node_id,
        rpc.to_node_id
    );
    auto message = RaftSchema::CreateMessage(
        builder,
        RaftSchema::MessageType_AppendEntriesResponse,
        append_request.Union()
    );

    builder.Finish(message);

    total_size = builder.GetSize();

    uint8_t *buffer_pointer = builder.GetBufferPointer();

    uint8_t *return_buffer = new uint8_t[total_size];
    memcpy(return_buffer, buffer_pointer, total_size);

    return return_buffer;
}

RaftNode::AppendEntriesResponseRPC RaftServer::UnpackageAppendEntriesResponse(std::vector<uint8_t> msg) {
    RaftNode::AppendEntriesResponseRPC rpc;
    auto message = RaftSchema::GetMessage(msg.data());

    switch (message->message_type()) {
        case RaftSchema::MessageType_AppendEntriesResponse: {
            auto raw_rpc = message->message_as_AppendEntriesResponse();
            rpc.success = raw_rpc->success();
            rpc.term = raw_rpc->term();
            rpc.match_index = raw_rpc->match_index();
            rpc.from_node_id = raw_rpc->from_node_id();
            rpc.to_node_id = raw_rpc->to_node_id();
            return rpc;
        }
        default:
            break;
    }
    return rpc;
}

uint8_t *RaftServer::PackageRequestVote(RaftNode::RequestVoteRpc rpc, int &total_size) {
    flatbuffers::FlatBufferBuilder builder(1024);

    auto append_request = RaftSchema::CreateRequestVoteRequest(
        builder,
        rpc.from_node_id,
        rpc.to_node_id,
        rpc.term,
        rpc.last_log_index,
        rpc.last_log_term
    );
    auto message = RaftSchema::CreateMessage(
        builder,
        RaftSchema::MessageType_RequestVoteRequest,
        append_request.Union()
    );

    builder.Finish(message);

    total_size = builder.GetSize();

    uint8_t *buffer_pointer = builder.GetBufferPointer();

    uint8_t *return_buffer = new uint8_t[total_size];
    memcpy(return_buffer, buffer_pointer, total_size);

    return return_buffer;
}

RaftNode::RequestVoteRpc RaftServer::UnpackageRequestVote(std::vector<uint8_t> msg) {
    RaftNode::RequestVoteRpc rpc{};
    auto message = RaftSchema::GetMessage(msg.data());

    switch (message->message_type()) {
        case RaftSchema::MessageType_RequestVoteRequest: {
            auto raw_rpc = message->message_as_RequestVoteRequest();
            rpc.from_node_id = raw_rpc->from();
            rpc.to_node_id = raw_rpc->to();
            rpc.term = raw_rpc->term();
            rpc.last_log_index = raw_rpc->last_log_index();
            rpc.last_log_term = raw_rpc->last_log_term();
            rpc.vote_granted = true;
            return rpc;
        }
        default:
            break;
    }
    return rpc;
}

uint8_t *RaftServer::PackageRequestVoteResponse(RaftNode::RequestVoteResponseRPC rpc, int &total_size) {
    flatbuffers::FlatBufferBuilder builder(1024);

    auto append_request = RaftSchema::CreateRequestVoteResponse(
        builder,
        rpc.from_node_id,
        rpc.to_node_id,
        rpc.term,
        rpc.vote_granted
    );
    auto message = RaftSchema::CreateMessage(
        builder,
        RaftSchema::MessageType_RequestVoteResponse,
        append_request.Union()
    );

    builder.Finish(message);

    total_size = builder.GetSize();

    uint8_t *buffer_pointer = builder.GetBufferPointer();

    auto *return_buffer = new uint8_t[total_size];
    memcpy(return_buffer, buffer_pointer, total_size);

    return return_buffer;
}

RaftNode::RequestVoteResponseRPC RaftServer::UnpackageRequestVoteResponse(std::vector<uint8_t> msg) {
    RaftNode::RequestVoteResponseRPC rpc{};
    auto message = RaftSchema::GetMessage(msg.data());

    switch (message->message_type()) {
        case RaftSchema::MessageType_RequestVoteResponse: {
            auto raw_rpc = message->message_as_RequestVoteResponse();
            rpc.from_node_id = raw_rpc->from();
            rpc.to_node_id = raw_rpc->to();
            rpc.term = raw_rpc->term();
            rpc.vote_granted = raw_rpc->granted();
            return rpc;
        }
        default:
            break;
    }
    return rpc;
}

void RaftServer::SendMessage(int node_id, uint8_t *msg, int total_size) {
    auto config = node_.Config().GetOneByIndex(node_id);
    constexpr int max_retries = 5;

    for (int attempt = 0; attempt < max_retries; attempt++) {
        TcpStream conn(config.address, config.port);
        try {
            conn.Connect();
            uint32_t size = htonl(total_size);
            send(conn.GetSocket(), &size, sizeof(size), 0);
            send(conn.GetSocket(), msg, total_size, 0);
            delete[] msg;
            return;
        } catch (const std::system_error &e) {
            if (attempt < max_retries - 1) {
                sleep(1);
            }
        }
    }
    spdlog::error("SendMessage: failed to send to node {} after {} retries", node_id, max_retries);
    delete[] msg;
}

void RaftServer::ReceiveMessageByType(
    std::vector<uint8_t> msg,
    RaftNode::AppendEntriesRPC *append_entries_rpc = nullptr,
    RaftNode::AppendEntriesResponseRPC *append_entries_response_rpc = nullptr,
    RaftNode::RequestVoteRpc *request_vote_rpc = nullptr,
    RaftNode::RequestVoteResponseRPC *request_vote_response_rpc = nullptr
) {
    if (this->Node()->State() == CANDIDATE) {
        spdlog::info("Node is CANDIDATE and received a message: size={}", msg.size());
    }
    if (msg.empty() || msg.size() < sizeof(flatbuffers::uoffset_t)) {
        spdlog::error("Received invalid message buffer: size={}", msg.size());
        return;
    }

    auto message = RaftSchema::GetMessage(msg.data());
    if (!message) {
        spdlog::error("Failed to get message from buffer");
        return;
    }

    try {
        RaftSchema::MessageType message_type = message->message_type();


        switch (message_type) {
            case RaftSchema::MessageType_NONE:
                spdlog::debug("Received NONE message type");
                break;
            case RaftSchema::MessageType_AppendEntriesRequest:
                if (this->Node()->State() == CANDIDATE) {
                    spdlog::info("CANDIDATE message: type=AppendEntries");
                }
                if (append_entries_rpc) {
                    *append_entries_rpc = UnpackageAppendEntries(msg);
                    Node()->Receive(std::nullopt, *append_entries_rpc, std::nullopt, std::nullopt);
                } else {
                    spdlog::warn("Received AppendEntriesRequest but no output parameter provided");
                }
                break;
            case RaftSchema::MessageType_AppendEntriesResponse:
                if (append_entries_response_rpc) {
                    *append_entries_response_rpc = UnpackageAppendEntriesResponse(msg);
                    Node()->Receive(*append_entries_response_rpc, std::nullopt, std::nullopt, std::nullopt);
                } else {
                    spdlog::warn("Received AppendEntriesResponse but no output parameter provided");
                }
                break;
            case RaftSchema::MessageType_RequestVoteRequest:
                if (this->Node()->State() == CANDIDATE) {
                    spdlog::info("CANDIDATE message: type=RequestVoteRequest");
                }
                if (request_vote_rpc) {
                    *request_vote_rpc = UnpackageRequestVote(msg);
                    spdlog::info("Got a vote request");
                    Node()->Receive(std::nullopt, std::nullopt, *request_vote_rpc, std::nullopt);
                } else {
                    spdlog::warn("Received RequestVoteRequest but no output parameter provided");
                }
                break;
            case RaftSchema::MessageType_RequestVoteResponse:
                if (request_vote_response_rpc) {
                    *request_vote_response_rpc = UnpackageRequestVoteResponse(msg);
                    spdlog::info("Got a vote response");
                    Node()->Receive(std::nullopt, std::nullopt, std::nullopt, *request_vote_response_rpc);
                } else {
                    spdlog::warn("Received RequestVoteResponse but no output parameter provided");
                }
                break;
            case RaftSchema::MessageType_ConsoleRequest:
                if (Node()->State() == LEADER) {
                    ReceiveConsoleMessage(msg);
                }
                break;
            default:
                spdlog::warn("Unknown message type: {}", static_cast<int>(message_type));
                break;
        }
    } catch (const std::exception &e) {
        spdlog::error("Exception when processing message: {}", e.what());
    }
}

void RaftServer::ReceiveConsoleMessage(std::vector<uint8_t> msg) {
    auto console_req = RaftSchema::GetMessage(msg.data());
    if (std::strlen(console_req->message_as_ConsoleRequest()->command()->str().c_str()) > 0) {
        if (console_req->message_as_ConsoleRequest()->command()->str() == "exit") {
            flatbuffers::FlatBufferBuilder builder(512);
            auto command = builder.CreateString("Goodbye");
            auto request = RaftSchema::CreateConsoleResponse(builder, command);
            auto message = RaftSchema::CreateMessage(
                builder,
                RaftSchema::MessageType_ConsoleResponse,
                request.Union()
            );
            builder.Finish(message);


            uint8_t *buffer = builder.GetBufferPointer();
            int size = builder.GetSize();
            console_buffer_.push_back(std::make_pair(buffer, size));
        }
        if (console_req->message_as_ConsoleRequest()->command()->str() == "show log") {
            std::cout << "Current Log: [ ";
            for (auto log_entry: Node()->GetLogBuffer()) {
                std::cout << "{" << log_entry.term << ", " << log_entry.command << "} ";
            }
            std::cout << "]" << std::endl;
        }
    }

    auto entry = console_req->message_as_ConsoleRequest()->command()->str();
    Node()->Send(std::vector({std::move(entry)}), std::nullopt, std::nullopt);


    flatbuffers::FlatBufferBuilder builder(512);
    auto command = builder.CreateString("OK");
    auto request = RaftSchema::CreateConsoleResponse(builder, command);
    auto message = RaftSchema::CreateMessage(
        builder,
        RaftSchema::MessageType_ConsoleResponse,
        request.Union()
    );
    builder.Finish(message);


    uint8_t *buffer = builder.GetBufferPointer();
    int size = builder.GetSize();
    console_buffer_.push_back(std::make_pair(buffer, size));
}
