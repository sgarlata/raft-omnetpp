/*
 * server.cc
 *
 *  Created on: 13 mar 2022
 *      Author: ste_dochio
 */
#include <stdio.h>
#include <string.h>
#include <omnetpp.h>
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

class Server : public cSimpleModule
{
    /*
     * red = server down;
     * bronze = server in follower state;
     * silver = server in candidate state;
     * gold = server in leader state;
     */
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

    enum stateEnum
    {
        FOLLOWER,
        CANDIDATE,
        LEADER,
        NON_VOTING_MEMBER
    };
    stateEnum serverState; // Current state (Follower, Leader or Candidate)
    std::vector<int> configuration;
    int numberVotingMembers;
    int networkAddress;
    int serverNumber;
    int messageElectionReceived;
    bool acceptVoteRequest;
    int temp;

    /****** STATE MACHINE ******/
    state_machine_variable variables[2];

    /****** Persistent state on all servers: ******/
    int currentTerm; // Time is divided into terms, and each term begins with an election. After a successful election, a single leader
    // manages the cluster until the end of the term. Some elections fail, in which case the term ends without choosing a leader.
    bool alreadyVoted; // ID of the candidate that received vote in current term (or null if none)
    std::vector<log_entry> logEntries;

    double randomTimeout; // when it expires an election starts

    int leaderAddress;          // network address of the leader
    int numberVoteReceived = 0; // number of vote received by every server
    bool iAmDead = false;       // it's a boolean useful to shut down server/client
    bool leaderTransferPhase = false;
    bool timeOutNowSent = false;

    /****** Volatile state on all servers: ******/
    int commitIndex = -1; // index of highest log entry known to be committed (initialized to 0, increases monotonically)
    int lastApplied = -1; // index of highest log entry applied to state machine (initialized to 0, increases monotonically)

    /****** Volatile state on leaders (Reinitialized after election) ******/
    std::vector<int> nextIndex;  // for each server, index of the next log entry to send to that server (initialized to leader last log index + 1)
    std::vector<int> matchIndex; // for each server, index of highest log entry known to be replicated on server (initialized to 0, increases monotonically)

    /****** Catching up phase ******/
    bool catchUpPhaseRunning = false;
    int newServerAddress; // server to catch up
    int catchUpRound;
    int maxNumberRound;

protected:
    // FOR TIMER IMPLEMENTATION SEE TXC8.CC FROM CUGOLA EXAMPLE, E'LA TIMEOUT EVENT
    // GUARDARE SEMPRE ESEMPIO TXC8.CC E SIMULATION PER CAPIRE COME MANDARE PIU' MESSAGGI
    // CONTEMPORANEAMENTE.

    // TXC15 E TXC16 SERVONO INVECE A FINI STATISTICI, GUARDARE QUESTI DUE ESEMPI PER CAPIRE
    // COME COLLEZIONARE DATI DURANTE LA LIVE SIMULATION

    virtual void initialize() override;
    virtual void handleMessage(cMessage *msg) override;
    virtual void commitLog(log_entry log);
    virtual void replyToClient(bool succeded, int serialNumber, cGate *clientGate);
    virtual void redirectToLeader(int serialNumber, cGate *clientGate);
    virtual void updateCommitIndex();
    virtual void updateState(log_entry log);
    virtual void acceptLog(int leaderAddress, int matchIndex);
    virtual void startAcceptVoteRequestCountdown();
    virtual void rejectLog(int leaderAddress);
    virtual void tryLeaderTransfer(int targetAddress);
    virtual void restartCountdown();
    virtual int min(int a, int b);
    virtual void initializeConfiguration();
    virtual void finish() override;
};

Define_Module(Server);

