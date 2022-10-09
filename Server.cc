/*
 * server.cc
 *
 *  Created on: 13 mar 2022
 *      Author: ste_dochio
 */
#include <stdio.h>
#include <string.h>
#include <omnetpp.h>
#include <algorithm>
#include "Ping_m.h"
#include <algorithm>
#include <list>
#include <random>
#include <sstream>
#include <chrono>
#include "Ping_m.h"
#include "LeaderElection_m.h"
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
    cMessage *failureMsg;             // autoMessage to shut down this server
    cMessage *recoveryMsg;            // autoMessage to reactivate this server
    cMessage *applyChangesMsg;
    cMessage *leaderTransferFailed;
    cMessage *minElectionTimeoutExpired; // a server starts accepting new vote requests only after a minimum timeout from the last heartbeat reception
    cMessage *catchUpPhaseCountDown;
    cMessage *shutDownDeletedServer;
    cMessage *channelLinkProblem; // when i receive this message i remove/restore some link

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
    int serverNumber;
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
    double maxDeathDuration;
    int activationCrashLink;
    cModule *Switch, *serverToDelete;
    cGate *newServerPortIN, *newServerPortOUT;
    cGate *newSwitchPortIN, *newSwitchPortOUT;
    cModule *tempClient;
    const int NO_CLIENT = -1;

    /****** Volatile state on all servers: ******/
    int commitIndex = -1; // index of highest log entry known to be committed (initialized to 0, increases monotonically)
    int lastApplied = -1; // index of highest log entry applied to state machine (initialized to 0, increases monotonically)

    /****** Volatile state on leaders (Reinitialized after election) ******/
    vector<int> nextIndex;  // for each server, index of the next log entry to send to that server (initialized to leader last log index + 1)
    vector<int> matchIndex; // for each server, index of highest log entry known to be replicated on server (initialized to 0, increases monotonically)

    /****** Catching up phase ******/
    bool catchUpPhaseRunning = false;
    int newServerAddress; // server to catch up
    int catchUpRound;
    int maxNumberRound;

protected:
    // TXC15 E TXC16 SERVONO INVECE A FINI STATISTICI, GUARDARE QUESTI DUE ESEMPI PER CAPIRE
    // COME COLLEZIONARE DATI DURANTE LA LIVE SIMULATION

    virtual void initialize() override;
    virtual void handleMessage(cMessage *msg) override;
    virtual void sendResponseToClient(int clientAddress, int serialNumber, bool succeded, bool redirect);
    virtual void updateState(log_entry log);
    virtual void acceptLog(int leaderAddress, int matchIndex);
    virtual void startAcceptVoteRequestCountdown();
    virtual void rejectLog(int leaderAddress);
    virtual void tryLeaderTransfer(int targetAddress);
    virtual void restartCountdown();
    virtual int min(int a, int b);
    virtual void initializeConfiguration();
    virtual void deleteServer();
    virtual void createServer();
    virtual void refreshDisplay() const override;
    virtual void finish() override;
    virtual void scheduleNextCrash();
    virtual void stepdown(int newCurrentTerm);
    virtual void updateCommitIndexOnLeader();
    virtual int countGreaterOrEqual(int threshold);
    virtual void initializeRequestTable(int size);
    virtual last_req* getLastRequest(int clientAddr);
    virtual last_req* addNewRequestEntry(int clientAddr);
    virtual bool needsToBeProcessed(int serialNumber, int clientAddress);

};

Define_Module(Server);

// Destructor
Server::~Server()
{
    cancelAndDelete(failureMsg);
    cancelAndDelete(recoveryMsg);
    cancelAndDelete(electionTimeoutExpired);
    cancelAndDelete(heartBeatsReminder);
    cancelAndDelete(applyChangesMsg);
    cancelAndDelete(leaderTransferFailed);
    cancelAndDelete(minElectionTimeoutExpired);
    cancelAndDelete(catchUpPhaseCountDown);

    /*
  for (int i = 0; i < appendEntryTimers.size() ; i++){
    cancelAndDelete(appendEntryTimers[i].timeoutEvent);
  }
  for (int i = 0; i < installSnapshotTimers.size() ; i++){
    cancelAndDelete(installSnapshotTimers[i].timeoutEvent);
  }
     */
}

