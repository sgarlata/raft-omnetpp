/*
 * client.cc
 *
 *  Created on: 13 mar 2022
 *      Author: ste_dochio
 */
#include <stdio.h>
#include <string.h>
#include "Server.h"
#include <omnetpp.h>
#include "Ping_m.h"
#include <algorithm>
#include <list>
#include <random>
#include <sstream>
#include "LogMessage_m.h"
#include "LogMessageResponse_m.h"

using namespace omnetpp;
using std::vector;
using std::__cxx11::to_string;
using std::string;
using std::to_string;
using std::count;

class ConfigurationManager : public cSimpleModule
{
private:
    bool crashed;

    int numberOfServers;
    int numberOfClients;

    cMessage *failureMsg;
    cMessage *recoveryMsg;
    cMessage *sendConfigurationChange;
    cMessage *reqTimeoutExpired;       // autoMessage to check whether the last request was acknowledged or not
    cMessage *tryAgainMsg;
    cMessage *shutDownDeletedServer;

    int leaderAddress;
    int randomIndex; // Random index for the leader within the client's configuration vector
    int networkAddress;
    vector<int> initialConfiguration; // new servers are initialized with this configuration
    vector<int> currentConfiguration;

    bool free = true; // True if the last request was acknowledged by the leader

    LogMessage *lastLogMessage = nullptr;

    cModule *Switch;
    double crashProbability;
    double crashDelay;

protected:
    virtual void initialize() override;
    virtual void handleMessage(cMessage *msg) override;
    virtual void finish() override;
    virtual void changeConfiguration();
    virtual vector<int> initializeConfiguration();
    virtual void addNewServer();
    virtual void removeServer(int toRemoveAddress);

};

Define_Module(ConfigurationManager);

void ConfigurationManager::initialize()
{
    crashed = false;

    numberOfServers = getParentModule()->par("numServer");
    numberOfClients = getParentModule()->par("numClient");

    networkAddress = gate("gateConfigurationManager$i", 0)->getPreviousGate()->getIndex();
    Switch = gate("gateConfigurationManager$i", 0)->getPreviousGate()->getOwnerModule();

    initialConfiguration = initializeConfiguration();
    currentConfiguration = initialConfiguration;

    randomIndex = intuniform(0, currentConfiguration.size() - 1); // The first request is sent to a random server
    leaderAddress = currentConfiguration[randomIndex];

    crashProbability = getParentModule()->par("clientsCrashProbability");
    crashDelay = getParentModule()->par("clientsMaxCrashDelay");

    // here expires the first timeout; so the first server with timeout expired sends the first leader election message
    sendConfigurationChange = new cMessage("I start to send entries.");
    double randomTimeout = uniform(0, 1);
    scheduleAt(simTime() + randomTimeout, sendConfigurationChange);
}

// the client send only a number to the leader server that insert this number into the log;
// than the client receive a message from the leader that confirms this operation.
void ConfigurationManager::handleMessage(cMessage *msg)
{
    LogMessageResponse *response = dynamic_cast<LogMessageResponse *>(msg);


    if (msg == failureMsg)
    {
        // Change status
        crashed = true;
        cDisplayString &dispStr = getDisplayString();
        dispStr.parse("i=abstract/penguin_l,red");
        bubble("Configuration manager crashes");
        // Schedule Recovery Message
        recoveryMsg = new cMessage("recoveryMsg");
        double randomFailureTime = uniform(1, 2);
        EV << "Config. Manager will be down for about " + to_string(randomFailureTime) + " seconds\n";
        scheduleAt(simTime() + randomFailureTime, recoveryMsg);
    }

    if (msg == recoveryMsg)
    {
        crashed = false;
        cDisplayString &dispStr = getDisplayString();
        dispStr.parse("i=device/pc,cyan");
        bubble("Config. Manager recover");
        EV << "Config. Manager: I'm back, let's start working again!\n";
        // here we schedule the next client crash
        // restart sending messages
        sendConfigurationChange = new cMessage("Sending requests");
        double randomTimeout = uniform(0, 1);
        scheduleAt(simTime() + randomTimeout, sendConfigurationChange);
    }

    if (crashed)
    {
        EV
        << "Config. Manager CRASHED: cannot react to messages \n";
    }
    else
    {
        if (msg == sendConfigurationChange)
        {
            changeConfiguration();
            // here the timeout has expired; client starts sending logMessage in loop
//            if (free)
//            {
//                changeConfiguration();
//            }
//            else
//            {
//                tryAgainMsg = new cMessage("Try again.");
//                scheduleAt(simTime() + 2, tryAgainMsg);
//            }
        }

        else if (msg == reqTimeoutExpired && !free)
        {
            bubble("Resending after timeout.");
            randomIndex = intuniform(0, currentConfiguration.size() - 1);
            leaderAddress = currentConfiguration[randomIndex];
            lastLogMessage->setLeaderAddress(leaderAddress);
            LogMessage *newMex = lastLogMessage->dup();
            if(gate("gateConfigurationManagert$o", 0)->isConnected())
                send(newMex, "gateConfigurationManager$o", 0);

            reqTimeoutExpired = new cMessage("Timeout expired.");
            scheduleAt(simTime() + 1, reqTimeoutExpired);
        }

        else if (msg == tryAgainMsg)
        {
            LogMessage *newMessage = lastLogMessage->dup();
            if(gate("gateConfigurationManager$o", 0)->isConnected())
                send(newMessage, "gateConfigurationManager$o", 0);
            reqTimeoutExpired = new cMessage("Timeout expired.");
            scheduleAt(simTime() + 1, reqTimeoutExpired);
        }

        else if (response != nullptr)
        {
            // Request acknowledged
            if (response->getSucceded())
            {
                sendConfigurationChange = new cMessage("Send new entry.");
                free = true;
                cancelEvent(reqTimeoutExpired);

                double randomTimeout = uniform(0, 1);
                scheduleAt(simTime() + randomTimeout, sendConfigurationChange);
            }
            else
            {
                cancelEvent(reqTimeoutExpired);

                if(response->getRedirect())
                {
                    // REDIRECT TO ANOTHER SERVER, POSSIBLY THE LEADER
                    bubble("Redirecting command to the leader.");
                    leaderAddress = response->getLeaderAddress();
                    lastLogMessage->setLeaderAddress(leaderAddress);
                    LogMessage *newMessage = lastLogMessage->dup();
                    if(gate("gateConfigurationManager$o", 0)->isConnected())
                        send(newMessage, "gateConfigurationManager$o", 0);
                    reqTimeoutExpired = new cMessage("Timeout expired.");
                    scheduleAt(simTime() + 1, reqTimeoutExpired);
                }
                else
                {
                    // TELL THE CLIENT TO WAIT FOR COMMIT
                    bubble("Waiting for commit.");
                    tryAgainMsg = new cMessage("Try again.");
                    scheduleAt(simTime() + 2, tryAgainMsg);
                }
            }
        }

    }
}

