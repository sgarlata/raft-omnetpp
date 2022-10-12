/*
 * server.cc
 *
 *  Created on: 13 mar 2022
 *      Author: ste_dochio
 */

// Destructor
#include "Server.h"

Server::~Server()
{
    cancelAndDelete(failureMsg);
    cancelAndDelete(recoveryMsg);
    cancelAndDelete(electionTimeoutExpired);
    cancelAndDelete(heartBeatsReminder);
    cancelAndDelete(applyChangesMsg);
    cancelAndDelete(leaderTransferFailed);
    cancelAndDelete(minElectionTimeoutExpired);
    cancelAndDelete(catchUpRoundTimeout);
}

void Server::initialize()
{
    cDisplayString &dispStr = getDisplayString();
    dispStr.parse("i=device/server2,bronze");

    WATCH(crashed);
    WATCH(commitIndex);
    WATCH(lastApplied);
    WATCH(currentTerm);
    WATCH_VECTOR(configuration);
    WATCH(leaderAddress);
    WATCH_VECTOR(nextIndex);
    WATCH_VECTOR(matchIndex);
    WATCH(logEntries);
    WATCH(var_X);
    WATCH(var_Y);

    numClient = getParentModule()->par("numClient");
    numServer = getParentModule()->par("numServer");
    serverCrashProbability = getParentModule()->par("serverCrashProbability");
    maxCrashDelay = getParentModule()->par("serverMaxCrashDelay");
    maxCrashDuration = getParentModule()->par("serverMaxCrashDuration");
    minElectionTimeout = par("minElectionTimeout");
    maxElectionTimeout = par("maxElectionTimeout");
    applyChangesPeriod = par("applyChangePeriod");
    heartbeatsPeriod = par("heartbeatsPeriod");

    currentTerm = 1;
    lastVotedTerm = 0;
    serverState = FOLLOWER;

    numberVoteReceived = 0;
    acceptVoteRequest = true;
    leaderAddress = -1;

    networkAddress = gate("gateServer$i", 0)->getPreviousGate()->getIndex();
    initializeRequestTable(4);
    initializeConfiguration();
    var_X = 1;
    var_Y = 1;
    numberVotingMembers = configuration.size();

    catchUpPhaseRunning = false;
    maxNumberRound = par("maxNumberRound");
    catcUpPhaseRoundDuration = par("catchUpPhaseRoundDuration");

    for (int i = 0; i < configuration.size(); ++i)
    {
        nextIndex.push_back(0);
        matchIndex.push_back(-1);
    }

    // INITIALIZE AUTOMESSAGES
    minElectionTimeoutExpired = new cMessage("MinElectionTimeoutExpired");
    heartBeatsReminder = new cMessage("heartBeatsReminder");

    electionTimeoutExpired = new cMessage("ElectionTimeoutExpired");
    double randomTimeout = uniform(minElectionTimeout, maxElectionTimeout);
    scheduleAt(simTime() + randomTimeout, electionTimeoutExpired);

    applyChangesMsg = new cMessage("ApplyChangesToFiniteStateMachine");
    scheduleAt(simTime() + applyChangesPeriod, applyChangesMsg);

    scheduleCrashMsg = new cMessage("Schedule next crash");
    scheduleAt(simTime(), scheduleCrashMsg);

}