void Server::initialize()
{
    WATCH(crashed);
    WATCH(commitIndex);
    WATCH(lastApplied);
    WATCH(currentTerm);
    WATCH(serverNumber);
    WATCH_VECTOR(configuration);
    WATCH(leaderAddress);
    WATCH_VECTOR(nextIndex);
    WATCH_VECTOR(matchIndex);
    WATCH(logEntries);
    WATCH(var_X);
    WATCH(var_Y);
    numClient = getParentModule()->par("numClient");
    serverCrashProbability = getParentModule()->par("serverCrashProbability");
    maxCrashDelay = getParentModule()->par("serverMaxCrashDelay");
    maxDeathDuration = getParentModule()->par("serverMaxCrashDuration");
    currentTerm = 1; // or 1
    // alreadyVoted = false;
    lastVotedTerm = 0;
    serverState = FOLLOWER;
    cDisplayString &dispStr = getDisplayString();
    dispStr.parse("i=device/server2,bronze");
    numberVoteReceived = 0;
    acceptVoteRequest = true;
    leaderAddress = -1;
    networkAddress = gate("gateServer$i", 0)->getPreviousGate()->getIndex();
    serverNumber = networkAddress;
    Switch = gate("gateServer$i", 0)->getPreviousGate()->getOwnerModule();
    var_X = 1;
    var_Y = 1;
    initializeConfiguration();
    initializeRequestTable(4);
    numberVotingMembers = configuration.size();

    for (int i = 0; i < configuration.size(); ++i)
    {
        nextIndex.push_back(0);
        matchIndex.push_back(-1);
    }

    // We define a probability of death and we start a self message that will "shut down" some nodes
    double crashProbabilitySample = uniform(0, 1);
    if (crashProbabilitySample < serverCrashProbability)
    {
        double randomDelay = uniform(1, maxCrashDelay);
        failureMsg = new cMessage("failureMsg");
        EV
        << "Here is server[" + to_string(serverNumber) + "]: I will be dead in " + to_string(randomDelay) + " seconds...\n";
        scheduleAt(simTime() + randomDelay, failureMsg);
    }
    minElectionTimeoutExpired = new cMessage("MinElectionTimeoutExpired");
    // here expires the first timeout; so the first server with timeout expired sends the first leader election message
    electionTimeoutExpired = new cMessage("ElectionTimeoutExpired");
    double randomTimeout = uniform(0.50, 1);
    scheduleAt(simTime() + randomTimeout, electionTimeoutExpired);

    applyChangesMsg = new cMessage("ApplyChangesToFiniteStateMachine");
    scheduleAt(simTime() + 1, applyChangesMsg);

    shutDownDeletedServer = new cMessage("shutDown");
    activationCrashLink = getParentModule()->par("activationlinkCrashServer");
    if (activationCrashLink == 1){
        channelLinkProblem = new cMessage("channel connection problem begins");
        double random = uniform(0, 6);
        scheduleAt(simTime() + random, channelLinkProblem);
    }


}

