//the leader server send to all other server this message to communicate 
//that he is the server and that he is alive.
// AppendEntriesRPC
cplusplus{{
	#include "structs.h"
}};

struct log_entry{
	@existingClass;
};

message HeartBeats {
    int leaderAddress;
    int destAddress;
    int leaderCurrentTerm;
    int leaderCommit;
    int prevLogIndex;
    int prevLogTerm;	// term of prevLogIndex entry
    // log information can be appended to heartbeat messages (entry)
    log_entry entry;
}
