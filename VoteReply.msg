//this message is use to indicate the serverIndex that is sending the 
//vote and the serverIndex that was voted on
message VoteReply {
    int voterAddress;
    int leaderAddress;
    int currentTerm;
}