void Server::handleMessage(cMessage *msg)
{
    VoteReply *voteReply = dynamic_cast<VoteReply *>(msg);
    VoteRequest *voteRequest = dynamic_cast<VoteRequest *>(msg);
    HeartBeats *heartBeat = dynamic_cast<HeartBeats *>(msg);
    HeartBeatResponse *heartBeatResponse = dynamic_cast<HeartBeatResponse *>(msg);
    LogMessage *logMessage = dynamic_cast<LogMessage *>(msg);
    TimeOutNow *timeoutLeaderTransfer = dynamic_cast<TimeOutNow *>(msg);

    // ############################################### RECOVERY BEHAVIOUR ###############################################

    if(msg == failureMsg){
        bubble("i'm dead");
        cDisplayString &dispStr = getDisplayString();
        dispStr.parse("i=device/server2,red");
        crashed = true;
        double randomFailureTime = uniform(5, maxDeathDuration);
        EV
        << "\nServer ID: [" + to_string(this->getIndex()) + "] is dead for about: [" + to_string(randomFailureTime) + "]\n";
        recoveryMsg = new cMessage("recoveryMsg");
        scheduleAt(simTime() + randomFailureTime, recoveryMsg);
    }

    if (msg == shutDownDeletedServer){
        //here the server has left the current configuration
        currentTerm = -1; // or 1
        // alreadyVoted = true;
        serverState = FOLLOWER;
        cDisplayString &dispStr = getDisplayString();
        dispStr.parse("i=device/server2,blue");
        numberVoteReceived = 0;
        acceptVoteRequest = false;
        leaderAddress = -1;
        failureMsg = nullptr;
        minElectionTimeoutExpired = nullptr;
        recoveryMsg = nullptr;
        electionTimeoutExpired = nullptr;
        applyChangesMsg = nullptr;
        heartBeatsReminder = nullptr;
        leaderTransferFailed = nullptr;
        catchUpPhaseCountDown = nullptr;

    }

    else if (msg == recoveryMsg)
    {
        crashed = false;
        EV << "Here is server[" + to_string(serverNumber) + "]: I am no more dead... \n";
        bubble("im returned alive");
        cDisplayString &dispStr = getDisplayString();
        dispStr.parse("i=device/server2,bronze");
        // if this server returns alive it has to be a follower because there might be another server serving as the leader
        serverState = FOLLOWER;
        numberVoteReceived = 0;
        acceptVoteRequest = true;
        // restart election count-down
        electionTimeoutExpired = new cMessage("NewElectionTimeoutExpired");
        double randomTimeout = uniform(2, 4);
        scheduleAt(simTime() + randomTimeout, electionTimeoutExpired);
        // restart the periodical updates of the FSM
        applyChangesMsg = new cMessage("Apply changes to State Machine");
        scheduleAt(simTime() + 1, applyChangesMsg);
        // here i kill again servers in order to have a simulations where all server continuously go down
        scheduleNextCrash();
    }

    else if (crashed)
    {
        EV << "SERVER CRASHED: cannot react to messages";
    }

    // ################################################ NORMAL BEHAVIOUR ################################################
    else if (crashed == false)
    {
        if(msg == channelLinkProblem){
            int decision = intuniform(0,1);//if decision is 1 i restore a random link; if it is 0 i delete a random link
            int gateINOUT = intuniform(0,1);// if gateINOUT is 1 i delete the in gate; else i delete the out gate
            if (decision == 0){//here i delete a link
                if(gateINOUT == 0){// i delete an out gate
                    if(gate("gateServer$o", 0)->isConnected())
                        gate("gateServer$o", 0)->disconnect();
                }else{//i delete the in gate
                    if(Switch->gate("gateSwitch$o",this->getIndex())->isConnected())
                        Switch->gate("gateSwitch$o",this->getIndex())->disconnect();
                }
            }else{//here i restore a link with the same method of the delete
                if(gateINOUT == 0){// i restore an out gate
                    if(!gate("gateServer$o", 0)->isConnected()){
                        cDelayChannel *delayChannelOUT = cDelayChannel::create("myChannel");
                        delayChannelOUT->setDelay(0.1);

                        this->gate("gateServer$o", 0)->connectTo(Switch->gate("gateSwitch$i",this->getIndex()), delayChannelOUT);
                    }
                }
                else{ //i restore the in gate
                    if(!(Switch->gate("gateSwitch$o",this->getIndex())->isConnected())){
                        cDelayChannel *delayChannelIN = cDelayChannel::create("myChannel");
                        delayChannelIN->setDelay(0.1);

                        Switch->gate("gateSwitch$o",this->getIndex())->connectTo(this->gate("gateServer$i", 0),delayChannelIN);
                    }
                }
            }
            channelLinkProblem = new cMessage("another channel connection problem");
            double randomDelay = uniform(0, 3);
            scheduleAt(simTime() + randomDelay, channelLinkProblem);
        }

        else if (msg == electionTimeoutExpired and serverState != LEADER)
        { // I only enter here if a new election has to be done
            bubble("timeout expired, new election start");
            cDisplayString &dispStr = getDisplayString();
            dispStr.parse("i=device/server2,silver");
            numberVoteReceived = 0;
            currentTerm++;
            serverState = CANDIDATE;
            numberVoteReceived++;      // it goes to 1, the server vote for himself
            // this->alreadyVoted = true; // each server can vote just one time per election; if the server is in a candidate state it vote for himself
            lastVotedTerm = currentTerm;
            // i set a new timeout range
            electionTimeoutExpired = new cMessage("NewElectionTimeoutExpired");
            double randomTimeout = uniform(0.75, 1.25);
            scheduleAt(simTime() + randomTimeout, electionTimeoutExpired);

            // send vote request to the switch, it will forward it
            VoteRequest *voteRequest = new VoteRequest("voteRequest");
            voteRequest->setCandidateAddress(networkAddress);
            voteRequest->setCurrentTerm(currentTerm);
            int lastLogIndex = logEntries.size() - 1;
            voteRequest->setLastLogIndex(lastLogIndex);
            if (!(lastLogIndex < 0))
            {
                voteRequest->setLastLogTerm(logEntries[lastLogIndex].entryTerm);
            }
            else
            {
                voteRequest->setLastLogTerm(0);
            }
            if (leaderTransferPhase)
            {
                voteRequest->setDisruptLeaderPermission(true);
            }
            if(gate("gateServer$o", 0)->isConnected())
                send(voteRequest, "gateServer$o", 0);
        }

        // TO DO: non voting members
        else if (voteRequest != nullptr && (acceptVoteRequest or voteRequest->getDisruptLeaderPermission()))
        {
            int candidateAddress = voteRequest->getCandidateAddress();
            int candidateTerm = voteRequest->getCurrentTerm();
            int candidateLastLogTerm = voteRequest->getLastLogTerm();
            int candidateLastLogIndex = voteRequest->getLastLogIndex();
            int thisLastLogIndex = logEntries.size() - 1;
            int thisLastLogTerm = 0;
            if (!(thisLastLogIndex < 0)) {
                thisLastLogTerm = logEntries[thisLastLogIndex].entryTerm;
            }
            bool candidateLogUpToDate = true;

            if (candidateTerm > currentTerm)
            {
                // STEPDOWN PROCEDURE
                stepdown(voteRequest->getCurrentTerm());
                if (leaderTransferPhase)
                {
                    cancelEvent(leaderTransferFailed);
                    leaderTransferPhase = false;
                    timeOutNowSent = false;
                }
            }

            if (candidateLastLogTerm < thisLastLogTerm
                    or (candidateLastLogTerm == thisLastLogTerm and candidateLastLogIndex < thisLastLogIndex))
            {
                candidateLogUpToDate = false;
            }

            if (candidateTerm == currentTerm && candidateTerm > lastVotedTerm && candidateLogUpToDate)
            {
                cancelEvent(electionTimeoutExpired);
                // i can grant up to 1 vote for each term
                lastVotedTerm = voteRequest->getCurrentTerm();
                // restart new election count-down
                electionTimeoutExpired = new cMessage("NewElectionTimeoutExpired");
                double randomTimeout = uniform(1, 2);
                scheduleAt(simTime() + randomTimeout, electionTimeoutExpired);
                // send positive vote reply
                bubble("vote reply: vote granted");
                VoteReply *voteReply = new VoteReply("voteReply");
                voteReply->setVoterAddress(networkAddress); // this is the id of the voting server
                voteReply->setLeaderAddress(candidateAddress);
                voteReply->setVoteGranted(1);
                voteReply->setCurrentTerm(currentTerm);
                if(gate("gateServer$o", 0)->isConnected())
                    send(voteReply, "gateServer$o", 0);
            }
            else
            {
                // send negative vote reply
                bubble("vote reply: NO");
                VoteReply *voteReply = new VoteReply("voteReply");
                voteReply->setVoterAddress(networkAddress); // this is the id of the voting server
                voteReply->setLeaderAddress(candidateAddress);
                voteReply->setVoteGranted(0);
                voteReply->setCurrentTerm(currentTerm);
                if(gate("gateServer$o", 0)->isConnected())
                    send(voteReply, "gateServer$o", 0);
            }
        }

        // TO DO: non voting members
        else if (voteReply != nullptr)
        { // here i received a vote so i increment the current term vote
            leaderTransferPhase = false;
            if (voteReply->getCurrentTerm() > currentTerm)
            {
                // STEPDOWN PROCEDURE
                stepdown(voteReply->getCurrentTerm());
            }

            if (voteReply->getCurrentTerm() == currentTerm && serverState == CANDIDATE)
            {
                numberVoteReceived = numberVoteReceived + voteReply->getVoteGranted();
                if (numberVoteReceived > numberVotingMembers / 2)
                {
                    // Majority is reached: i am the NEW LEADER
                    bubble("i'm the leader");
                    cDisplayString &dispStr = getDisplayString();
                    dispStr.parse("i=device/server2,gold");
                    // if a server becomes leader I have to cancel the timer for a new election since it will
                    // remain leader until the first failure, furthermore i have to reset all variables used in the election
                    cancelEvent(electionTimeoutExpired);
                    serverState = LEADER;
                    leaderAddress = networkAddress;
                    numberVoteReceived = 0;
                    for (int serverIndex = 0; serverIndex < nextIndex.size(); ++serverIndex)
                    {
                        nextIndex[serverIndex] = logEntries.size();
                        if (serverIndex != networkAddress)
                        {
                            matchIndex[serverIndex] = -1;
                        }
                        else
                        {
                            matchIndex[serverIndex] = logEntries.size() - 1;
                        }
                    }
                    scheduleNextCrash();
                    // add NOP to log
                    log_entry NOP;
                    NOP.clientAddress = NO_CLIENT;
                    NOP.entryTerm = currentTerm;
                    NOP.operandName = 'X';
                    NOP.operandValue = 0;
                    NOP.operation = 'A';
                    NOP.entryLogIndex = logEntries.size();
                    // update next index and match index for leader.
                    nextIndex[serverNumber]++;
                    matchIndex[serverNumber]++;
                    logEntries.push_back(NOP);

                    // periodical HeartBeat
                    heartBeatsReminder = new cMessage("heartBeatsReminder");
                    scheduleAt(simTime(), heartBeatsReminder);

                    // adding a new server
                    //createServer();
                    //deleteServer();
                }
            }
        }

        // HEARTBEAT RECEIVED (AppendEntries RPC)
        if (heartBeat != nullptr)
        {
            int logSize = logEntries.size();
            int lastLogIndex = logSize - 1;
            int term = heartBeat->getLeaderCurrentTerm();
            int prevLogIndex = heartBeat->getPrevLogIndex();
            int prevLogTerm = heartBeat->getPrevLogTerm();
            int leaderCommit = heartBeat->getLeaderCommit();
            int clientAddr = heartBeat->getEntry().clientAddress;
            bool condition2Satisfied = true;
            last_req* lastRequestFromClient;

            // ALL SERVERS: If RPC request or response contains term > currentTerm: set currentTerm = term, convert to follower
            if (term < currentTerm)
            {
                // @ensure LOG MATCHING PROPERTY
                // CONSISTENCY CHECK: (1) Reply false if term < currentTerm
                rejectLog(leaderAddress);
            }
            else
            {
                // CONDITION (1) satisfied
                currentTerm = term;
                cDisplayString &dispStr = getDisplayString();
                dispStr.parse("i=device/server2, bronze");
                serverState = FOLLOWER;
                numberVoteReceived = 0;
                // alreadyVoted = false;
                leaderAddress = heartBeat->getLeaderAddress();
                // (2) Reply false if log doesn't contain an entry at prevLogIndex...
                // whose term matches prevLogTerm
                // (2.a) Log is too short
                if (prevLogIndex > lastLogIndex)
                {
                    condition2Satisfied = false;
                }
                // (2.b) no entry at prevLogIndex whose term matches prevLogTerm
                if (logSize > 0 and prevLogIndex >= 0)
                {
                    // if logEntries is empty (size == 0) or prevLogTerm == -1 there is no need to deny
                    if (logEntries[prevLogIndex].entryTerm != prevLogTerm)
                    {
                        condition2Satisfied = false;
                    }

                }
                if (condition2Satisfied)
                {
                    // LOG IS ACCEPTED
                    leaderAddress = heartBeat->getLeaderAddress();
                    int newEntryIndex = heartBeat->getEntry().entryLogIndex;
                    // CASE A: heartbeat DOES NOT CONTAIN ANY ENTRY, the follower
                    //         replies to confirm consistency with leader's log
                    if (heartBeat->getEmpty())
                    {
                        if (leaderCommit > commitIndex)
                        {
                            commitIndex = min(leaderCommit, prevLogIndex); // no new entries in the message: we can guarantee consistency up to prevLogIndex
                        }
                        acceptLog(leaderAddress, prevLogIndex);
                    }
                    else
                    {
                        // CASE B: heartbeat delivers a new entry for follower's log
                        // @ensure CONSISTENCY WITH SEVER LOG UP TO prevLogIndex
                        // No entry at newEntryIndex, simply append the new entry
                        if (lastLogIndex < newEntryIndex)
                        {
                            logEntries.push_back(heartBeat->getEntry());
                        }
                        // @ensure (3): if an existing entry conflicts with a new one (same index but different terms),
                        //              delete the existing entry and all that follow it
                        // Conflicting entry at newEntryIndex, delete the last entries up to newEntryIndex, then append the new entry
                        else if (logEntries[newEntryIndex].entryTerm != term)
                        {
                            int to_erase = logSize - newEntryIndex;
                            logEntries.erase(logEntries.end() - to_erase, logEntries.end());
                            logEntries.push_back(heartBeat->getEntry());
                        }
                        // client request index = index of last the entry. Ignore NOPs
                        if (clientAddr != NO_CLIENT)
                        {
                            lastRequestFromClient = getLastRequest(clientAddr);
                            if(lastRequestFromClient == nullptr)
                            {
                                lastRequestFromClient = addNewRequestEntry(clientAddr);
                            }
                            lastRequestFromClient->lastArrivedIndex = logEntries.size() - 1;
                        }
                        // NOTE: if a replica receives the same entry twice, it simply ignores the second one and sends an ACK.
                        // @ensure (5) If leaderCommit > commitIndex, set commitIndex = min(leaderCommit, index of last new entry)
                        // *index of last
                        acceptLog(leaderAddress, newEntryIndex);
                        if (leaderCommit > commitIndex)
                        {
                            commitIndex = min(leaderCommit, newEntryIndex);
                        }
                    }
                }
                else
                {
                    rejectLog(leaderAddress);
                    restartCountdown();
                }
            }
            startAcceptVoteRequestCountdown();
            restartCountdown();
        }

        if (heartBeatResponse != nullptr)
        {
            int followerIndex = heartBeatResponse->getFollowerNumber();
            int followerLogLength = heartBeatResponse->getLogLength();
            int followerMatchIndex = heartBeatResponse->getMatchIndex();
            if (heartBeatResponse->getSucceded())
            {
                // heartBeat accepted
                matchIndex[followerIndex] = followerMatchIndex;
                nextIndex[followerIndex] = matchIndex[followerIndex] + 1;
                // else heartBeat accepted and log is up to date
            }
            else
            {
                // heartBeat rejected
                if (followerLogLength < nextIndex[followerIndex])
                {
                    nextIndex[followerIndex] = followerLogLength;
                }
                else
                {
                    if (nextIndex[followerIndex] > 0)
                    {
                        nextIndex[followerIndex] = nextIndex[followerIndex] - 1;
                    }
                }
            }
            updateCommitIndexOnLeader();
        }

        if (msg == minElectionTimeoutExpired)
        {
            acceptVoteRequest = true;
        }

        // APPLY CHANGES TO FSM BY EXECUTING OPERATIONS IN THE LOG
        if (msg == applyChangesMsg)
        {
            int applyNextIndex;
            if(lastApplied < commitIndex)
            {
                for (applyNextIndex = lastApplied + 1 ; commitIndex > lastApplied; applyNextIndex++)
                {
                    log_entry nextToApply = logEntries[applyNextIndex];
                    updateState(nextToApply);
                    // update table, but ignore the NOP
                    if(nextToApply.clientAddress != NO_CLIENT)
                        getLastRequest(nextToApply.clientAddress)->lastAppliedSerial = nextToApply.serialNumber;
                    lastApplied++;
                }
            }
            applyChangesMsg = new cMessage("Apply changes to State Machine");
            scheduleAt(simTime() + 1, applyChangesMsg);
        }

        // LOG MESSAGE REQUEST RECEIVED, it is ignored only if leader transfer process is going on
        if (logMessage != nullptr && !leaderTransferPhase && leaderAddress >= 0)
        {
            int serialNumber = logMessage->getSerialNumber();
            int clientAddress = logMessage->getClientAddress();
            bool alreadyReceived = false;
            last_req* lastReq = getLastRequest(clientAddress);

            if (lastReq != nullptr)
            {
                int index = lastReq->lastArrivedIndex;
                // Immediately send an ack if the request has already been committed
                if(logEntries[index].serialNumber >= serialNumber)
                {
                    if(index > commitIndex)
                    {
                        bubble("This request is already in the log, but it is still uncommitted");
                        sendResponseToClient(clientAddress, serialNumber, false, false);
                    }
                    else
                    {
                        bubble("This request has already been committed!");
                        sendResponseToClient(clientAddress, serialNumber, true, false);
                    }
                    alreadyReceived = true;
                }
            }
            else
            {
                // first message from the client: add an entry to the table
                lastReq = addNewRequestEntry(clientAddress);
            }

            if(!alreadyReceived)
            {
                // Redirect to leader in case the message is received by a follower.
                if (networkAddress != leaderAddress)
                {
                    sendResponseToClient(clientAddress, serialNumber, false, true);
                }
                else
                {
                    // once a log message is received a new log entry is added in the leader node
                    log_entry newEntry;
                    newEntry.clientAddress = logMessage->getClientAddress();
                    newEntry.entryTerm = currentTerm;
                    newEntry.operandName = logMessage->getOperandName();
                    newEntry.operandValue = logMessage->getOperandValue();
                    newEntry.operation = logMessage->getOperation();
                    newEntry.serialNumber = logMessage->getSerialNumber();
                    newEntry.entryLogIndex = logEntries.size();
                    // update next index and match index for leader.
                    nextIndex[serverNumber]++;
                    matchIndex[serverNumber]++;
                    logEntries.push_back(newEntry);
                    // update last received index
                    lastReq->lastArrivedIndex = newEntry.entryLogIndex;
                }
            }
        }

        // forced timeout due to leader transfer
        else if (timeoutLeaderTransfer != nullptr)
        {
            leaderTransferPhase = true;
            cancelEvent(electionTimeoutExpired);
            electionTimeoutExpired = new cMessage("NewElectionTimeoutExpired");
            scheduleAt(simTime(), electionTimeoutExpired);
        }
        // ABORT LEADER TRANSFER PROCESS
        else if (msg == leaderTransferFailed)
        {
            this->leaderTransferPhase = false;
            this->timeOutNowSent = false;
        }

        // SEND HEARTBEAT (AppendEntries RPC)
        if (msg == heartBeatsReminder)
        {
            int logSize = logEntries.size();
            int lastLogIndex = logSize - 1;
            int nextLogIndex;
            int followerAddr;
            for (int i = 0; i < configuration.size(); i++)
            {
                followerAddr = configuration[i];
                HeartBeats *RPCAppendEntriesMsg = new HeartBeats("i'm the leader");
                // to avoid message to client and self message
                if (followerAddr != this->networkAddress)
                {
                    nextLogIndex = nextIndex[followerAddr];
                    RPCAppendEntriesMsg->setLeaderAddress(networkAddress);
                    RPCAppendEntriesMsg->setDestAddress(followerAddr);
                    RPCAppendEntriesMsg->setLeaderCurrentTerm(currentTerm);
                    RPCAppendEntriesMsg->setLeaderCommit(commitIndex);
                    RPCAppendEntriesMsg->setPrevLogIndex(nextLogIndex - 1);
                    // leader's log not empty
                    if (nextLogIndex == 0 )
                    {
                        RPCAppendEntriesMsg->setPrevLogTerm(1);
                    }
                    else
                    {
                        RPCAppendEntriesMsg->setPrevLogTerm(logEntries[nextLogIndex - 1].entryTerm);
                    }

                    if (nextLogIndex <= lastLogIndex)
                    {
                        // follower's log needs an update
                        RPCAppendEntriesMsg->setEntry(logEntries[nextLogIndex]);
                        RPCAppendEntriesMsg->setEmpty(false);
                    }
                    // follower's log up to date
                    else if (leaderTransferPhase && !timeOutNowSent)
                    {
                        tryLeaderTransfer(followerAddr);
                    }
                    if(gate("gateServer$o", 0)->isConnected())
                        send(RPCAppendEntriesMsg, "gateServer$o", 0);
                }
            }

            heartBeatsReminder = new cMessage("heartBeatsReminder");
            double randomTimeout = uniform(0.1, 0.3);
            scheduleAt(simTime() + randomTimeout, heartBeatsReminder);
        }
    }
}


