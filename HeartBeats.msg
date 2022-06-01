//the leader server send to all other server this message to communicate 
//that he is the server and that he is alive.
message HeartBeats {
    int leaderIndex;
}