void Server::initialize()
{
    WATCH(iAmDead);
    WATCH(currentTerm);
    WATCH_VECTOR(configuration);
    serverNumber = this->getIndex();
    double realProbability = getParentModule()->par("serverDeadProbability");
    double maxDeathStart = getParentModule()->par("serverMaxDeathStart");
    currentTerm = 1; // or 1
    alreadyVoted = false;
    serverState = FOLLOWER;
    numberVoteReceived = 0;
    acceptVoteRequest = true;
    leaderAddress = -1;
    networkAddress = gate("gateServer$i", 0)->getPreviousGate()->getIndex();
    initializeConfiguration();
    numberVotingMembers = configuration.size();
    for (int i = 0; i < configuration.size(); ++i)
    {
        nextIndex.push_back(0);
        matchIndex.push_back(0);
    }

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

    // here expires the first timeout; so the first server with timeout expired sends the first leader election message
    electionTimeoutExpired = new cMessage("ElectionTimeoutExpired");
    double randomTimeout = uniform(0.50, 1);
    scheduleAt(simTime() + randomTimeout, electionTimeoutExpired);

    applyChangesMsg = new cMessage("ApplyChangesToFiniteStateMachine");
    scheduleAt(simTime() + 1, applyChangesMsg);
}
// here i redefine handleMessage method
// invoked every time a message enters in the node
void Server::handleMessage(cMessage *msg)
{
    Ping *ping = dynamic_cast<Ping *>(msg);
    VoteReply *voteReply = dynamic_cast<VoteReply *>(msg);
    VoteRequest *voteRequest = dynamic_cast<VoteRequest *>(msg);
    LeaderElection *leaderElection = dynamic_cast<LeaderElection *>(msg);
    HeartBeats *heartBeat = dynamic_cast<HeartBeats *>(msg);
    HeartBeatResponse *heartBeatResponse = dynamic_cast<HeartBeatResponse *>(msg);
    LogMessage *logMessage = dynamic_cast<LogMessage *>(msg);
    TimeOutNow *timeoutLeaderTransfer = dynamic_cast<TimeOutNow *>(msg);

    // ############################################### RECOVERY BEHAVIOUR ###############################################
    if (msg == failureMsg)
    {
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
        if (msg == electionTimeoutExpired and serverState != LEADER)
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
                // getSenderGate() ?
                VoteReply *voteReply = new VoteReply("voteReply");
                voteReply->setVoterAddress(networkAddress); // this is the id of the voting server
                voteReply->setLeaderAddress(candidateAddress);
                voteReply->setCurrentTerm(currentTerm);
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
                    numberVoteReceived = 0;
                    this->alreadyVoted = false;
                    for (int serverIndex = 0; serverIndex < nextIndex.size(); ++serverIndex)
                    {
                        if (serverIndex != serverNumber)
                        {
                            if (logEntries.size() != 0)
                            {
                                nextIndex[serverIndex] = logEntries.size();
                            } // ELSE: nextIndex == 0, as its initial value
                            matchIndex[serverIndex] = 0;
                        }
                        else
                        {
                            nextIndex[serverIndex] = logEntries.size();
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

                    // the leader sends RPCAppendEntries messages to all the followers
                    for (int i = 0; i < configuration.size(); i++)
                    {
                        if (i != networkAddress)
                        {
                            int h = configuration[i];
                            HeartBeats *heartBeat = new HeartBeats("im the leader");
                            heartBeat->setLeaderAddress(networkAddress);
                            heartBeat->setDestAddress(i);
                            heartBeat->setLeaderCurrentTerm(currentTerm);
                            send(heartBeat, "gateServer$o", 0);
                        }
                    }
                    // the leader periodically send the heartBeat
                    heartBeatsReminder = new cMessage("heartBeatsReminder");
                    double randomTimeout = uniform(0.1, 0.3);
                    scheduleAt(simTime() + randomTimeout, heartBeatsReminder);
                }
            }
        }

        // HEARTBEAT RECEIVED (AppendEntries RPC)
        else if (heartBeat != nullptr)
        {
            int term = heartBeat->getLeaderCurrentTerm();
            int prevLogIndex = heartBeat->getPrevLogIndex();
            int prevLogTerm = heartBeat->getPrevLogTerm();
            int leaderCommit = heartBeat->getLeaderCommit();

            // ALL SERVERS: If RPC request or response contains term > currentTerm: set currentTerm = term, convert to follower
            if (term > currentTerm)
            {
                currentTerm = term;
                cDisplayString &dispStr = getDisplayString();
                dispStr.parse("i=device/server2, bronze");
                serverState = FOLLOWER;
                numberVoteReceived = 0;
                alreadyVoted = false;
                leaderAddress = heartBeat->getLeaderAddress();
                startAcceptVoteRequestCountdown();
            }

            // @ensure LOG MATCHING PROPERTY
            // CONSISTENCY CHECK: (1) Reply false if term < currentTerm
            if (term < currentTerm)
            {
                rejectLog(leaderAddress);
            }
            // (2) Reply false if log doesn't contain an entry at prevLogIndex...
            // whose term matches prevLogTerm
            // (2.a) Log entries is too short
            else if (prevLogIndex > logEntries.size() - 1)
            {
                rejectLog(leaderAddress);
                restartCountdown();
            }
            else
            {
                if (logEntries.size() > 0)
                {
                    if (logEntries[prevLogIndex].entryTerm != prevLogTerm)
                    { // (2.b) no entry at prevLog index whose term matches prevLogTerm
                        rejectLog(leaderAddress);
                    }
                    // if logEntries is empty (size == 0) there is no need to deny
                }
                else
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
                            commitIndex = prevLogIndex; // no new entries in the message: we can guarantee consistency up to prevLogIndex
                        }
                        acceptLog(leaderAddress, prevLogIndex);
                    }
                    else
                    { // CASE B: heartbeat delivers a new entry for follower's log
                        // @ensure CONSISTENCY WITH SEVER LOG UP TO prevLogIndex
                        // No entry at newEntryIndex, simply append the new entry
                        if (logEntries.size() - 1 < newEntryIndex)
                        {
                            logEntries.push_back(heartBeat->getEntry());
                        }
                        // @ensure (3): if an existing entry conflicts with a new one (same index but different terms),
                        //              delete the existing entry and all that follow it
                        // Conflicting entry at newEntryIndex, delete the last entries up to newEntryIndex, then append the new entry
                        else if (logEntries[newEntryIndex].entryTerm != term)
                        {
                            int to_erase = logEntries.size() - newEntryIndex;
                            logEntries.erase(logEntries.end() - to_erase, logEntries.end());
                            logEntries.push_back(heartBeat->getEntry());
                        }
                        // NOTE: if a replica receives the same entry twice, it simply ignores the second one and sends an ACK.
                        // @ensure (5) If leaderCommit > commitIndex, set commitIndex = min(leaderCommit, index of last new entry)
                        // *index of last
                        acceptLog(leaderAddress, newEntryIndex);
                    }
                    if (leaderCommit > commitIndex)
                    {
                        commitIndex = min(leaderCommit, newEntryIndex);
                    }
                }
                startAcceptVoteRequestCountdown();
                restartCountdown();
            }
        }

        else if (heartBeatResponse != nullptr)
        {
            int clientIndex = heartBeatResponse->getFollowerIndex();
            int clientLogLength = heartBeatResponse->getLogLength();
            if (heartBeatResponse->getSucceded())
            {
                // heartBeat accepted and log still needs some update
                if (nextIndex[clientIndex] < logEntries.size())
                {
                    nextIndex[clientIndex]++;
                }
                // else heartBeat accepted and log is up to date
                // TO DO: update commit index
            }
            else
            {
                // heartBeat rejected
                if (clientLogLength < nextIndex[clientIndex])
                {
                    nextIndex[clientIndex] = clientLogLength;
                }
                else
                {
                    nextIndex[clientIndex]--;
                }
            }
        }

        else if (msg == minElectionTimeoutExpired)
        {
            acceptVoteRequest = true;
        }

        // SEND HEARTBEAT (AppendEntries RPC)
        else if (msg == heartBeatsReminder)
        {
            int lastLogIndex = logEntries.size() - 1;
            int nextLogIndex;
            for (int followerAddr = 0; followerAddr < configuration.size(); followerAddr++)
            {
                HeartBeats *heartBeat = new HeartBeats("i'm the leader");
                // to avoid message to client and self message
                if (followerAddr != this->networkAddress)
                {
                    nextLogIndex = nextIndex[followerAddr];
                    heartBeat->setLeaderAddress(this->networkAddress);
                    heartBeat->setDestAddress(followerAddr);
                    heartBeat->setLeaderCurrentTerm(currentTerm);
                    heartBeat->setLeaderCommit(commitIndex);
                    heartBeat->setPrevLogIndex(nextLogIndex - 1);
                    if (logEntries.size() > 0)
                    {
                        // leader's log not empty
                        heartBeat->setPrevLogTerm(logEntries[nextLogIndex - 1].entryTerm);
                        if (nextLogIndex <= lastLogIndex)
                        {
                            // follower's log needs an update
                            heartBeat->setEntry(logEntries[nextLogIndex]);
                            heartBeat->setEmpty(false);
                        }
                        else
                            // follower's log up to date
                            if (leaderTransferPhase && !timeOutNowSent)
                            {
                                tryLeaderTransfer(followerAddr);
                            }
                    }
                    else
                    {
                        // leader's log is empty, any other log will match with it
                        heartBeat->setPrevLogTerm(0);
                        if (leaderTransferPhase && !timeOutNowSent)
                        {
                            tryLeaderTransfer(followerAddr);
                        }
                    }
                    send(heartBeat, "gateServer$o", 0);
                }
            }
            applyChangesMsg = new cMessage("Apply changes to State Machine");
            scheduleAt(simTime() + 2, applyChangesMsg);

            heartBeatsReminder = new cMessage("heartBeatsReminder");
            double randomTimeout = uniform(0.1, 0.3);
            scheduleAt(simTime() + randomTimeout, heartBeatsReminder);
        }

        else if (msg == applyChangesMsg)
        {
            // TO DO: apply changes to FSM
        }

        // LOG MESSAGE REQUEST RECEIVED, it is ignored only if leader transfer process is going on
        else if (logMessage != nullptr && !leaderTransferPhase)
        {
            int serialNumber = logMessage->getSerialNumber();
            cGate *clientGate = gateHalf(logMessage->getArrivalGate()->getName(), cGate::OUTPUT,
                                         logMessage->getArrivalGate()->getIndex());
            // Redirect to leader in case the message is received by a follower.
            if (networkAddress != leaderAddress)
            {
                redirectToLeader(serialNumber, clientGate);
                // replyToClient(false, serialNumber, clientGate);
            }
            else
            {
                // once a log message is received a new log entry is added in the leader node
                // TO DO: check whether the log is already in the queue (serial number check)
                // If that is the case and the log has already been committed, simply reply with a "success" ACK
                log_entry newEntry;
                newEntry.clientAddress = logMessage->getClientAddress();
                newEntry.entryTerm = currentTerm;
                newEntry.operandName = logMessage->getOperandName();
                newEntry.operandValue = logMessage->getOperandValue();
                newEntry.operation = logMessage->getOperation();
                logEntries.push_back(newEntry);
                logEntries[logEntries.size() - 1].entryLogIndex = logEntries.size() - 1;
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
    }
}

