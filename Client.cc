/*
 * client.cc
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
#include "LogMessage_m.h"
#include "LogMessageResponse_m.h"

using namespace omnetpp;

class Client : public cSimpleModule
{
private:
    bool iAmDead;     // this is useful to shut down a client
    int numberToSend; // this is the number that the client send to the leader, we register this number into the log

    cMessage *failureMsg;  // this message is useful to shut down a client
    cMessage *recoveryMsg; // this message is useful to revive a client
    cMessage *sendToLog;   // this is a message only for leader server, this message contains the number for log

    cGate *currLeaderOutputGate;

    int leaderAddress;
    int networkAddress;
    std::vector<int> configuration;

    // Each command must have a unique ID. Solution: the ID (within the network) of the given Client module first, followed by a counter.
    int commandCounter;

    int randomValue;
    int intToConvert;
    char randomVarName;
    char randomOperation;

protected:
    virtual void initialize() override;
    virtual void handleMessage(cMessage *msg) override;
    virtual void finish() override;
    virtual void sendLogMessage(char varName); // this method is useful to generate a message that a client have to send to the log in the leader server (WORK IN PROGRESS)
    virtual void initializeConfiguration();
    virtual char convertToChar(int operation);
};

Define_Module(Client);

void Client::initialize()
{

    WATCH(iAmDead);
    WATCH_VECTOR(configuration);
    WATCH(leaderAddress);
    iAmDead = false;

    networkAddress = gate("gateClient$i", 0)->getPreviousGate()->getIndex();
    initializeConfiguration();

    leaderAddress = intuniform(0, configuration.size()); // The first request is sent to a random server
    commandCounter = 0;

    intToConvert = intuniform(65, 90);
    randomVarName = (char)intToConvert;

    double realProb = getParentModule()->par("clientsDeadProbability");
    double maxDeathStart = getParentModule()->par("clientsMaxDeathStart");

    // We define a probability of death and we start a self message that will "shut down" some nodes
    double deadProbability = uniform(0, 1);
    if (deadProbability < realProb)
    {
        double randomDelay = uniform(1, maxDeathStart);
        failureMsg = new cMessage("failureMsg");
        EV
            << "Here is client[" + std::to_string(this->getId()) + "]: I will be dead in " + std::to_string(randomDelay) + " seconds...\n";
        scheduleAt(simTime() + randomDelay, failureMsg);
    }

    // ###################################################################################
    sendLogMessage('x');
}

// the client send only a number to the leader server that insert this number into the log;
// than the client receive a message from the leader that confirms this operation.
void Client::handleMessage(cMessage *msg)
{
    Ping *ping = dynamic_cast<Ping *>(msg);
    LogMessageResponse *response = dynamic_cast<LogMessageResponse *>(msg);

    // ############################################### RECOVERY BEHAVIOUR ###############################################

    if (msg == failureMsg)
    {
        // Change status
        iAmDead = true;

        // Schedule Recovery Message
        recoveryMsg = new cMessage("recoveryMsg");
        double maxDeathDuration = getParentModule()->par(
            "clientsMaxDeathDuration");
        double randomFailureTime = uniform(5, maxDeathDuration);
        EV
            << "\nClient[" + std::to_string(this->getIndex()) + "] is dead for about: [" + std::to_string(randomFailureTime) + "]\n";
        scheduleAt(simTime() + randomFailureTime, recoveryMsg);
    }

    else if (msg == recoveryMsg)
    {
        iAmDead = false;
        EV << "I'm back, let's start working again!\n";
        // here i kill again clients in order to have a simulations where all clients continuously goes down
        double maxDeathStart1 = uniform(1, 10);
        double realProbability1 = 0.9;
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

    // ################################################ NORMAL BEHAVIOUR ################################################

    else if (iAmDead)
    {
        EV
            << "At the moment I'm dead so I can't react to this message, sorry \n";
    }

    else if (!iAmDead)
    {
        if (ping != nullptr)
        {
            int execIndx = ping->getExecIndex();
            this->currLeaderOutputGate = gateHalf(
                ping->getArrivalGate()->getName(), cGate::OUTPUT,
                ping->getArrivalGate()->getIndex());

            bubble("message from server received");
            int leaderServerId = ping->getLeaderId();
            delete ping;
            Ping *pong = new Ping("ping");
            pong->setClientIndex(this->getIndex());
            pong->setExecIndex(leaderServerId);
            send(pong, (currLeaderOutputGate)->getName(),
                 (currLeaderOutputGate)->getIndex());
            bubble("here i sent a new ping");
        }
    }

    if (response != nullptr)
    {
        // // Wrong leader
        if (response->getLeaderAddress !=)

            // Request acknowledged
            if (response->getSucceded() == true)
            {
            }

            // Request NOT acknowledged
            else
            {
            }
    }
}

// It sends a log message, under the assumption that the client already knows a leader
void Client::sendLogMessage(char varName)
{
    // Preparation of random values
    int intToChar = intuniform(0, 2);
    char randomOperation = convertToChar(intToChar);
    randomValue = intuniform(0, 1000);

    LogMessage *logMessage = new LogMessage("logMessage");
    logMessage->setClientAddress(networkAddress);
    logMessage->setOperandName(varName);
    logMessage->setOperandValue(randomValue);
    logMessage->setOperation(randomOperation);
    logMessage->setSerialNumber(commandCounter++);
    logMessage->setLeaderAddress(leaderIndex);
    send(logMessage, "gateServer$o", 0);
    bubble("Sending a new command");
}

void Client::initializeConfiguration()
{
    cModule *Switch = gate("gateClient$i", 0)->getPreviousGate()->getOwnerModule();
    std::string serverString = "client";
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

char Client::convertToChar(int operation)
{
    switch (intToChar)
    {
    case 0:
        return s;
        break;

    case 1:
        return a;
        break;

    case 2:
        return m;
        break;
    }
}

void Client::finish()
{
}
