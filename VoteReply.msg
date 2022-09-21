//this message is use to indicate the serverIndex that is sending the 
//vote and the serverIndex that was voted on
message VoteReply {
    int commonServerIndex;
    int leaderIndex;
    int currentTerm;
    int voteGranted; //if it is zero the vote rejected; otherwise is granted
}