void Server::handleMessage(cMessage *msg)
{
    VoteReply *voteReply = dynamic_cast<VoteReply *>(msg);
    VoteRequest *voteRequest = dynamic_cast<VoteRequest *>(msg);
    HeartBeats *heartBeat = dynamic_cast<HeartBeats *>(msg);
    HeartBeatResponse *heartBeatResponse = dynamic_cast<HeartBeatResponse *>(msg);
    LogMessage *logMessage = dynamic_cast<LogMessage *>(msg);
    TimeOutNow *timeOutnow = dynamic_cast<TimeOutNow *>(msg);

    // ############################################### RECOVERY BEHAVIOUR ###############################################

    if(msg == failureMsg)
    {
        bubble("CRASHED");
        cDisplayString &dispStr = getDisplayString();
        dispStr.parse("i=device/server2,red");
        crashed = true;
        double randomFailureTime = uniform(5, maxCrashDuration);
        EV
        << "\nServer ID: [" + to_string(getIndex(networkAddress)) + "] is dead for about: [" + to_string(randomFailureTime) + "]\n";
        recoveryMsg = new cMessage("recoveryMsg");
        scheduleAt(simTime() + randomFailureTime, recoveryMsg);
    }

    if (msg == recoveryMsg)
    {
        crashed = false;
        EV << "Here is server[" + to_string(getIndex(networkAddress)) + "]: I am no more dead... \n";
        bubble("im returned alive");
        cDisplayString &dispStr = getDisplayString();

        // if this server returns alive it has to be a follower because there might be another server serving as the leader
        if (serverState != NON_VOTING_MEMBER)
        {
            dispStr.parse("i=device/server2,bronze");
            serverState = FOLLOWER;
            numberVoteReceived = 0;
            acceptVoteRequest = true;
            // restart election count-down
            electionTimeoutExpired = new cMessage("NewElectionTimeoutExpired");
            double randomTimeout = uniform(minElectionTimeout, maxElectionTimeout);
            scheduleAt(simTime() + randomTimeout, electionTimeoutExpired);
            // restart the periodical updates of the FSM

        }
        else
        {
            dispStr.parse("i=device/server2,blue");
        }

        applyChangesMsg = new cMessage("Apply changes to State Machine");
        scheduleAt(simTime() + applyChangesPeriod, applyChangesMsg);
        // Schedule next crashes
        scheduleCrashMsg = new cMessage("Schedule next crash");
        scheduleAt(simTime(), scheduleCrashMsg);
    }

    else if (crashed)
    {
        EV << "SERVER CRASHED: cannot react to messages";
    }

    // ################################################ NORMAL BEHAVIOUR ################################################
    else if (crashed == false)
    {
        ////
        // ELECTION TIMEOUT IS EXPIRED, START A NEW ELECTION
        if (msg == electionTimeoutExpired and serverState != LEADER and serverState != NON_VOTING_MEMBER)
        {
            // New election needed
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

            // send broadcast vote request to the switch
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
            if(gate("gateServer$o", 0)->isConnected())
                send(voteRequest, "gateServer$o", 0);
        }

        else if (msg == scheduleCrashMsg)
        {
            double crashProbabilitySample = uniform(0, 1);
            if (crashProbabilitySample < serverCrashProbability)
            {
                double randomDelay = uniform(1, maxCrashDelay);
                failureMsg = new cMessage("failureMsg");
                EV
                << "Here is server" + to_string(getIndex(networkAddress)) + ": I will crash in " + to_string(randomDelay) + " seconds...\n";
                scheduleAt(simTime() + randomDelay, failureMsg);
            }
            else
            {
                scheduleCrashMsg = new cMessage("Schedule next crash");
                double randomTimeout = uniform(0, maxCrashDelay);
                scheduleAt(simTime() + randomTimeout, scheduleCrashMsg);
            }
        }

        else if (msg == catchUpRoundTimeout)
        {
            catchUpCountdownEnded = true;
        }

        ////
        // VOTE REQUEST RECEIVED
        else if (voteRequest != nullptr && acceptVoteRequest && serverState != NON_VOTING_MEMBER)
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

        else if (voteReply != nullptr)
        { // here i received a vote so i increment the current term vote

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
                    // remain leader until the first failure, furthermore i have to reset all state variables, including nextIdex and matchIndex
                    cancelEvent(electionTimeoutExpired);
                    serverState = LEADER;
                    leaderAddress = networkAddress;
                    numberVoteReceived = 0;
                    catchUpPhaseRunning = false;
                    for (int serverIndex = 0; serverIndex < nextIndex.size(); serverIndex++)
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

                    if(!leaderTransferPhase)
                    {
                        // add NOP to log
                        log_entry NOP;
                        NOP.clientAddress = NO_CLIENT;
                        NOP.entryTerm = currentTerm;
                        NOP.operandName = 'X';
                        NOP.operandValue = 0;
                        NOP.operation = 'A';
                        NOP.entryLogIndex = logEntries.size();
                        // update next index and match index for leader.
                        nextIndex[getIndex(networkAddress)]++;
                        matchIndex[getIndex(networkAddress)]++;
                        logEntries.push_back(NOP);
                        // periodical HeartBeat

                    }
                    else
                    {
                        log_entry REMOVE;
                        REMOVE.clientAddress = changingServerEntry.clientAddress;
                        REMOVE.entryTerm = currentTerm;
                        REMOVE.entryLogIndex = logEntries.size();
                        REMOVE.addressServerToRemove = changingServerEntry.addressServerToRemove;
                        // update next index and match index for leader.
                        nextIndex[getIndex(networkAddress)]++;
                        matchIndex[getIndex(networkAddress)]++;
                        logEntries.push_back(REMOVE);
                        int toRemove = REMOVE.addressServerToRemove;
                        configuration.erase(remove(configuration.begin(), configuration.end(), toRemove));
                    }
                    heartBeatsReminder = new cMessage("heartBeatsReminder");
                    scheduleAt(simTime(), heartBeatsReminder);
                }
            }
            leaderTransferPhase = false;
        }

        ////
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

            // A non voting member saves the commit index of the first message sent by the leader
            if (serverState == NON_VOTING_MEMBER && heartBeat->getEmpty())
            {
                catchUpTargetIndex = heartBeat->getLeaderCommit();
            }

            /* ****************
             * LOG IS REFUSED *
             ******************/
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
                if(serverState != NON_VOTING_MEMBER)
                {
                    cDisplayString &dispStr = getDisplayString();
                    dispStr.parse("i=device/server2, bronze");
                    serverState = FOLLOWER;
                }
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
                    /*******************
                     * LOG IS ACCEPTED *
                     *******************/
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
                            lastRequestFromClient->lastLoggedIndex = logEntries.size() - 1;
                        }
                        // CONFIGURATION CHANGE
                        // ADD SERVER
                        int toAdd = heartBeat->getEntry().addressServerToAdd;
                        if(toAdd >= 0 && !(*find(configuration.begin(), configuration.end(), toAdd) == toAdd))
                        {
                            configuration.push_back(heartBeat->getEntry().addressServerToAdd);
                            // if a new server arrives for the first time, then extend nextIndex and matchIndex
                            if(not(heartBeat->getEntry().operandValue < nextIndex.size()))
                            {
                                nextIndex.push_back(0);
                                matchIndex.push_back(-1);
                            }
                            if(serverState == NON_VOTING_MEMBER && heartBeat->getEntry().addressServerToAdd == networkAddress)
                            {
                                // become follower
                                stepdown(heartBeat->getLeaderCurrentTerm());
                            }

                        }
                        // REMOVE SERVER
                        else if (heartBeat->getEntry().addressServerToRemove >= 0)
                        {
                            int toRemove = heartBeat->getEntry().addressServerToRemove;
                            configuration.erase(remove(configuration.begin(), configuration.end(), toRemove), configuration.end());
                        }
                        /* NOTE: if a replica receives the same entry twice, it simply ignores the second one and sends an ACK.
                         * @ensure (5) If leaderCommit > commitIndex, set commitIndex = min(leaderCommit, index of last new entry)
                         * index of last */
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

        ////
        // RECEIVED RESPONSE FROM A FOLLOWER
        if (heartBeatResponse != nullptr)
        {
            int followerAddr = heartBeatResponse->getFollowerAddress();
            int followerIndex = getIndex(followerAddr);
            int followerLogLength = heartBeatResponse->getLogLength();
            int followerMatchIndex = heartBeatResponse->getMatchIndex();
            if (heartBeatResponse->getSucceded())
            {
                // heartBeat accepted
                matchIndex[followerIndex] = followerMatchIndex;
                nextIndex[followerIndex] = matchIndex[followerIndex] + 1;

                // catch up phase round ends
                if (catchUpPhaseRunning && followerAddr == changingServerEntry.addressServerToAdd && matchIndex[followerIndex] == catchUpTargetIndex)
                {
                    endCatchUpRound();
                }
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
            startAcceptVoteRequestCountdown();
        }

        ////
        // SUFFICIENT TIME IS PASSED, VOTE REQUESTS CAN BE ACCEPTED
        if (msg == minElectionTimeoutExpired)
        {
            acceptVoteRequest = true;
        }

        ////
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
            scheduleAt(simTime() + applyChangesPeriod, applyChangesMsg);
        }

        ////
        // LOG MESSAGE REQUEST RECEIVED, it is ignored only if leader transfer process is going on
        if (logMessage != nullptr && !leaderTransferPhase && leaderAddress >= 0)
        {
            int serialNumber = logMessage->getSerialNumber();
            int clientAddress = logMessage->getClientAddress();
            bool alreadyReceived = false;
            last_req* lastReqHashEntry = getLastRequest(clientAddress);

            if (lastReqHashEntry != nullptr)
            {
                int lastSerial = lastReqHashEntry->lastArrivedSerial;
                int index = lastReqHashEntry->lastLoggedIndex;

                if(lastSerial >= serialNumber)
                {
                    alreadyReceived = true;
                    if(index > commitIndex)
                    {
                        // NACK: request yet to commit
                        bubble("This request is already in the log, but it is still uncommitted");
                        sendResponseToClient(clientAddress, serialNumber, false, false);
                    }
                    else if (logEntries[index].clientAddress == logMessage->getClientAddress())
                    {
                        // ACK: request has already been committed
                        bubble("This request has already been committed!");
                        sendResponseToClient(clientAddress, serialNumber, true, false);
                    }
                }
            }
            else
            {
                // first message from the client: add an entry to the table
                lastReqHashEntry = addNewRequestEntry(clientAddress);
            }
            lastReqHashEntry->lastArrivedSerial = logMessage->getSerialNumber();

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
                    newEntry.addressServerToAdd = logMessage->getServerToAdd();
                    newEntry.addressServerToRemove = logMessage->getServerToRemove();
                    newEntry.entryLogIndex = logEntries.size();
                    if (logMessage->getServerToRemove() < 0 and logMessage->getServerToAdd() < 0)
                    {
                        // ordinary entry
                        // update next index and match index for leader.
                        nextIndex[getIndex(networkAddress)]++;
                        matchIndex[getIndex(networkAddress)]++;
                        logEntries.push_back(newEntry);
                        // update last received index
                        lastReqHashEntry->lastLoggedIndex = newEntry.entryLogIndex;
                    }
                    else
                    {
                        // change configuration entry
                        startMembershipChangeProcedure(newEntry);
                    }
                }
            }
        }

        ////
        // FORCED TIMEOUT MESSAGE DUE TO LEADER TRANSFER
        else if (timeOutnow != nullptr)
        {
            leaderTransferPhase = true;
            log_entry rm_serv ;
            rm_serv.addressServerToRemove = timeOutnow->getServerToRemove();
            rm_serv.clientAddress = timeOutnow->getClientAddress();
            rm_serv.serialNumber = timeOutnow->getSerialNumber();
            changingServerEntry = rm_serv;
            cancelEvent(electionTimeoutExpired);
            electionTimeoutExpired = new cMessage("NewElectionTimeoutExpired");
            scheduleAt(simTime(), electionTimeoutExpired);
        }

        ////
        // ABORT LEADER TRANSFER PROCESS
        else if (msg == leaderTransferFailed)
        {
            leaderTransferPhase = false;
            timeOutNowSent = false;
            int managerAddr = changingServerEntry.clientAddress;
            int serial = changingServerEntry.serialNumber;
            sendResponseToClient(managerAddr, serial, false, false);
        }

        ////
        // SEND HEARTBEAT (AppendEntries RPC)
        if (msg == heartBeatsReminder)
        {
            int logSize = logEntries.size();
            int lastLogIndex = logSize - 1;
            int nextLogIndex;
            int followerAddr;
            int followerIndex;
            vector<int> toUpdate = configuration;
            if(catchUpPhaseRunning) {
                toUpdate.push_back(changingServerEntry.addressServerToAdd);
            }

            for (int i = 0; i < toUpdate.size(); i++)
            {
                followerAddr = toUpdate[i];
                followerIndex = getIndex(followerAddr);
                HeartBeats *RPCAppendEntriesMsg = new HeartBeats("i'm the leader");
                // to avoid message to client and self message
                if (followerAddr != this->networkAddress)
                {
                    nextLogIndex = nextIndex[followerIndex];
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
            scheduleAt(simTime() + heartbeatsPeriod, heartBeatsReminder);
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
    reply->setFollowerAddress(networkAddress);
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
    reply->setFollowerAddress(networkAddress);
    if(gate("gateServer$o", 0)->isConnected())
        send(reply, "gateServer$o", 0);
}

void Server::tryLeaderTransfer(int addr)
{
    TimeOutNow *timeOutNow = new TimeOutNow("TIMEOUT_NOW");
    timeOutNow->setDestAddress(addr);
    timeOutNow->setClientAddress(changingServerEntry.clientAddress);
    timeOutNow->setServerToRemove(networkAddress);
    timeOutNow->setSerialNumber(changingServerEntry.serialNumber);

    if(gate("gateServer$o", 0)->isConnected())
        send(timeOutNow, "gateServer$o", 0);
    timeOutNowSent = true;

    leaderTransferFailed = new cMessage("Leader transfer failed");
    scheduleAt(simTime() + maxElectionTimeout, leaderTransferFailed);
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

    cancelEvent(minElectionTimeoutExpired);

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

int Server::getAddress()
{
    return networkAddress;
}

int Server::getIndex(int address)
{
    if(address < numServer)
        return address;
    else
        return address - numClient - 1; // -1 it is because of the configuration manager
}

// If there exists an N such that N > commitIndex, a majority of matchIndex[i] >= N,
// and log[N].term == currentTerm: set commitIndex = N
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
                for (int nextCommitIndex = commitIndex + 1; nextCommitIndex <= i; nextCommitIndex++)
                {
                    commitIndex = nextCommitIndex;
                    // here we update the hash table, registering the new commit
                    clientAddr = logEntries[nextCommitIndex].clientAddress;
                    serialNumber = logEntries[nextCommitIndex].serialNumber;
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

    if (needsToBeProcessed(logSerNum, logClientAddr) && (log.addressServerToAdd < 0 && log.addressServerToRemove < 0))
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


void Server::stepdown(int newCurrentTerm)
{
    cancelEvent(electionTimeoutExpired);
    cancelEvent(heartBeatsReminder);
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
    char buf[300];
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

void Server::startMembershipChangeProcedure(log_entry changeConfigEntry)
{
    int toAdd = changeConfigEntry.addressServerToAdd;
    int toDel = changeConfigEntry.addressServerToRemove;
    if(toAdd >= 0)
    {
        // START CATCH UP PHASE (PRE-LOG)
        changingServerEntry = changeConfigEntry;
        int serverIndex = getIndex(changeConfigEntry.addressServerToAdd);
        if (serverIndex >= matchIndex.size())
        {
            nextIndex.push_back(logEntries.size());
            matchIndex.push_back(-1);
        }
        catchUpPhaseRunning = true;
        catchUpRoundNumber = 0;
        catchUpTargetIndex = logEntries.size() - 1;
        catchUpRoundTimeout = new cMessage("Round timeout expired");
        scheduleAt(simTime() + maxElectionTimeout, catchUpRoundTimeout);
        cancelEvent(heartBeatsReminder);
        scheduleAt(simTime(), heartBeatsReminder);
    }
    else
    {
        if(toDel == networkAddress)
        {
            leaderTransferPhase = true;
            changingServerEntry = changeConfigEntry;
        }
        else
        {
            EV << "Removing server" + to_string(getIndex(toDel));
            configuration.erase(remove(configuration.begin(), configuration.end(), toDel));
            nextIndex[getIndex(networkAddress)]++;
            matchIndex[getIndex(networkAddress)]++;
            numberVotingMembers  = configuration.size();
            logEntries.push_back(changeConfigEntry);
        }
    }
}

void Server::configureServer(vector<int> initialConfiguration)
{
    cancelEvent(electionTimeoutExpired);
    int numClient = getParentModule()->par("numClient");
    configuration = initialConfiguration;

    serverState= NON_VOTING_MEMBER;
    cDisplayString &dispStr = getDisplayString();
    dispStr.parse("i=device/server2,blue");

    nextIndex.clear();
    matchIndex.clear();
    for (int i = 0; i < configuration.size(); ++i)
    {
        nextIndex.push_back(0);
        matchIndex.push_back(-1);
    }
}

void Server::endCatchUpRound()
{

    int managerAddr = changingServerEntry.clientAddress;
    int serial = changingServerEntry.serialNumber;

    if(catchUpCountdownEnded)
    {
        // increment the round number
        catchUpRoundNumber++;
        catchUpCountdownEnded = false;
        if (catchUpRoundNumber >= maxNumberRound)
        {
            // FAIL
            catchUpPhaseRunning = false;
            catchUpRoundNumber = 0;
            // send NACK: unsuccessful catch up
            sendResponseToClient(managerAddr, serial, false, false);
        }
        else
        {
            // next round
            catchUpTargetIndex = logEntries.size() - 1;
            catchUpRoundTimeout = new cMessage("Round timeout expired");
            scheduleAt(simTime() + maxElectionTimeout, catchUpRoundTimeout);

        }

    }
    else
    {
        // SUCCESSFUL CATCH UP
        cancelEvent(catchUpRoundTimeout);
        catchUpPhaseRunning = false;
        catchUpCountdownEnded = false;
        int serverToAddNum = getIndex(changingServerEntry.addressServerToAdd);
        // SUCCESSFUL catch up phase.

        // START CLUSTER MEMBERSHIP PHASE
        changingServerEntry.entryLogIndex = logEntries.size();
        logEntries.push_back(changingServerEntry);
        configuration.push_back(changingServerEntry.addressServerToAdd);
    }
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
    cancelAndDelete(catchUpRoundTimeout);
}
