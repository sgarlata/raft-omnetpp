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
#include "HeartBeat_m.h"
#include "HeartBeatResponse_m.h"

using namespace omnetpp;

class Server : public cSimpleModule
{
    // this is the class definition that is separated from implementation
    // sto estendendo la classe c simple module da cui eredito tutti i metodi, tra cui ad esempio il metodo get name
private:
    cMessage *electionTimeoutExpired; // autoMessage
    cMessage *heartBeatsReminder;     // if the leader receive this autoMessage it send a broadcast heartbeat
    cMessage *failureMsg;             // autoMessage to shut down this server
    cMessage *recoveryMsg;            // autoMessage to reactivate this server

    enum stateEnum
    {
        FOLLOWER,
        CANDIDATE,
        LEADER,
        DEAD
    };
    stateEnum serverState; // Current state (Follower, Leader or Candidate)
    int messageElectionReceived;
    int serverNumber;
    int temp;

    /****** STATE MACHINE ******/
    state_machine_variable variables[26];

    /****** Persistent state on all servers: ******/
    int currentTerm;   // Time is divided into terms, and each term begins with an election. After a successful election, a single leader
                       // manages the cluster until the end of the term. Some elections fail, in which case the term ends without choosing a leader.
    bool alreadyVoted; // ID of the candidate that received vote in current term (or null if none)
                       // -------------------------------------------------------------------------- Sarebbe votedFor? boolean?
    std::vector<log_entry> logEntries;

    double randomTimeout; // when it expires an election starts

    int leaderId;               // ID of the leader
    int numberVoteReceived = 0; // number of vote received by every server
    bool iAmDead = false;       // it's a boolean useful to shut down server/client

    /****** Volatile state on all servers: ******/
    int commitIndex = 0; // index of highest log entry known to be committed (initialized to 0, increases monotonically)
    int lastApplied = 0; // index of highest log entry applied to state machine (initialized to 0, increases monotonically)

    /****** Volatile state on leaders (Reinitialized after election) ******/
    std::vector<int> nextIndex;  // for each server, index of the next log entry to send to that server (initialized to leader last log index + 1)
    std::vector<int> matchIndex; // for each server, index of highest log entry known to be replicated on server (initialized to 0, increases monotonically)

protected:
    // FOR TIMER IMPLEMENTATION SEE TXC8.CC FROM CUGOLA EXAMPLE, E'LA TIMEOUT EVENT
    // GUARDARE SEMPRE ESEMPIO TXC8.CC E SIMULATION PER CAPIRE COME MANDARE PIU' MESSAGGI
    // CONTEMPORANEAMENTE.

    // TXC15 E TXC16 SERVONO INVECE A FINI STATISTICI, GUARDARE QUESTI DUE ESEMPI PER CAPIRE
    // COME COLLEZIONARE DATI DURANTE LA LIVE SIMULATION

    virtual void initialize() override;
    virtual void handleMessage(cMessage *msg) override;
    virtual void commitLog(log_entry log);
    virtual void updateState(log_entry log);
    virtual void acceptLog(cGate *leaderGate, int matchIndex);
    virtual void rejectLog(cGate *leaderGate);
    virtual int min(int a, int b);
    virtual void finish() override;
    // virtual void sendBroadcastToAllServer();// this method is useful to send broadcast message from one server to all other server
};

Define_Module(Server);

