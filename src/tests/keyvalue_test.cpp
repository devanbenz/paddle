#include <gtest/gtest.h>
#include "../key_value/server.h"

TEST(KeyValueStore, TestAll) {
    std::remove("/home/devan/Documents/rafting_trip_2025/raft_2025_04/snapshot_test.bin");
    std::map<std::string, std::string> values;
    std::map<std::string, std::string> values2;
    values.insert(std::pair<std::string, std::string>("key1", "value1"));
    values.insert(std::pair<std::string, std::string>("key2", "value2"));
    values.insert(std::pair<std::string, std::string>("key3", "value3"));
    auto snapshotter = Snapshotter("/home/devan/Documents/rafting_trip_2025/raft_2025_04/snapshot_test.bin");
    snapshotter.snapshot(values);
    snapshotter.restore(values2);

    for (auto it = values.begin(); it != values.end(); it++) {
        auto values2_val = values2[it->first];
        EXPECT_EQ(it->second, values2_val);
    }
    std::remove("/home/devan/Documents/rafting_trip_2025/raft_2025_04/snapshot_test.bin");
}