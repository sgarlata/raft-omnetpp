//this type of message is sent by the client to the leader server; this is a log message
//so the client write here all the information that he wants to save in the log and then 
//sends this message to leader server
message LogMessage {
    int clientId;
    int num;
    char operation;
}