// here began the implementation of the class
// here i redefine initialize method
// invoked at simulation starting time
void Server::initialize()
{
    WATCH(iAmDead);
    // parameter initialization
    serverNumber = getParentModule()->par("numServer");
    double realProbability = getParentModule()->par("serverDeadProbability");
    double maxDeathStart = getParentModule()->par("serverMaxDeathStart");
    currentTerm = 0; // or 1
    alreadyVoted = false;
    serverState = FOLLOWER;
    leaderId = -1;
    for (int i = 0; i < serverNumber; ++i)
    {
        nextIndex.push_back(1);
        matchIndex.push_back(0);
    }

    for (char ch = 'a'; ch < 'z'; ch++)
    {
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

    // this is a self message
    // here expires the first timeout; so the first server with timeout expired sends the first leader election message
    electionTimeoutExpired = new cMessage("ElectionTimeoutExpired");
    double randomTimeout = uniform(0.25, 0.7);
    scheduleAt(simTime() + randomTimeout, electionTimeoutExpired);
}
// here i redefine handleMessage method
// invoked every time a message enters in the node
void Server::handleMessage(cMessage *msg)
{
    Ping *ping = dynamic_cast<Ping *>(msg);
    VoteReply *voteReply = dynamic_cast<VoteReply *>(msg);
    VoteRequest *voteRequest = dynamic_cast<VoteRequest *>(msg);
    LeaderElection *leaderElection = dynamic_cast<LeaderElection *>(msg);
    HeartBeats *heartBeats = dynamic_cast<HeartBeats *>(msg);
    LogMessage *logMessage = dynamic_cast<LogMessage *>(msg);

    // ############################################### RECOVERY BEHAVIOUR ###############################################
    if (msg == failureMsg)
    {
        iAmDead = true;
        serverNumber--;
        recoveryMsg = new cMessage("recoveryMsg");

        double maxDeathDuration = getParentModule()->par("serverMaxDeathDuration");
        double randomFailureTime = uniform(5, maxDeathDuration);
        EV
            << "\nServer ID: [" + std::to_string(this->getIndex()) + "] is dead for about: [" + std::to_string(randomFailureTime) + "]\n";
        scheduleAt(simTime() + randomFailureTime, recoveryMsg);
    }

    else if (msg == recoveryMsg)
    {
        iAmDead = false;
        EV << "Here is server[" + std::to_string(this->getIndex()) + "]: I am no more dead... \n";
        serverNumber++;
    }

    else if (iAmDead)
    {
        EV << "At the moment I'm dead so I can't react to this message, sorry \n";
    }

    // ################################################ NORMAL BEHAVIOUR ################################################
    else if (iAmDead == false)
    {
        if (msg == electionTimeoutExpired)
        { // I only enter here if a new election has to be done
            bubble("timeout expired, new election start");

            currentTerm++;
            serverState = CANDIDATE;
            numberVoteReceived++;      // it goes to 1, the server vote for himself
            this->alreadyVoted = true; // each server can vote just one time per election; if the server is in a candidate state it vote for himself

            // i set a new timeout range
            electionTimeoutExpired = new cMessage("NewElectionTimeoutExpired");
            double randomTimeout = uniform(0.5, 1);
            scheduleAt(simTime() + randomTimeout, electionTimeoutExpired);

            // now i send in broadcast to all other server the vote request
            for (cModule::GateIterator i(this); !i.end(); i++)
            {
                cGate *gate = *i;
                // dst server index
                int h = (gate)->getPathEndGate()->getOwnerModule()->getIndex();
                const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
                std::string ex = "server";

                // to avoid message to client and self message
                if (h != this->getIndex() && name == ex)
                {
                    VoteRequest *voteRequest = new VoteRequest("voteRequest");
                    voteRequest->setCandidateIndex(this->getIndex());
                    send(voteRequest, gate->getName(), gate->getIndex());
                }
            }
        }

        else if (voteRequest != nullptr && alreadyVoted == false)
        { // if arrives a vote request and i didn't already vote i can vote and send this vote to the candidate

            cancelEvent(electionTimeoutExpired);
            alreadyVoted = true;
            electionTimeoutExpired = new cMessage("NewElectionTimeoutExpired");
            double randomTimeout = uniform(0.5, 1);
            scheduleAt(simTime() + randomTimeout, electionTimeoutExpired);
            // now i send to the candidate server that sends to me the vote request a vote reply;
            // i send this message only to him
            bubble("vote reply");
            int candidateIndex = voteRequest->getCandidateIndex();

            // this cycle is useful to send the message only to the candidate server that asks for a vote
            // getSenderGate() ?
            for (cModule::GateIterator i(this); !i.end(); i++)
            {
                cGate *gate = *i;
                int h = (gate)->getPathEndGate()->getOwnerModule()->getIndex();
                const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
                std::string ex = "server";

                // to send message to candidate server
                if (h == candidateIndex && name == ex && h != this->getIndex())
                {
                    VoteReply *voteReply = new VoteReply("voteReply");
                    voteReply->setCommonServerIndex(this->getIndex()); // this is the id of the voting server
                    voteReply->setLeaderIndex(candidateIndex);
                    send(voteReply, gate->getName(), gate->getIndex());
                }
            }
        }

        else if (voteReply != nullptr)
        { // here i received a vote so i increment the current term vote
            // bubble("vote reply received");
            numberVoteReceived = numberVoteReceived + 1;
            if (numberVoteReceived > (serverNumber / 2))
            { // to became a leader a server must have the majority

                // if a server becomes leader I have to cancel the timer for a new election since it will
                // remain leader until the first failure, furthermore i have to reset all variables used in the election
                // so i reset here the leader variable, LE ALTRE VARIABILI NON SO DOVE RESETTARLE
                cancelEvent(electionTimeoutExpired);

                bubble("im the leader");
                serverState = LEADER;
                numberVoteReceived = 0;
                this->alreadyVoted = false;
                // i send in broadcast the heartBeats to all other server and a ping to all the client, in this way every client knows the leader and can send
                // information that must be saved in the log
                for (cModule::GateIterator i(this); !i.end(); i++)
                {
                    cGate *gate = *i;
                    int h = (gate)->getPathEndGate()->getOwnerModule()->getIndex();
                    const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
                    std::string server = "server";
                    // to avoid message to client and self message
                    if (h != this->getIndex() && name == server)
                    {
                        HeartBeats *heartBeats = new HeartBeats("im the leader");
                        heartBeats->setLeaderIndex(this->getIndex());
                        send(heartBeats, gate->getName(), gate->getIndex());
                    }
                    std::string client = "client";
                    // to send message only to all clients
                    if (name == client)
                    {
                        Ping *ping = new Ping("i'm the leader");
                        EV << "i'm the leader, with index " << std::to_string(this->getIndex());
                        ping->setExecIndex(this->getIndex());
                        send(ping, gate->getName(), gate->getIndex());
                    }
                }
                // the leader periodically send an heartBeats
                heartBeatsReminder = new cMessage("heartBeatsReminder");
                double randomTimeout = uniform(0.1, 0.3);
                scheduleAt(simTime() + randomTimeout, heartBeatsReminder);
            }
        }

        // HEARTBEAT RECEIVED (AppendEntries RPC)
        else if (heartBeats != nullptr)
        {
            // da ottimizzare, qui azzero troppe volte le variabili
            // CANCEL RUNNING TIMEOUT
            serverState = FOLLOWER;
            numberVoteReceived = 0;
            this->alreadyVoted = false;
            cancelEvent(electionTimeoutExpired);

            int term = heartBeats->getLeaderCurrentTerm();
            int prevLogIndex = heartBeats->getPrevLogIndex();
            int prevLogTerm = heartBeats->getPrevLogTerm();
            int leaderCommit = heartBeats->getLeaderCommit();
            cGate *leaderGate = gateHalf(heartBeats->getArrivalGate()->getName(), cGate::OUTPUT,
                                         heartBeats->getArrivalGate()->getIndex());

            // @ensure LOG MATCHING PROPERTY
            // CONSISTENCY CHECK: (1) Reply false if term < currentTerm
            //                    (2) Reply false if log doesn’t contain an entry at prevLogIndex...
            //                       whose term matches prevLogTerm
            if (term < currentTerm or prevLogIndex > logEntries.size() - 1)
            {
                rejectLog(leaderGate);
            }
            else
            {
                if (logEntries.size() > 0)
                { // if logEntries is empty there is no need to deny
                    if (logEntries[prevLogIndex].entryTerm != prevLogTerm)
                    { // (2)
                        rejectLog(leaderGate);
                    }
                }
                else
                {
                    // LOG IS ACCEPTED
                    int newEntryIndex = heartBeats->getEntry().entryLogIndex;
                    // CASE A: logMessage does not contain any entry, the follower
                    //         replies to confirm consistency with leader's log
                    // NOTE: id == 0 is the default value, but it does not correspond to any client
                    if (heartBeats->getEntry().clientId == 0)
                    {
                        if (leaderCommit > commitIndex)
                        {
                            commitIndex = prevLogIndex; // min(leaderCommit, prevLogIndex) == prevLogIndex;
                        }
                        acceptLog(leaderGate, prevLogIndex);
                    }
                    else
                    { // CASE B: logMessage delivers a new entry for follower's log
                      // @ensure CONSISTENCY WITH SEVER LOG UP TO prevLogIndex
                      // No entry at newEntryIndex, simply append the new entry
                        if (logEntries.size() - 1 < newEntryIndex)
                        {
                            logEntries.push_back(heartBeats->getEntry());
                        }
                        // @ensure (3): if an existing entry conflicts with a new one (same index but different terms),
                        //              delete the existing entry and all that follow it
                        // Conflicting entry at newEntryIndex, delete the last entries up to newEntryIndex, then append the new entry
                        else if (logEntries[newEntryIndex].entryTerm != term)
                        {
                            int to_erase = logEntries.size() - newEntryIndex;
                            logEntries.erase(logEntries.end() - to_erase, logEntries.end());
                            logEntries.push_back(heartBeats->getEntry());
                        }
                        // @ensure (5) If leaderCommit > commitIndex, set commitIndex = min(leaderCommit, index of last new entry)
                        // *index of last
                        acceptLog(leaderGate, newEntryIndex);
                    }
                    if (leaderCommit > commitIndex)
                    {
                        commitIndex = min(leaderCommit, newEntryIndex);
                    }
                }
            }
            electionTimeoutExpired = new cMessage("NewElectionTimeoutExpired");
            double randomTimeout = uniform(2, 4);
            scheduleAt(simTime() + randomTimeout, electionTimeoutExpired);
        }

        else if (msg == heartBeatsReminder)
        {
            for (cModule::GateIterator i(this); !i.end(); i++)
            {
                cGate *gate = *i;
                // h represents the follower's index
                int h = (gate)->getPathEndGate()->getOwnerModule()->getIndex();
                const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
                std::string ex = "server";

                // to avoid message to client and self message
                if (h != this->getIndex() && name == ex)
                {
                    HeartBeats *heartBeat = new HeartBeats("i'm the leader");
                    send(heartBeat, gate->getName(), gate->getIndex());
                }
            }
            heartBeatsReminder = new cMessage("heartBeatsReminder");
            double randomTimeout = uniform(0.1, 0.3);
            scheduleAt(simTime() + randomTimeout, heartBeatsReminder);
        }

        // LOG MESSAGE REQUEST RECEIVED
        else if (logMessage != nullptr)
        {
            // TO ADD: forward to leader in case the message is received by a follower.
            // If the leader is not known, wait until the new leader is elected and forward
            // client's req to the new leader. (I guess)

            // once a log message is received a new log entry is added in the leader node
            log_entry newEntry;
            newEntry.clientId = logMessage->getClientId();
            newEntry.entryTerm = currentTerm;
            newEntry.operandName = logMessage->getOperandName();
            newEntry.operandValue = logMessage->getOperandValue();
            newEntry.operation = logMessage->getOperation();
            logEntries.push_back(newEntry);
            logEntries[logEntries.size() - 1].entryLogIndex = logEntries.size() - 1;

            // TO DO: send update to followers
        }
    }
    /*
     else if (ping != nullptr){
     EV<<"SONO NEL PING";
     //rimando il ping al client
     bubble("message from client received");
     int clientId = ping->getClientIndex();
     // provaPerIlPing++;

     //int newIndexx = provaPerIlPing % 3;

     delete ping;

     Ping *ping = new Ping("pong");
     ping->setExecIndex(newIndexx);

     ping->setClientIndex(clientId);

     for (cModule::GateIterator i(this); !i.end(); i++) {
     cGate *gate = *i;
     int h = (gate)->getPathEndGate()->getOwnerModule()->getIndex();
     const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
     std::string cl = "client";
     // To send the msg only to the client chosen, not the exec with same index
     if (h == ping->getClientIndex() && name == cl) {
     EV << "\n##################### \n";
     EV << "... answering to the client[" + std::to_string(ping->getClientIndex()) + "]";
     EV << "\n##################### \n";
     send(ping, gate->getName(), gate->getIndex());
     }
     }
     }

     else if(leaderElection != nullptr && messageElectionReceived == 0){
     bubble("leader election start");
     delete leaderElection;
     for (cModule::GateIterator i(this); !i.end(); i++) {
     cGate *gate = *i;
     int h = (gate)->getPathEndGate()->getOwnerModule()->getIndex();
     const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
     std::string ex = "server";

     // to avoid self msg and msg to client
     if (h != this->getIndex() && name == ex) {
     LeaderElection *leaderElection = new LeaderElection("election");
     send(leaderElection, gate->getName(), gate->getIndex());
     }
     }
     }
     */
}

void Server::commitLog(log_entry log)
{
    updateState(log);
    logEntries.push_back(log);
    // TO DO: update commit state
}

void Server::acceptLog(cGate *leaderGate, int matchIndex)
{
    HeartBeatResponse *reply = new HeartBeatResponse("Consistency check: OK");
    reply->setMatchIndex(matchIndex);
    reply->setTerm(currentTerm);
    reply->setSucceded(true);
    send(reply, leaderGate->getName(), leaderGate->getIndex());
}

void Server::rejectLog(cGate *leaderGate)
{
    HeartBeatResponse *reply = new HeartBeatResponse("Consistency check: FAIL");
    reply->setMatchIndex(0);
    reply->setTerm(currentTerm);
    reply->setSucceded(false);
    send(reply, leaderGate->getName(), leaderGate->getIndex());
}

int Server::min(int a, int b)
{
    if (a > b)
        return a;
    else
        return b;
}

// Assumption: variables name space is the lower-case alphabet
void Server::updateState(log_entry log)
{
    // TO ADD: input check and sanitizing
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

void Server::finish()
{
}