void Server::acceptLog(int leaderAddress, int matchIndex)
{
    HeartBeatResponse *reply = new HeartBeatResponse("Consistency check: OK");
    reply->setMatchIndex(matchIndex);
    reply->setTerm(currentTerm);
    reply->setSucceded(true);
    reply->setLeaderAddress(leaderAddress);
    reply->setFollowerNumber(serverNumber);
    if(gate("gateServer$o", 0)->isConnected())
        send(reply, "gateServer$o", 0);
}

void Server::rejectLog(int leaderAddress)
{
    HeartBeatResponse *reply = new HeartBeatResponse("Consistency check: FAIL");
    reply->setMatchIndex(-1);
    reply->setTerm(currentTerm);
    reply->setSucceded(false);
    reply->setLeaderAddress(leaderAddress);
    reply->setLogLength(logEntries.size());
    reply->setFollowerNumber(serverNumber);
    if(gate("gateServer$o", 0)->isConnected())
        send(reply, "gateServer$o", 0);
}

void Server::tryLeaderTransfer(int addr)
{
    TimeOutNow *timeOutLeaderTransfer = new TimeOutNow("TIMEOUT_NOW");
    timeOutLeaderTransfer->setDestAddress(addr);
    if(gate("gateServer$o", 0)->isConnected())
        send(timeOutLeaderTransfer, "gateServer$o", 0);
    timeOutNowSent = true;
    leaderTransferFailed = new cMessage("Leader transfer failed");
    scheduleAt(simTime() + 2, leaderTransferFailed);
}

