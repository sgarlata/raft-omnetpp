// client notifies log reception 
message HeartbeatResponse {
    int term;
    int logIndex; // index of the entry in the leader's log
    bool succeded;
}