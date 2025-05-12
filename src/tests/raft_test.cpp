#include <gtest/gtest.h>
#include "../raft/raft_server.h"

TEST(RaftNodeTest, LeaderTest) {
    struct TestCases {
        std::string command;
        int term;
        int prev_log_idx;
        int prev_log_term;
    };

    std::vector<TestCases> test_cases = {
        TestCases{.command = "set foo bar", .term = 1, .prev_log_idx = 0, .prev_log_term = 0},
        TestCases{.command = "set 1 2", .term = 1, .prev_log_idx = 1, .prev_log_term = 1},
        TestCases{.command = "get foo", .term = 1, .prev_log_idx = 2, .prev_log_term = 1},
        TestCases{.command = "get 1", .term = 1, .prev_log_idx = 3, .prev_log_term = 1},
    };

    RaftLogInMem log(RaftLogInMem::NewLogEntry(0, ""));
    auto config = RaftConfig();
    auto raft_leader = RaftNode(std::move(log), config, 0, 1, LEADER);

    raft_leader.Send(std::make_optional(std::vector<std::string>{"set foo bar"}), std::nullopt, std::nullopt);
    auto queue = raft_leader.GetEntriesBuffer();

    EXPECT_EQ(queue.size(), 1);
    EXPECT_EQ(queue.front().term, 1);
    EXPECT_EQ(queue.front().prev_log_idx, 0);
    EXPECT_EQ(queue.front().log_size, 1);

    raft_leader.Send(std::make_optional(std::vector<std::string>{"set 1 2"}), std::nullopt, std::nullopt);
    raft_leader.Send(std::make_optional(std::vector<std::string>{"get foo"}), std::nullopt, std::nullopt);
    raft_leader.Send(std::make_optional(std::vector<std::string>{"get 1"}), std::nullopt, std::nullopt);
    queue = raft_leader.GetEntriesBuffer();

    EXPECT_EQ(queue.size(), 4);
    for (auto &test_case: test_cases) {
        EXPECT_EQ(queue.front().term, test_case.term);
        EXPECT_EQ(queue.front().prev_log_idx, test_case.prev_log_idx);
        EXPECT_EQ(queue.front().prev_log_term, test_case.prev_log_term);
        EXPECT_EQ(test_case.command, test_case.command);
        queue.pop_front();
    }

    EXPECT_EQ(queue.size(), 0);
}

TEST(RaftNodeTest, LeaderReplicationTest) {
    struct TestCases {
        std::string command;
        int term;
        bool success;
        int match_index;
        int from_node_id;
        int to_node_id;
    };

    std::vector<TestCases> test_cases = {
        TestCases{
            .command = "set foo bar", .term = 2, .success = true, .match_index = 1, .from_node_id = 1, .to_node_id = 0
        },
        TestCases{
            .command = "set 1 2", .term = 2, .success = true, .match_index = 1, .from_node_id = 1, .to_node_id = 0
        },
        TestCases{
            .command = "get foo", .term = 2, .success = true, .match_index = 1, .from_node_id = 1, .to_node_id = 0
        },
        TestCases{.command = "get 1", .term = 2, .success = true, .match_index = 1, .from_node_id = 1, .to_node_id = 0},
        TestCases{.command = "get 2", .term = 2, .success = true, .match_index = 1, .from_node_id = 1, .to_node_id = 0},
        TestCases{
            .command = "get 3", .term = 2, .success = true, .match_index = 1, .from_node_id = 1, .to_node_id = 0
        },
    };

    RaftLogInMem leader_log(RaftLogInMem::NewLogEntry(1, ""));
    RaftLogInMem follower_log(RaftLogInMem::NewLogEntry(1, ""));
    auto config = RaftConfig();
    auto raft_leader = RaftNode(std::move(leader_log), config, 0, 2, LEADER);
    auto raft_follower = RaftNode(std::move(follower_log), config, 1, 2, FOLLOWER);
    raft_follower.HelloLeader(raft_leader.ID());
    raft_leader.HelloLeader(raft_leader.ID());

    for (const auto &test_case: test_cases) {
        raft_leader.Send(std::make_optional(std::vector<std::string>{test_case.command}), std::nullopt, std::nullopt);
        auto rpc = raft_leader.GetEntriesBuffer().front();
        raft_leader.GetEntriesBuffer().pop_front();
        raft_follower.Receive(std::nullopt, rpc, std::nullopt, std::nullopt);
        auto res_rpc = raft_follower.GetResponsBuffer().front();
        raft_follower.GetResponsBuffer().pop_front();
        EXPECT_EQ(res_rpc.term, test_case.term);
        EXPECT_EQ(res_rpc.success, true);
        EXPECT_EQ(res_rpc.from_node_id, test_case.from_node_id);
        EXPECT_EQ(res_rpc.to_node_id, test_case.to_node_id);
        raft_leader.Receive(res_rpc, std::nullopt, std::nullopt, std::nullopt);
    }


    // I'm going to add these log entries to my leader but not my follower
    std::vector<TestCases> test_cases_two = {
        TestCases{
            .command = "set bin baz", .term = 2, .success = true, .match_index = 1, .from_node_id = 1, .to_node_id = 0
        },
        TestCases{
            .command = "set zz yy", .term = 2, .success = true, .match_index = 1, .from_node_id = 1, .to_node_id = 0
        },
        TestCases{
            .command = "get zz", .term = 2, .success = true, .match_index = 1, .from_node_id = 1, .to_node_id = 0
        },
        TestCases{
            .command = "get bin", .term = 2, .success = true, .match_index = 1, .from_node_id = 1, .to_node_id = 0
        },
    };

    for (const auto &test_case: test_cases_two) {
        raft_leader.Send(std::make_optional(std::vector<std::string>{test_case.command}), std::nullopt, std::nullopt);
        // The follower is offline so right now we're just popping messages off the deque
        auto rpc = raft_leader.GetEntriesBuffer().front();
        raft_leader.GetEntriesBuffer().pop_front();
    }

    raft_leader.Send(std::make_optional(std::vector<std::string>{"set abc 123"}), std::nullopt, std::nullopt);
    auto buf = raft_leader.GetEntriesBuffer().back();
    raft_leader.GetEntriesBuffer().pop_front();

    // This response should be false
    // The leader is currently 5 log entries ahead of the follower
    // The next block of test cases will rebuild the log on the follower

    // 1
    raft_follower.Receive(std::nullopt, buf, std::nullopt, std::nullopt);
    auto res = raft_follower.GetResponsBuffer().back();
    raft_follower.GetResponsBuffer().pop_front();
    EXPECT_EQ(res.term, 2);
    EXPECT_EQ(res.success, false);

    // 2
    raft_leader.Receive(res, std::nullopt, std::nullopt, std::nullopt);
    buf = raft_leader.GetEntriesBuffer().back();
    raft_leader.GetEntriesBuffer().pop_front();

    // 3
    raft_follower.Receive(std::nullopt, buf, std::nullopt, std::nullopt);
    res = raft_follower.GetResponsBuffer().back();
    raft_follower.GetResponsBuffer().pop_front();
    EXPECT_EQ(res.term, 2);
    EXPECT_EQ(res.success, false);

    raft_leader.Receive(res, std::nullopt, std::nullopt, std::nullopt);
    buf = raft_leader.GetEntriesBuffer().back();
    raft_leader.GetEntriesBuffer().pop_front();

    // Should succeed and log will replay
    raft_follower.Receive(std::nullopt, buf, std::nullopt, std::nullopt);
    res = raft_follower.GetResponsBuffer().back();
    raft_follower.GetResponsBuffer().pop_front();
    EXPECT_EQ(res.term, 2);
    EXPECT_EQ(res.success, true);

    raft_leader.Receive(res, std::nullopt, std::nullopt, std::nullopt);
    auto outgoing_entries = raft_leader.GetEntriesBuffer();

    for (int i = 0; i < outgoing_entries.size(); i++) {
        auto entry = outgoing_entries[i];
        raft_follower.Receive(std::nullopt, entry, std::nullopt, std::nullopt);
        raft_follower.GetResponsBuffer().pop_front();
    }

    // Logs should be the same now:
    auto leader_entries = raft_leader.GetLogBuffer();
    auto follower_entries = raft_follower.GetLogBuffer();

    for (int i = 0; i < leader_entries.size(); i++) {
        EXPECT_EQ(leader_entries[i].term, follower_entries[i].term);
        EXPECT_EQ(leader_entries[i].command, follower_entries[i].command);
    }

    EXPECT_EQ(raft_leader.MyCommitIndex(), raft_follower.MyCommitIndex());
}