vector<int> ConfigurationManager::initializeConfiguration()
{
    cModule *Switch = gate("gateConfigurationManager$i", 0)->getPreviousGate()->getOwnerModule();
    string serverString = "server";
    vector<int> configuration;

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

    return configuration;
}

void ConfigurationManager::changeConfiguration()
{

//    int choice = intuniform(0,1);
//    if(choice > 0)
//    {
        addNewServer();
//    }
//    else
//    {
//        choice = intuniform(0, currentConfiguration.size() - 1);
//
//        choice = currentConfiguration[choice];
//        removeServer(choice);
//    }
}

void ConfigurationManager::addNewServer()
{
    cModule *Switch = gate("gateConfigurationManager$i", 0)->getPreviousGate()->getOwnerModule();
    cGate *newServerPortIN, *newServerPortOUT;
    cGate *newSwitchPortIN, *newSwitchPortOUT;
    cModule *tempClient;
    int nextServerIndex = numberOfServers;
    numberOfServers++;

    Switch->setGateSize("gateSwitch", Switch->gateSize("gateSwitch$o") + 1);
    int index = gate("gateConfigurationManager$o",0)->getNextGate()->size() - 1;

    newSwitchPortIN = Switch->gate("gateSwitch$i", index);
    newSwitchPortOUT = Switch->gate("gateSwitch$o", index);

    bubble("Adding new server!");
    cModuleType *moduleType = cModuleType::get("Server");
    string i = to_string(numberOfServers);

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

    ((Server *)module)->configureServer(nextServerIndex, initialConfiguration);

}

void ConfigurationManager::removeServer(int toDelete)
{
    cModule *Switch = gate("gateConfigurationManager$i", 0)->getPreviousGate()->getOwnerModule();
    cModule *serverToDelete;

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
        }
    }
}

void ConfigurationManager::finish()
{
}

// ########## CODE FOR CHANNEL LINK CUT ############################################################################

//if(msg == channelLinkProblem){
//    int decision = intuniform(0,1);//if decision is 1 i restore a random link; if it is 0 i delete a random link
//    int gateINOUT = intuniform(0,1);// if gateINOUT is 1 i delete the in gate; else i delete the out gate
//    if (decision == 0){//here i delete a link
//        if(gateINOUT == 0){// i delete an out gate
//            if(gate("gateServer$o", 0)->isConnected())
//                gate("gateServer$o", 0)->disconnect();
//        }else{//i delete the in gate
//            if(Switch->gate("gateSwitch$o",this->getIndex())->isConnected())
//                Switch->gate("gateSwitch$o",this->getIndex())->disconnect();
//        }
//    }else{//here i restore a link with the same method of the delete
//        if(gateINOUT == 0){// i restore an out gate
//            if(!gate("gateServer$o", 0)->isConnected()){
//                cDelayChannel *delayChannelOUT = cDelayChannel::create("myChannel");
//                delayChannelOUT->setDelay(0.1);
//
//                this->gate("gateServer$o", 0)->connectTo(Switch->gate("gateSwitch$i",this->getIndex()), delayChannelOUT);
//            }
//        }
//        else{ //i restore the in gate
//            if(!(Switch->gate("gateSwitch$o",this->getIndex())->isConnected())){
//                cDelayChannel *delayChannelIN = cDelayChannel::create("myChannel");
//                delayChannelIN->setDelay(0.1);
//
//                Switch->gate("gateSwitch$o",this->getIndex())->connectTo(this->gate("gateServer$i", 0),delayChannelIN);
//            }
//        }
//    }
//    channelLinkProblem = new cMessage("another channel connection problem");
//    double randomDelay = uniform(0, 3);
//    scheduleAt(simTime() + randomDelay, channelLinkProblem);
//}
