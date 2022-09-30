//the candidate server send this vote request to all other servers and indicates his index
message VoteRequest{
	bool disruptLeaderPermission = false;
    int candidateAddress;
    int currentTerm;
    int lastLogTerm;
    int lastLogIndex;
}