TEST(RaftNodeTest, LeaderElectionTest) {
    auto config = RaftConfig(5, 10000);
    RaftLogInMem log_1(RaftLogInMem::NewLogEntry(1, ""));
    auto node_1 = RaftNode(std::move(log_1), config, 0, 2, FOLLOWER);
    RaftLogInMem log_2(RaftLogInMem::NewLogEntry(1, ""));
    auto node_2 = RaftNode(std::move(log_2), config, 1, 2, FOLLOWER);
    RaftLogInMem log_3(RaftLogInMem::NewLogEntry(1, ""));
    auto node_3 = RaftNode(std::move(log_3), config, 2, 2, FOLLOWER);
    RaftLogInMem log_4(RaftLogInMem::NewLogEntry(1, ""));
    auto node_4 = RaftNode(std::move(log_4), config, 3, 2, FOLLOWER);
    RaftLogInMem log_5(RaftLogInMem::NewLogEntry(1, ""));
    auto node_5 = RaftNode(std::move(log_5), config, 4, 2, FOLLOWER);

    std::vector node_list{&node_1, &node_2, &node_3, &node_4, &node_5};


    // Simulation for leader timeout and leader election occurring

    // Timeout has occurred and node_1 is a candidate
    node_1.SetState(CANDIDATE);

    // Best case scenario--node_1 sends out votes and gets everyone's vote
    for (auto node: config.GetAll()) {
        if (node.id != node_1.ID()) {
            auto request_vote = RaftNode::RequestVoteRpc{
                .from_node_id = node_1.ID(),
                .to_node_id = node.id,
                .last_log_index = 0,
                .last_log_term = 0,
                .vote_granted = true,
            };
            node_1.Send(std::nullopt, std::nullopt, request_vote);
        }
    }

    while (!node_1.GetVotesBuffer().empty()) {
        auto vote = node_1.GetVotesBuffer().front();
        node_1.GetVotesBuffer().pop_front();
        auto node = node_list[vote.to_node_id];
        node->Receive(std::nullopt, std::nullopt, vote, std::nullopt);
    }

    for (auto node: node_list) {
        if (node->ID() != node_1.ID()) {
            auto res = node->GetVotesResponseBuffer().front();
            node->GetVotesResponseBuffer().pop_front();
            node_1.Receive(std::nullopt, std::nullopt, std::nullopt, res);
        }
    }

    EXPECT_EQ(node_1.State(), LEADER);
    EXPECT_EQ(node_2.State(), FOLLOWER);
    EXPECT_EQ(node_3.State(), FOLLOWER);
    EXPECT_EQ(node_4.State(), FOLLOWER);
    EXPECT_EQ(node_5.State(), FOLLOWER);

    std::cout << "done";
}
