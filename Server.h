#include <stdio.h>
#include <string.h>
#include <omnetpp.h>
#include <algorithm>
#include <algorithm>
#include <list>
#include <random>
#include <sstream>
#include <chrono>
#include "VoteReply_m.h"
#include "VoteRequest_m.h"
#include "LogMessage_m.h"
#include "LogMessageResponse_m.h"
#include "HeartBeat_m.h"
#include "HeartBeatResponse_m.h"
#include "TimeOutNow_m.h"

using namespace omnetpp;
using std::vector;
using std::__cxx11::to_string;
using std::string;
using std::to_string;
using std::count;
using std::find;

#ifndef SERVER_H_
#define SERVER_H_

class Server : public cSimpleModule
{
    /*
     * red = server down;
     * bronze = server in follower state;
     * silver = server in candidate state;
     * gold = server in leader state;
     */
public:
    virtual ~Server();
private:
    // AUTOMESSAGES
    cMessage *electionTimeoutExpired; // autoMessage
    cMessage *heartBeatsReminder;     // if the leader receive this autoMessage it send a broadcast heartbeat
    cMessage *scheduleCrashMsg;
    cMessage *failureMsg;             // autoMessage to shut down this server
    cMessage *recoveryMsg;            // autoMessage to reactivate this server
    cMessage *applyChangesMsg;
    cMessage *leaderTransferFailed;
    cMessage *minElectionTimeoutExpired; // a server starts accepting new vote requests only after a minimum timeout from the last heartbeat reception
    cMessage *catchUpRoundTimeout;
    // MESSAGES
    VoteReply *voteReply;
    VoteRequest *voteRequest;
    HeartBeats *heartBeat;
    HeartBeatResponse *heartBeatResponse;
    LogMessage *logMessage;
    TimeOutNow *timeOutnow;

    enum stateEnum
    {
        FOLLOWER,
        CANDIDATE,
        LEADER,
        NON_VOTING_MEMBER
    };
    stateEnum serverState; // Current state (Follower, Leader or Candidate)
    vector<int> configuration;
    int numberVotingMembers;
    int networkAddress;
    int numServer;
    int numClient;
    bool acceptVoteRequest;

    /****** STATE MACHINE VARIABLES ******/
    int var_X;
    int var_Y;

    /****** Persistent state on all servers: ******/
    int currentTerm; // Time is divided into terms, and each term begins with an election. After a successful election, a single leader
    // manages the cluster until the end of the term. Some elections fail, in which case the term ends without choosing a leader.
    int lastVotedTerm;
    vector<log_entry> logEntries;
    client_requests_table requestTable;

    int leaderAddress;          // network address of the leader
    int numberVoteReceived = 0; // number of vote received by every server
    bool crashed = false;       // it's a boolean useful to shut down server/client
    bool leaderTransferPhase = false;
    bool timeOutNowSent = false;
    double serverCrashProbability;
    double maxCrashDelay;
    double maxCrashDuration;
    double minElectionTimeout;
    double maxElectionTimeout;
    double applyChangesPeriod;
    double heartbeatsPeriod;
    const int NO_CLIENT = -1;

    /****** Volatile state on all servers: ******/
    int commitIndex = -1; // index of highest log entry known to be committed (initialized to 0, increases monotonically)
    int lastApplied = -1; // index of highest log entry applied to state machine (initialized to 0, increases monotonically)

    /****** Volatile state on leaders (Reinitialized after election) ******/
    vector<int> nextIndex;  // for each server, index of the next log entry to send to that server (initialized to leader last log index + 1)
    vector<int> matchIndex; // for each server, index of highest log entry known to be replicated on server (initialized to 0, increases monotonically)

    /****** Cluster Membership Change ******/
    log_entry changingServerEntry;
    bool catchUpPhaseRunning = false;
    bool catchUpCountdownEnded = false;
    int catcUpPhaseRoundDuration;
    int catchUpRoundNumber;
    int maxNumberRound;
    int catchUpTargetIndex;

    // methods
    virtual void initialize() override;

    virtual void handleMessage(cMessage *msg) override;
    virtual void startNewElection(bool disruptPermitted);
    virtual void sendResponseToClient(int clientAddress, int serialNumber, bool succeded, bool redirect);
    virtual void updateState(log_entry log);
    virtual void acceptLog(int leaderAddress, int matchIndex);
    virtual void startAcceptVoteRequestCountdown();
    virtual void rejectLog(int leaderAddress);
    virtual void tryLeaderTransfer(int targetAddress);
    virtual void restartCountdown();
    virtual int min(int a, int b);
    virtual int getIndex(int addr);
    virtual void initializeConfiguration();
    virtual void refreshDisplay() const override;
    virtual void finish() override;
    virtual void stepdown(int newCurrentTerm);
    virtual void updateCommitIndexOnLeader();
    virtual int countGreaterOrEqual(int threshold);
    virtual void initializeRequestTable(int size);
    virtual last_req* getLastRequest(int clientAddr);
    virtual last_req* addNewRequestEntry(int clientAddr);
    virtual bool needsToBeProcessed(int serialNumber, int clientAddress);
    virtual void startMembershipChangeProcedure(log_entry changeMembershipEntry);
    virtual void endCatchUpRound();
public:
    virtual void configureServer(vector<int> initialConfiguration);
    virtual int getAddress();
};

Define_Module(Server);


#endif /* TEST_H_ */
