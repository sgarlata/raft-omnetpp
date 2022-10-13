/*
 * server.cc
 *
 *  Created on: 13 mar 2022
 *      Author: ste_dochio
 */
#include <stdio.h>
#include <string.h>
#include <omnetpp.h>
#include <random>
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
    int numberOfServers;
    int numberOfClients;
    double reliability;
    VoteReply *voteReply;
    VoteRequest *voteRequest;
    HeartBeats *heartBeat;
    HeartBeatResponse *heartBeatResponse;
    LogMessage *logMessage;
    LogMessageResponse *logMessageResponse;
    TimeOutNow *timeout;
protected:
    virtual void initialize() override;
    virtual void finish() override;
    virtual void handleMessage(cMessage *msg) override;
public:
    virtual ~Switch();
};

Define_Module(Switch);

void Switch::initialize()
{
    numberOfServers = getParentModule()->par("numServer");
    numberOfClients = getParentModule()->par("numClient");
    reliability = getParentModule()->par("channelsReliability");
}
// here i redefine handleMessage method
// invoked every time a message enters in the node
void Switch::handleMessage(cMessage *msg)
{
    voteReply = dynamic_cast<VoteReply *>(msg);
    voteRequest = dynamic_cast<VoteRequest *>(msg);
    heartBeat = dynamic_cast<HeartBeats *>(msg);
    heartBeatResponse = dynamic_cast<HeartBeatResponse *>(msg);
    logMessage = dynamic_cast<LogMessage *>(msg);
    logMessageResponse = dynamic_cast<LogMessageResponse *>(msg);
    timeout = dynamic_cast<TimeOutNow *>(msg);

    bool switchIsFaulty = false;
    if (uniform(0,1) > reliability)
        switchIsFaulty = true;

    // PACKET IS LOST
    if (switchIsFaulty)
    {
        bubble("A packet is lost!");
        EV << "Lost message " + std::to_string(msg->getId());
    }

    // PACKET IS CORRECTLY FORWARDED
    else if ((voteRequest != nullptr) && (gate("gateSwitch$o",  voteRequest->getCandidateAddress())->isConnected()))
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

    else if ((voteReply != nullptr) && (gate("gateSwitch$o", voteReply->getLeaderAddress())->isConnected()))
    {
        int dest = voteReply->getLeaderAddress();
        VoteReply *voteReplyForward = voteReply->dup();
        send(voteReplyForward, "gateSwitch$o", dest);
    }

    else if ((heartBeat != nullptr) && (gate("gateSwitch$o",  heartBeat->getDestAddress())->isConnected()))
    {
        int dest = heartBeat->getDestAddress();
        HeartBeats *heartBeatForward = heartBeat->dup();
        send(heartBeatForward, "gateSwitch$o", dest);
    }

    else if ((heartBeatResponse != nullptr) && (gate("gateSwitch$o",  heartBeatResponse->getLeaderAddress())->isConnected()))
    {
        int dest = heartBeatResponse->getLeaderAddress();
        HeartBeatResponse *responseForward = heartBeatResponse->dup();
        send(responseForward, "gateSwitch$o", dest);
    }

    else if ((timeout != nullptr) && (gate("gateSwitch$o",  timeout->getDestAddress())->isConnected()))
    {
        int dest = timeout->getDestAddress();
        TimeOutNow *responseForward = timeout->dup();
        send(responseForward, "gateSwitch$o", dest);
    }

    else if ((logMessage != nullptr) && (gate("gateSwitch$o", logMessage->getLeaderAddress())->isConnected()))
    {
        int dest = logMessage->getLeaderAddress();
        LogMessage *logMessageForward = logMessage->dup();
        send(logMessageForward, "gateSwitch$o", dest);
    }

    else if ((logMessageResponse != nullptr) && (gate("gateSwitch$o",  logMessageResponse->getClientAddress())->isConnected()))
    {
        int dest = logMessageResponse->getClientAddress();
        LogMessageResponse *responseForward = logMessageResponse->dup();
        send(responseForward, "gateSwitch$o", dest);
    }

}

Switch::~Switch()
{
    cancelAndDelete(voteReply);
    cancelAndDelete(voteRequest);
    cancelAndDelete(heartBeat);
    cancelAndDelete(heartBeatResponse);
    cancelAndDelete(logMessage);
    cancelAndDelete(logMessageResponse);
    cancelAndDelete(timeout);
}

void Switch::finish()
{
    cancelAndDelete(voteReply);
    cancelAndDelete(voteRequest);
    cancelAndDelete(heartBeat);
    cancelAndDelete(heartBeatResponse);
    cancelAndDelete(logMessage);
    cancelAndDelete(logMessageResponse);
    cancelAndDelete(timeout);
}