void Server::sendResponseToClient(int clientAddress, int serialNumber, bool succeded, bool redirect)
{
    LogMessageResponse *resp = new LogMessageResponse("ACK");
    if (!succeded)
    {
        free(resp);
        if(redirect)
        {
            resp = new LogMessageResponse("REDIRECTION");
        }
        else
        {
            resp = new LogMessageResponse("WAIT");
        }
    }
    resp->setClientAddress(clientAddress);
    resp->setLogSerialNumber(serialNumber);
    resp->setLeaderAddress(leaderAddress);
    resp->setSucceded(succeded);
    resp->setRedirect(redirect);
    if(gate("gateServer$o", 0)->isConnected())
        send(resp, "gateServer$o", 0);
}

void Server::restartCountdown()
{
    cancelEvent(electionTimeoutExpired);
    electionTimeoutExpired = new cMessage("NewElectionTimeoutExpired");
    double randomTimeout = uniform(1, 2);
    scheduleAt(simTime() + randomTimeout, electionTimeoutExpired);
}

void Server::startAcceptVoteRequestCountdown()
{
    acceptVoteRequest = false;
    if (minElectionTimeoutExpired != nullptr)
    {
        cancelEvent(minElectionTimeoutExpired);
    }
    minElectionTimeoutExpired = new cMessage("minElectionCountodwn");
    scheduleAt(simTime() + 1, minElectionTimeoutExpired);
}

