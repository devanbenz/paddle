#### Server properties
- Can be in one of 3 states
  - leader
  - follower
  - candidate
- Followers receive no requests (client must know who leader is?? Or requests get redirected)
#### Terms
- Time is divided in to terms of arbitrary length
- Each term begins with an election and then proceeds with normal operation
- If a leader has an out of date term it reverts to a follower
- If a follower has an out of date term it updates it's term

Per leaders and terms I believe that each server needs to persist the following (so far):
- list of nodes (leader + followers)
- current term (as it is known by this servers state)

#### RPCs
- RequestVote RPC
- AppendEntries RPC
  - Used as heartbeat as well (contains no log entries)
- 
