//this type of message is sent by the client to the leader server; this is a log message
//so the client write here all the information that he wants to save in the log and then 
//sends this message to leader server
message LogMessage {
    int clientAddress;
    char operandName;
    int operandValue;			// if serverToAdd >= 0, then it contains the serverNumber of the server to add
    char operation; 			// s == set, a == add, m == mul
    int serverToRemove = -1; 	// n == remove server n
    int serverToAdd = -1;		// n == add server n
    int serialNumber;
    int leaderAddress;
};