int Server::min(int a, int b)
{
    if (a < b)
        return a;
    else
        return b;
}

bool isGreaterOrEqual (int num) {
    return (num % 2) == 0;
}

// If there exists an N such that N > commitIndex, a majority of matchIndex[i] >= N,
// and log[N].term == currentTerm: set commitIndex = N
// TO DO: non voting members
void Server::updateCommitIndexOnLeader()
{
    int lastLogEntryIndex = logEntries.size() - 1;
    int counter = 0;
    int majority = numberVotingMembers / 2;
    int temp, serialNumber, clientAddr;

    if (lastLogEntryIndex > commitIndex)
    {
        for (int i = commitIndex + 1; i <= lastLogEntryIndex; i++)
        {
            temp = countGreaterOrEqual(i);
            if (temp > majority and logEntries[i].entryTerm == currentTerm and counter < majority)
            {
                for (int j = commitIndex + 1; j <= i; j++)
                {
                    commitIndex = j;
                    // here we update the hash table, registering the new commit
                    clientAddr = logEntries[j].clientAddress;
                    serialNumber = logEntries[j].serialNumber;
                    // but only if it is a real request from a client and not a NOP
                    if(clientAddr != NO_CLIENT)
                    {
                        sendResponseToClient(clientAddr, serialNumber, true, false);
                    }
                }
                // this additional variable allows to exit the loop earlier
                counter = counter + temp;
            }
        }
    }
}

