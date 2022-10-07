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
    bool alreadyVoted; // ID of the candidate that received vote in current term (or null if none)
    vector<log_entry> logEntries;

    int leaderAddress;          // network address of the leader
    int numberVoteReceived = 0; // number of vote received by every server
    bool iAmDead = false;       // it's a boolean useful to shut down server/client
    bool leaderTransferPhase = false;
    bool timeOutNowSent = false;
    int activationCrashLink;
    cModule *Switch, *serverToDelete;
    cGate *newServerPortIN, *newServerPortOUT;
    cGate *newSwitchPortIN, *newSwitchPortOUT;
    cModule *tempClient;

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
    virtual void redirectToLeader(int serialNumber, int clientAddress);
    virtual void sendACKToClient(int clientAddress, int serialNumber);
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
    virtual int getLastSerialNumberFromClient(int clientAddress);
    virtual void updateCommitIndexOnLeader();
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
    WATCH(iAmDead);
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
    serverNumber = (this)->getIndex();
    double realProbability = getParentModule()->par("serverDeadProbability");
    double maxDeathStart = getParentModule()->par("serverMaxDeathStart");
    currentTerm = 1; // or 1
    alreadyVoted = false;
    serverState = FOLLOWER;
    cDisplayString &dispStr = getDisplayString();
    dispStr.parse("i=device/server2,bronze");
    numberVoteReceived = 0;
    acceptVoteRequest = true;
    leaderAddress = -1;
    networkAddress = gate("gateServer$i", 0)->getPreviousGate()->getIndex();
    Switch = gate("gateServer$i", 0)->getPreviousGate()->getOwnerModule();
    var_X = 2;
    var_Y = 2;
    initializeConfiguration();

    numberVotingMembers = configuration.size();
    if(serverNumber != 2){
        for (int i = 0; i < configuration.size(); ++i)
        {
            nextIndex.push_back(0);
            matchIndex.push_back(-1);
        }}

    // We define a probability of death and we start a self message that will "shut down" some nodes
    double deadProbability = uniform(0, 1);
    if (deadProbability < realProbability)
    {
        double randomDelay = uniform(1, maxDeathStart);
        failureMsg = new cMessage("failureMsg");
        EV
        << "Here is server[" + std::to_string(this->getIndex()) + "]: I will be dead in " + std::to_string(randomDelay) + " seconds...\n";
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
    if (msg == failureMsg or msg == shutDownDeletedServer)
    {
        if(msg == failureMsg){
            bubble("i'm dead");
            cDisplayString &dispStr = getDisplayString();
            dispStr.parse("i=device/server2,red");
            iAmDead = true;
            double maxDeathDuration = getParentModule()->par("serverMaxDeathDuration");
            double randomFailureTime = uniform(5, maxDeathDuration);
            EV
            << "\nServer ID: [" + std::to_string(this->getIndex()) + "] is dead for about: [" + std::to_string(randomFailureTime) + "]\n";
            recoveryMsg = new cMessage("recoveryMsg");
            scheduleAt(simTime() + randomFailureTime, recoveryMsg);
        } else{//here the server has left the current configuration
            currentTerm = -1; // or 1
            alreadyVoted = true;
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
    }

    else if (msg == recoveryMsg)
    {
        iAmDead = false;
        EV << "Here is server[" + std::to_string(this->getIndex()) + "]: I am no more dead... \n";
        bubble("im returned alive");
        cDisplayString &dispStr = getDisplayString();
        dispStr.parse("i=device/server2,bronze");
        // if this server returns alive it have to be a follower because in theory there are already another server that is the leader
        serverState = FOLLOWER;
        numberVoteReceived = 0;
        acceptVoteRequest = true;
        this->alreadyVoted = false;
        electionTimeoutExpired = new cMessage("NewElectionTimeoutExpired");
        double randomTimeout = uniform(2, 4);
        scheduleAt(simTime() + randomTimeout, electionTimeoutExpired);

        // here i kill again servers in order to have a simulations where all server continuously goes down
        double maxDeathStart1 = getParentModule()->par("serverMaxDeathStart");
        double realProbability1 = getParentModule()->par("dieAgainProbability");
        double deadProbability1 = uniform(0, 1);
        if (deadProbability1 < realProbability1)
        {
            double randomDelay1 = uniform(1, maxDeathStart1);
            failureMsg = new cMessage("failureMsg");
            EV
            << "Here is server[" + std::to_string(this->getIndex()) + "]: I will be dead in " + std::to_string(randomDelay1) + " seconds...\n";
            scheduleAt(simTime() + randomDelay1, failureMsg);
        }
    }

    else if (iAmDead)
    {
        EV << "At the moment I'm dead so I can't react to this message, sorry \n";
    }

    // ################################################ NORMAL BEHAVIOUR ################################################
    else if (iAmDead == false)
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
            this->alreadyVoted = true; // each server can vote just one time per election; if the server is in a candidate state it vote for himself

            // i set a new timeout range
            electionTimeoutExpired = new cMessage("NewElectionTimeoutExpired");
            double randomTimeout = uniform(0.75, 1.25);
            scheduleAt(simTime() + randomTimeout, electionTimeoutExpired);

            // send vote request to the switch, it will forward it
            VoteRequest *voteRequest = new VoteRequest("voteRequest");
            voteRequest->setCandidateAddress(networkAddress);
            voteRequest->setCurrentTerm(currentTerm);
            if (leaderTransferPhase)
            {
                voteRequest->setDisruptLeaderPermission(true);
            }
            if(gate("gateServer$o", 0)->isConnected())
                send(voteRequest, "gateServer$o", 0);
        }

        // TO DO: non voting members
        else if (voteRequest != nullptr && (acceptVoteRequest or voteRequest->getDisruptLeaderPermission()))
        { // if arrives a vote request and i didn't already vote i can vote and send this vote to the candidate

            if (voteRequest->getCurrentTerm() > currentTerm)
            { // THIS IS A STEPDOWN PROCEDURE
                cancelEvent(electionTimeoutExpired);
                if (leaderTransferPhase)
                {
                    cancelEvent(leaderTransferFailed);
                    leaderTransferPhase = false;
                    timeOutNowSent = false;
                }
                cDisplayString &dispStr = getDisplayString();
                dispStr.parse("i=device/server2,bronze");
                currentTerm = voteRequest->getCurrentTerm();
                numberVoteReceived = 0;
                serverState = FOLLOWER;
                alreadyVoted = false;
                electionTimeoutExpired = new cMessage("NewElectionTimeoutExpired");
                double randomTimeout = uniform(0.75, 1.25);
                scheduleAt(simTime() + randomTimeout, electionTimeoutExpired);
            }

            if (voteRequest->getCurrentTerm() == currentTerm && alreadyVoted == false)
            {
                cancelEvent(electionTimeoutExpired);
                alreadyVoted = true;
                electionTimeoutExpired = new cMessage("NewElectionTimeoutExpired");
                double randomTimeout = uniform(1, 2);
                scheduleAt(simTime() + randomTimeout, electionTimeoutExpired);
                // now i send to the candidate server that sends to me the vote request a vote reply;
                // i send this message only to him
                bubble("vote reply");
                int candidateAddress = voteRequest->getCandidateAddress();

                // this cycle is useful to send the message only to the candidate server that asks for a vote
                VoteReply *voteReply = new VoteReply("voteReply");
                voteReply->setVoterAddress(networkAddress); // this is the id of the voting server
                voteReply->setLeaderAddress(candidateAddress);
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
            { // THIS IS A STEPDOWN PROCEDURE-> DA RIVEDERE PERCH� NON MOLTO CHIARA
                cancelEvent(electionTimeoutExpired);
                cDisplayString &dispStr = getDisplayString();
                dispStr.parse("i=device/server2,bronze");
                currentTerm = voteReply->getCurrentTerm();
                serverState = FOLLOWER;
                alreadyVoted = false;
                electionTimeoutExpired = new cMessage("NewElectionTimeoutExpired");
                double randomTimeout = uniform(0.75, 1.25);
                scheduleAt(simTime() + randomTimeout, electionTimeoutExpired);
            }

            if (voteReply->getCurrentTerm() == currentTerm && serverState == CANDIDATE)
            {
                numberVoteReceived = numberVoteReceived + 1;
                if (numberVoteReceived > numberVotingMembers / 2)
                { // to became a leader a server must have the majority
                    bubble("i'm the leader");
                    cDisplayString &dispStr = getDisplayString();
                    dispStr.parse("i=device/server2,gold");
                    // if a server becomes leader I have to cancel the timer for a new election since it will
                    // remain leader until the first failure, furthermore i have to reset all variables used in the election
                    cancelEvent(electionTimeoutExpired);
                    serverState = LEADER;
                    leaderAddress = networkAddress;
                    numberVoteReceived = 0;
                    this->alreadyVoted = false;
                    for (int serverIndex = 0; serverIndex < nextIndex.size(); ++serverIndex)
                    {
                        nextIndex[serverIndex] = logEntries.size();
                        if (serverIndex != serverNumber)
                        {
                            matchIndex[serverIndex] = -1;
                        }
                        else
                        {
                            matchIndex[serverIndex] = logEntries.size() - 1;
                        }
                    }
                    // i send in broadcast the heartBeats to all other server and a ping to all the client, in this way every client knows the leader and can send
                    // for simulation purpose i kill the leader every five seconds
                    double realLeaderProbability = getParentModule()->par("leaderDeadProbability");
                    double leaderMaxDeath = getParentModule()->par("leaderMaxDeathDuration");
                    double leaderDeadProbability = uniform(0, 1);
                    if (leaderDeadProbability < realLeaderProbability)
                    {
                        double randomDelay2 = uniform(1, leaderMaxDeath);
                        failureMsg = new cMessage("failureMsg");
                        EV
                        << "Here is server[" + std::to_string(serverNumber) + "]: I will be dead in " + std::to_string(randomDelay2) + " seconds...\n";
                        scheduleAt(simTime() + randomDelay2, failureMsg);
                    }

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
            bool condition2Satisfied = true;

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
                alreadyVoted = false;
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
            int followerIndex = heartBeatResponse->getFollowerIndex();
            int followerLogLength = heartBeatResponse->getLogLength();
            int followerMatchIndex = heartBeatResponse->getMatchIndex();
            int logSize = logEntries.size();
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
                    updateState(logEntries[applyNextIndex]);
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
            int lastReqSerialNumber = getLastSerialNumberFromClient(clientAddress);
            bool alreadyProcessed = false;

            // Immediately send an ack if the request has already been committed
            if(lastReqSerialNumber >= serialNumber)
            {
                sendACKToClient(clientAddress, serialNumber);
                alreadyProcessed = true;
            }

            // Redirect to leader in case the message is received by a follower.
            if (networkAddress != leaderAddress && !alreadyProcessed)
            {
                redirectToLeader(serialNumber, clientAddress);
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

                    //                if (logSize > 0)
                    //                {
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
    reply->setFollowerIndex(serverNumber);
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
    reply->setFollowerIndex(serverNumber);
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

void Server::redirectToLeader(int serialNumber, int clientAddress)
{
    LogMessageResponse *response = new LogMessageResponse("I'm not the leader. Try with this.");
    response->setClientAddress(clientAddress);
    response->setLeaderAddress(leaderAddress);
    response->setSucceded(false);
    response->setLogSerialNumber(serialNumber);
    if(gate("gateServer$o", 0)->isConnected())
        send(response, "gateServer$o", 0);
}

void Server::sendACKToClient(int clientAddress, int serialNumber)
{
    LogMessageResponse *ack = new LogMessageResponse("ACK");
    ack->setClientAddress(clientAddress);
    ack->setLogSerialNumber(serialNumber);
    ack->setLeaderAddress(leaderAddress);
    ack->setSucceded(true);
    if(gate("gateServer$o", 0)->isConnected())
        send(ack, "gateServer$o", 0);
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
        for (int i = commitIndex + 1; i <= lastLogEntryIndex and counter < majority; i++)
        {
            temp = count(matchIndex.begin(), matchIndex.end(), i);
            if (temp > majority and logEntries[i].entryTerm == currentTerm and commitIndex < i)
            {
                commitIndex = i;
                clientAddr = logEntries[i].clientAddress;
                serialNumber = logEntries[i].serialNumber;
                sendACKToClient(clientAddr, serialNumber);
            }
            counter = counter + temp;
        }
    }
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
    std::string serverString = "server";
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

int Server::getLastSerialNumberFromClient(int clientAddress)
{
    for(int i = commitIndex; i >= 0 ; i--)
    {
        if(logEntries[i].clientAddress == clientAddress)
            return logEntries[i].serialNumber;
    }
    return 0;
}

bool Server::needsToBeProcessed(int serialNumber, int clientAddress)
{
    for(int i = lastApplied; i >= 0; i--)
    {
        if(logEntries[i].clientAddress == clientAddress)
        {
            if(logEntries[i].serialNumber >= serialNumber)
            {
                return false;
            }
            else
            {
                return true;
            }
        }
    }
    return true;
}

void Server::deleteServer()
{
    int serverIndex;
    int gatesize = Switch->gateSize("gateSwitch$o");
    for (int i = 0; i < gatesize; i++)
    {
        // There is only one server to delete and disconnect port from the switch
        serverIndex = Switch->gate("gateSwitch$o", i)->getIndex();
        if (serverNumber == serverIndex)
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
        if(Switch->gate("gateSwitch$o", i-1)->getNextGate() == nullptr){//doesn't exit the link from switch to client, so i create it

        }
        tempClient = Switch->gate("gateSwitch$o", i-1)->getNextGate()->getOwnerModule();//this is the client with the highest index
        Switch->gate("gateSwitch$o", i-1)->disconnect();
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

void Server::refreshDisplay() const{
    char buf[100];
    sprintf(buf, "currentTerm: %ld\n commitIndex: %ld\n lastApplied: %ld\n X==%ld; Y==%ld", currentTerm, commitIndex, lastApplied, var_X, var_Y);
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