void Server::commitLog(log_entry log)
{
    updateState(log);
    logEntries.push_back(log);
    // TO DO: update commit state
}

void Server::acceptLog(int leaderAddress, int matchIndex)
{
    HeartBeatResponse *reply = new HeartBeatResponse("Consistency check: OK");
    reply->setMatchIndex(matchIndex);
    reply->setTerm(currentTerm);
    reply->setSucceded(true);
    reply->setLeaderAddress(leaderAddress);
    send(reply, "gateServer$o", 0);
}

void Server::tryLeaderTransfer(int addr)
{
    TimeOutNow *timeOutLeaderTransfer = new TimeOutNow("TIMEOUT_NOW");
    timeOutLeaderTransfer->setDestAddress(addr);
    send(timeOutLeaderTransfer, "gateServer$o", 0);
    timeOutNowSent = true;
    leaderTransferFailed = new cMessage("Leader transfer failed");
    scheduleAt(simTime() + 2, leaderTransferFailed);
}

void Server::rejectLog(int leaderAddress)
{
    HeartBeatResponse *reply = new HeartBeatResponse("Consistency check: FAIL");
    reply->setMatchIndex(0);
    reply->setTerm(currentTerm);
    reply->setSucceded(false);
    reply->setLeaderAddress(leaderAddress);
    reply->setLogLength(logEntries.size());
    reply->setFollowerIndex(serverNumber);
    send(reply, "serverGate$o", 0);
}

