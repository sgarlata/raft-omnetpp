// client notifies log reception 
message HeartBeatResponse {
    int term;
    int matchIndex; // index of the entry in the leader's log
    bool succeded;
}