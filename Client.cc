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

protected:
    virtual void initialize() override;
    virtual void handleMessage(cMessage *msg) override;
    virtual void finish() override;
    virtual void sendLogMessage(char operation, char varName, int value); // this method is useful to generate a message that a client have to send to the log in the leader server
};

Define_Module(Client);

void Client::initialize()
{

    WATCH(iAmDead);
    iAmDead = false;

    double realProb = getParentModule()->par("clientsDeadProbability");
    double maxDeathStart = getParentModule()->par("clientsMaxDeathStart");

    // We define a probability of death and we start a self message that will "shut down" some nodes
    double deadProbability = uniform(0, 1);
    if (deadProbability < realProb)
    {
        double randomDelay = uniform(1, maxDeathStart);
        failureMsg = new cMessage("failureMsg");
        EV
            << "Here is client[" + std::to_string(this->getIndex()) + "]: I will be dead in " + std::to_string(randomDelay) + " seconds...\n";
        scheduleAt(simTime() + randomDelay, failureMsg);
    }
    //##########################HERE I CHECK IF ALL SERVERS ARE LINKED TO CLIENTS#########################################################
    // this is only for debug
    EV << "Lista dei moduli connessi ai gate del client: \n";
    int counter_printed = 1;
    for (cModule::GateIterator i(this); !i.end(); i++)
    {
        cGate *gate = *i;
        int h = (gate)->getPathEndGate()->getOwnerModule()->getId(); // it returns the id of the client or server
        const char *name =
            (gate)->getPathEndGate()->getOwnerModule()->getName(); // it returns a string name as "client" or "server"

        if (h != this->getId())
        {
            EV
                << std::to_string(counter_printed) + ". " + (gate)->getPathEndGate()->getOwnerModule()->getName() + "[" + std::to_string((gate)->getPathEndGate()->getOwnerModule()->getIndex()) + "]\n";
            counter_printed++;
        }
    }
    //###################################################################################
    WATCH(numberToSend);
    // here i sent to the server the first number
    numberToSend = uniform(1, 30);
}

// the client send only a number to the leader server that insert this number into the log;
// than the client receive a message from the leader that confirms this operation.
void Client::handleMessage(cMessage *msg)
{
    Ping *ping = dynamic_cast<Ping *>(msg);
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
}

// It sends a log message, under the assumption that the client already knows a leader
void Client::sendLogMessage(char operation, char varName, int value)
{
    LogMessage *logMessage = new LogMessage("new entry for log");

    logMessage->setClientId(this->getIndex());
    logMessage->setOperandValue(value);
    logMessage->setOperandName(varName);
    logMessage->setOperation(operation);
    send(logMessage, (currLeaderOutputGate)->getName(),
         (currLeaderOutputGate)->getIndex());
}

void Client::finish()
{
}