void Server::replyToClient(bool succeded, int serialNumber, cGate *clientGate)
{
    std::string serNumber = std::to_string(serialNumber);
    std::string temp;

    if (succeded)
    {
        temp = "ACK: " + serNumber;
    }
    else
    {
        temp = "NACK: " + serNumber;
    }
    char const *messageContent = temp.c_str();
    LogMessageResponse *response = new LogMessageResponse(messageContent);
    response->setLogSerialNumber(serialNumber);
    response->setSucceded(succeded);
    response->setClientId(clientGate->getPathEndGate()->getOwnerModule()->getId());
    send(response, clientGate->getName(), clientGate->getIndex());
}

void Server::redirectToLeader(int serialNumber, cGate *clientGate)
{
    LogMessageResponse *response = new LogMessageResponse("I'm not the leader. Try with this.");
    // response->setClientId(); TODO obtain client id from clientgate
    response->setLeaderAddress(leaderAddress);
    response->setSucceded(false);
    response->setLogSerialNumber(serialNumber);
}

void Server::restartCountdown()
{
    cancelEvent(electionTimeoutExpired);
    electionTimeoutExpired = new cMessage("NewElectionTimeoutExpired");
    // double randomTimeout = uniform(0.5, 1);
    double randomTimeout = uniform(1, 2);
    scheduleAt(simTime() + randomTimeout, electionTimeoutExpired);
}

