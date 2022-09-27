//the candidate server send this vote request to all other servers and indicates his index
message VoteRequest{
    int candidateAddress;
    int currentTerm;
}