int Server::countGreaterOrEqual(int threshold)
{
    int i;
    int count = 0;
    for (i = 0; i < matchIndex.size(); i++)
    {
        if(matchIndex[i] >= threshold)
        {
            count++;
        }
    }
    return count;
}

// This function checks whether a request is being processed twice
bool Server::needsToBeProcessed(int serialNumber, int clientAddress)
{
    if(clientAddress == NO_CLIENT)
        return false;
    last_req* lastReq = getLastRequest(clientAddress);
    if (lastReq->lastAppliedSerial < serialNumber)
        return true;
    return false;
}

void Server::updateState(log_entry log)
{
    int *variable;
    int logSerNum = log.serialNumber;
    int logClientAddr = log.clientAddress;

    if (needsToBeProcessed(logSerNum, logClientAddr))
    {
        // FSM variable choice
        if (log.operandName == 'X')
        {
            variable = &var_X;
        }
        else
        {
            variable = &var_Y;
        }

        // FSM variable update
        if (log.operation == 'S')
        {
            (*variable) = log.operandValue;
        }
        else if (log.operation == 'A')
        {
            (*variable) = (*variable) + log.operandValue;
        }
        else if (log.operation == 'M')
        {
            (*variable) = (*variable) * log.operandValue;
        }
    }
}

void Server::initializeConfiguration()
{
    cModule *Switch = gate("gateServer$i", 0)->getPreviousGate()->getOwnerModule();
    string serverString = "server";
    for (cModule::GateIterator iterator(Switch); !iterator.end(); iterator++)
    {
        cGate *gate = *iterator;
        int serverAddress = (gate)->getIndex();
        const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
        if (gate->isConnected())
        {
            if (name == serverString)
            {
                serverAddress = gate->getIndex();
                configuration.push_back(serverAddress);
            }
        }
    }
}

std::ostream& operator<<(std::ostream& stream, const vector<log_entry> logEntries)
{
    for (int index = 0; index < logEntries.size(); index++)
    {
        stream << "[I: "
                << logEntries[index].entryLogIndex
                << ",T:"
                << logEntries[index].entryTerm
                << ",VAR:"
                << logEntries[index].operandName
                << ",OP:"
                << logEntries[index].operation
                << ",VAL:"
                << logEntries[index].operandValue
                << "] ";
    }
    return stream;
}

void Server::deleteServer()
{
    int serverIndex;
    int gatesize = Switch->gateSize("gateSwitch$o");
    for (int i = 0; i < gatesize; i++)
    {
        // There is only one server to delete and disconnect port from the switch
        serverIndex = Switch->gate("gateSwitch$o", i)->getIndex();
        if (networkAddress == serverIndex)
        {
            serverToDelete = Switch->gate("gateSwitch$o", i)->getNextGate()->getOwnerModule();
            serverToDelete->gate("gateServer$o", 0)->disconnect();
            Switch->gate("gateSwitch$o", i)->disconnect();
            // Delete the Server
            serverToDelete->callFinish();
            scheduleAt(simTime(), shutDownDeletedServer);
            //serverToDelete->deleteModule();
        }
    }
}