void Server::startAcceptVoteRequestCountdown()
{
    acceptVoteRequest = false;
    minElectionTimeoutExpired = new cMessage("minElectionCountodwn");
    scheduleAt(simTime() + 1, minElectionTimeoutExpired);
}

int Server::min(int a, int b)
{
    if (a > b)
        return a;
    else
        return b;
}

// If there exists an N such that N > commitIndex, a majority of matchIndex[i] >= N,
// and log[N].term == currentTerm: set commitIndex = N
// TO DO: non voting members
void Server::updateCommitIndex()
{
    int lastLogEntryIndex = logEntries.size() - 1;
    int counter = 0;
    int temp;
    if (lastLogEntryIndex > commitIndex and counter < numberVotingMembers / 2)
    {
        for (int i = commitIndex + 1; i < lastLogEntryIndex; i++)
        {
            temp = std::count(matchIndex.begin(), matchIndex.end(), i);
            if (temp > numberVotingMembers / 2 and logEntries[i].entryTerm == currentTerm)
            {
                commitIndex = i;
            }
            counter = counter + temp;
        }
    }
}

// TO DO: redo all this function
void Server::updateState(log_entry log)
{
    int index = (int)log.operandName - 65;
    if (log.operandName == 'S')
    {
        variables[index].val = log.operandValue;
    }
    else if (log.operandName == 'A')
    {
        variables[index].val = variables[index].val + log.operandValue;
    }
    else if (log.operandName == 'B')
    {
        variables[index].val = variables[index].val - log.operandValue;
    }
    else if (log.operandName == 'M')
    {
        variables[index].val = variables[index].val * log.operandValue;
    }
    else if (log.operandName == 'D')
    {
        variables[index].val = variables[index].val / log.operandValue;
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

void Server::finish()
{
    cancelAndDelete(failureMsg);
    cancelAndDelete(recoveryMsg);
    cancelAndDelete(electionTimeoutExpired);
    cancelAndDelete(heartBeatsReminder);
}
