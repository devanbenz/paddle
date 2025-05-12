#include <gtest/gtest.h>
#include "../raft/raft_log.h"

TEST(RaftLogTest, SimpleTest) {
  auto raft_log = RaftLogInMem(RaftLogInMem::NewLogEntry(0, ""));
  auto entry1_ok = RaftLogInMem::NewLogEntry(1, "set x foo");
  auto entry2_ok = RaftLogInMem::NewLogEntry(1, "set foo bar");
  auto entry3_ok = RaftLogInMem::NewLogEntry(1, "set a 1");
  auto entries1_3 = std::vector({entry1_ok, entry2_ok, entry3_ok});
  EXPECT_TRUE(raft_log.AppendEntries(entry1_ok.term, 0, 0, 0, std::vector({entry1_ok}), 0));
  EXPECT_TRUE(raft_log.AppendEntries(entry2_ok.term, 0, 1, 1, std::vector({entry2_ok}), 0));
  EXPECT_TRUE(raft_log.AppendEntries(entry3_ok.term, 0, 2, 1, std::vector({entry3_ok}), 0));
  auto curr_entries = raft_log.GetEntries();
  EXPECT_EQ(curr_entries.size(), 4);

  // Bad term curr term is 1 this log has lower term
  auto entry1_bad = RaftLogInMem::NewLogEntry(0, "set foo bar");
  EXPECT_FALSE(raft_log.AppendEntries(entry1_bad.term, 0, 0, 0, std::vector({entry1_bad}), 0));
  // Term on the log has been increased (leader has done some reconciliation with it or something)
  auto entry1_good = RaftLogInMem::NewLogEntry(1, "set foo bar");
  EXPECT_TRUE(raft_log.AppendEntries(1, 0, 3, 1, std::vector({entry1_good}), 0));

  // Test case for when the previous log index does not exist in current buffer
  // We are on the third term now! This raft_log has 1 as its latest term; oh no!
  // We also have 2 more logs! This would be the seventh log in the system being sent.
  auto log_in_future = RaftLogInMem::NewLogEntry(3, "set foo bar");
  EXPECT_FALSE(raft_log.AppendEntries(log_in_future.term, 0, 6, 3, std::vector({log_in_future}), 0));

  // And so we do a log replay! This is a simulation.
  auto replay_1 = RaftLogInMem::NewLogEntry(2, "set foo bar");
  EXPECT_TRUE(raft_log.AppendEntries(replay_1.term, 0, 4, 1, std::vector({replay_1}), 0));
  auto replay_2 = RaftLogInMem::NewLogEntry(3, "set foo bar");
  EXPECT_TRUE(raft_log.AppendEntries(replay_2.term, 0, 5, 2, std::vector({replay_2}), 0));

  // Great--now we can submit this log.
  EXPECT_TRUE(raft_log.AppendEntries(log_in_future.term, 0, 6, 3, std::vector({log_in_future}), 0));

  // Test will truncate log, I'm pretty sure I need to just return true after truncating and inserting the record
  auto truncate_log = RaftLogInMem::NewLogEntry(3, "set foo bar");
  EXPECT_TRUE(raft_log.AppendEntries(truncate_log.term, 0, 3, 1, std::vector({truncate_log}), 0));

  // should go from 8 -> 4 entries
  EXPECT_EQ(raft_log.GetEntries().size(), 4);
}