void Server::createServer()
{
    Switch->setGateSize("gateSwitch", Switch->gateSize("gateSwitch$o") + 1);
    //this loop is useful to order the switch vector: all server at the beginning, all client at the end
    for(int i = numClient + numberVotingMembers; i  >numberVotingMembers ; i--){
        if(Switch->gate("gateSwitch$o", i - 1)->getNextGate() == nullptr){//doesn't exit the link from switch to client, so i create it

        }
        tempClient = Switch->gate("gateSwitch$o", i - 1)->getNextGate()->getOwnerModule();//this is the client with the highest index
        Switch->gate("gateSwitch$o", i - 1)->disconnect();
        if(tempClient->gate("gateClient$o", 0)->isConnected())
            tempClient->gate("gateClient$o", 0)->disconnect();

        cDelayChannel *delayChannelINclient = cDelayChannel::create("myChannel");
        cDelayChannel *delayChannelOUTclient = cDelayChannel::create("myChannel");
        delayChannelINclient->setDelay(0.1);
        delayChannelOUTclient->setDelay(0.1);
        tempClient->gate("gateClient$o", 0)->connectTo(Switch->gate("gateSwitch$i", i), delayChannelOUTclient);
        Switch->gate("gateSwitch$o",i)->connectTo(tempClient->gate("gateClient$i", 0),delayChannelINclient);
    }
    int index = gate("gateServer$o",0)->getNextGate()->size() - 1 - numClient;
    newSwitchPortIN = Switch->gate("gateSwitch$i", index);
    newSwitchPortOUT = Switch->gate("gateSwitch$o", index);
    bubble("Adding new server!");
    cModuleType *moduleType = cModuleType::get("Server");
    string i = to_string(index);
    string temp = "server[" + i + "]";
    const char *name = temp.c_str();
    cModule *module = moduleType->create(name, getSystemModule());

    cDelayChannel *delayChannelIN = cDelayChannel::create("myChannel");
    cDelayChannel *delayChannelOUT = cDelayChannel::create("myChannel");
    delayChannelIN->setDelay(0.1);
    delayChannelOUT->setDelay(0.1);

    module->setGateSize("gateServer", module->gateSize("gateServer$o") + 1);

    newServerPortIN = module->gate("gateServer$i",0);
    newServerPortOUT = module->gate("gateServer$o",0);

    newSwitchPortOUT->connectTo(newServerPortIN, delayChannelIN);
    newServerPortOUT->connectTo(newSwitchPortIN, delayChannelOUT);

    // create internals, and schedule it
    module->buildInside();
    module->callInitialize();
}

void Server::stepdown(int newCurrentTerm)
{
    cancelEvent(electionTimeoutExpired);
    cDisplayString &dispStr = getDisplayString();
    dispStr.parse("i=device/server2,bronze");
    currentTerm = newCurrentTerm;
    numberVoteReceived = 0;
    serverState = FOLLOWER;
    // alreadyVoted = false;
    electionTimeoutExpired = new cMessage("NewElectionTimeoutExpired");
    double randomTimeout = uniform(0.75, 1.25);
    scheduleAt(simTime() + randomTimeout, electionTimeoutExpired);
}

void Server::scheduleNextCrash()
{
    double crashProbabilitySample = uniform(0, 1);
    if (crashProbabilitySample < serverCrashProbability)
    {
        double randomDelay = uniform(1, maxCrashDelay);
        failureMsg = new cMessage("failureMsg");
        EV
        << "Here is client: I will crash in " + to_string(randomDelay) + " seconds...\n";
        scheduleAt(simTime() + randomDelay, failureMsg);
    }
}

void Server::initializeRequestTable(int size)
{
    vector<last_req> chainToAdd;
    for (int i = 0; i < size; i++)
    {
        requestTable.entries.push_back(chainToAdd);
    }
    requestTable.size = size;
}

last_req* Server::addNewRequestEntry(int clientAddr)
{
    int mod = clientAddr % requestTable.size;
    int newElementIndex = requestTable.entries[mod].size();

    last_req toAdd;
    toAdd.clientAddress = clientAddr;

    requestTable.entries[mod].push_back(toAdd);
    return &requestTable.entries[mod][newElementIndex];
}

last_req* Server::getLastRequest(int clientAddr)
{
    int mod = clientAddr % requestTable.size;
    int chainSize = requestTable.entries[mod].size();
    int index;
    for (index = 0; index < chainSize; index++)
    {
        if (requestTable.entries[mod][index].clientAddress == clientAddr)
        {
            return &requestTable.entries[mod][index];
        }
    }
    return nullptr;
}

void Server::refreshDisplay() const {
    char buf[120];
    string logEntriesFormat = "";
    int startIndex = lastApplied + 1;
    for (int index = startIndex; index < logEntries.size(); index++)
    {
        logEntriesFormat = logEntriesFormat
                + "[I: "
                + to_string(logEntries[index].entryLogIndex)
                + ",T:"
                + to_string(logEntries[index].entryTerm)
                + ",VAR:"
                + logEntries[index].operandName
                + ",OP:"
                + logEntries[index].operation
                + ",VAL:"
                + to_string(logEntries[index].operandValue)
                + "] \n";
    }
    logEntriesFormat = logEntriesFormat + "currentTerm: %ld\n commitIndex: %ld\n lastApplied: %ld\n X==%ld; Y==%ld";
    char * cstr = new char [logEntriesFormat.length() + 1];
    strcpy(cstr, logEntriesFormat.c_str());
    sprintf(buf, cstr, currentTerm, commitIndex, lastApplied, var_X, var_Y);
    getDisplayString().setTagArg("t", 0, buf);
}

void Server::finish()
{
    cancelAndDelete(failureMsg);
    cancelAndDelete(recoveryMsg);
    cancelAndDelete(electionTimeoutExpired);
    cancelAndDelete(heartBeatsReminder);
    cancelAndDelete(applyChangesMsg);
    cancelAndDelete(leaderTransferFailed);
    cancelAndDelete(minElectionTimeoutExpired);
    cancelAndDelete(catchUpPhaseCountDown);
}
