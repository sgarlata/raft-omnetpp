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

class Switch : public cSimpleModule
{

private:
    cMessage *message;

protected:
    virtual void handleMessage(cMessage *msg) override;
};

Define_Module(Switch);

// here i redefine handleMessage method
// invoked every time a message enters in the node
void Switch::handleMessage(cMessage *msg)
{
    Ping *ping = dynamic_cast<Ping *>(msg);
    VoteReply *voteReply = dynamic_cast<VoteReply *>(msg);
    VoteRequest *voteRequest = dynamic_cast<VoteRequest *>(msg);
    LeaderElection *leaderElection = dynamic_cast<LeaderElection *>(msg);
    HeartBeats *heartBeat = dynamic_cast<HeartBeats *>(msg);
    HeartBeatResponse *heartBeatResponse = dynamic_cast<HeartBeatResponse *>(msg);
    LogMessage *logMessage = dynamic_cast<LogMessage *>(msg);
    LogMessageResponse *logMessageResponse = dynamic_cast<LogMessageResponse *>(msg);
    TimeOutNow *timeout = dynamic_cast<TimeOutNow *>(msg);

    if (voteRequest != nullptr)
    {
        int srcAddress = voteRequest->getCandidateAddress();
        // now i send in broadcast to all other server the vote request
        for (cModule::GateIterator i(this); !i.end(); i++)
        {
            cGate *gate = *i;
            // FROM omnet++ DOCUMENTATION: you cannot use the same message pointer in all send() calls,
            // what you have to do instead is create copies (duplicates)
            VoteRequest *voteRequestForward = voteRequest->dup();
            // dst server index
            int h = (gate)->getIndex();
            const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
            std::string ex = "server";
            // to avoid message to client and self message
            if (h != srcAddress && name == ex)
            {
                send(voteRequestForward, gate->getName(), gate->getIndex());
            }
        }
    }

    if (voteReply != nullptr)
    {
        int dest = voteReply->getLeaderAddress();
        VoteReply *voteReplyForward = voteReply->dup();
        send(voteReplyForward, "gateSwitch$o", dest);
    }

    if (heartBeat != nullptr)
    {
        int dest = heartBeat->getDestAddress();
        HeartBeats *heartBeatForward = heartBeat->dup();
        send(heartBeatForward, "gateSwitch$o", dest);
    }

    if (heartBeatResponse != nullptr)
    {
        int dest = heartBeatResponse->getLeaderAddress();
        HeartBeatResponse *responseForward = heartBeatResponse->dup();
        send(responseForward, "gateSwitch$o", dest);
    }

    if (timeout != nullptr)
    {
        int dest = timeout->getDestAddress();
        TimeOutNow *responseForward = timeout->dup();
        send(responseForward, "gateSwitch$o", dest);
    }

    if (logMessage != nullptr)
    {
        int dest = logMessage->getLeaderAddress();
        LogMessage *logMessage = logMessage->dup();
        send(logMessage, "gateSwitch$o", dest);
    }

    if (logMessageResponse != nullptr)
    {
        int dest = logMessageResponse->getClientAddress();
        LogMessageResponse *responseForward = logMessageResponse->dup();
        send(responseForward, "gateSwitch$o", dest);
    }

    if (logMessageResponse != nullptr)
    {
        int dest = logMessageResponse->getClientAddress();
        LogMessageResponse *responseForward = logMessageResponse->dup();
        send(responseForward, "gateSwitch$o", dest);
    }
}
