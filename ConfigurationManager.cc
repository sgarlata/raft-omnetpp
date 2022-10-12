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
    int newServerNumber;
    int serverToDelete;

    cMessage *failureMsg;
    cMessage *recoveryMsg;
    cMessage *reqTimeoutExpired;       // autoMessage to check whether the last request was acknowledged or not
    cMessage *tryAgainMsg;
    cMessage *shutDownDeletedServer;
    cMessage *timeToChange;
    cMessage *deleteServerMsg;

    Server *removedServer = nullptr;

    int leaderAddress;
    int randomIndex; // Random index for the leader within the client's configuration vector
    int networkAddress;
    int numberAddedServer;
    int commandCounter;
    vector<int> initialConfiguration; // new servers are initialized with this configuration
    vector<int> currentConfiguration;
    vector<int> outOfConfigurationServers;

    bool free = true; // True if the last request was acknowledged by the leader

    LogMessage *lastLogMessage = nullptr;
    LogMessage *notifyLeaderOfChangeConfig = nullptr;

    cModule *Switch;
    double crashProbability;
    double crashDelay;

protected:
    virtual void initialize() override;
    virtual void handleMessage(cMessage *msg) override;
    virtual void finish() override;
    //virtual void changeConfiguration();
    virtual vector<int> initializeConfiguration();
    //virtual vector<int> createNewConfiguration();
    virtual void scheduleNewMessage(char operation, int serverAddress);
    virtual int addNewServer();
    virtual void removeServer(int toRemoveAddress);
    virtual void updateConfiguration();

};

Define_Module(ConfigurationManager);

void ConfigurationManager::initialize()
{
    WATCH(newServerNumber);
    WATCH(serverToDelete);
    WATCH(crashed);
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
    /*sendConfigurationChange = new cMessage("I start to send entries.");
    double randomTimeout = uniform(0, 1);
    scheduleAt(simTime() + randomTimeout, sendConfigurationChange);*/
    commandCounter = 0;

    newServerNumber = intuniform(1,3);
    serverToDelete = intuniform(1,2);
    //here start the timer for a change configuration
    timeToChange = new cMessage("timeToChange");
    scheduleAt(simTime() + 0.5 , timeToChange);
    free = true;
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
        timeToChange = new cMessage("Sending requests");
        double randomTimeout = uniform(0, 1);
        scheduleAt(simTime() + randomTimeout, timeToChange);
    }

    if (crashed)
    {
        EV
        << "Config. Manager CRASHED: cannot react to messages \n";
    }
    else
    {
        if (msg == timeToChange){
            if(free)
            {
                //here i create the new configuration
                if (newServerNumber > 0)
                {
                    // 1) Add server procedure
                    int addedServer = addNewServer();
                    char operation = '+'; //add
                    //now i have to update the leader about the new server in the configuration
                    scheduleNewMessage(operation, addedServer);
                }
                else if (newServerNumber == 0 && serverToDelete > 0)
                {
                    // 2) Now new servers has been added, so we can purge the ones we want to remove from configuration
                    int toDelete = intuniform(0, currentConfiguration.size()-1);
                    bubble("Removing old servers from configuration!");
                    char operation = '-'; //delete
                    scheduleNewMessage(operation,  toDelete);
                }
            }
            else
            {
                tryAgainMsg = new cMessage("Try again.");
                scheduleAt(simTime() + 2, tryAgainMsg);
            }
        }

        else if (msg == reqTimeoutExpired && !free)
        {
            bubble("Resending after timeout.");
            randomIndex = intuniform(0, currentConfiguration.size() - 1);
            leaderAddress = currentConfiguration[randomIndex];
            lastLogMessage->setLeaderAddress(leaderAddress);
            LogMessage *newMex = lastLogMessage->dup();
            if(gate("gateConfigurationManager$o", 0)->isConnected())
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

        else if (msg == deleteServerMsg)
        {
            for(int i = 0; i < outOfConfigurationServers.size(); i++)
            {
                removeServer(outOfConfigurationServers[i]);
            }
        }

        else if (response != nullptr)
        {
            // Request acknowledged
            if (response->getSucceded())
            {
                if(newServerNumber > 0)
                {
                    newServerNumber--;
                }
                else if(serverToDelete > 0)
                {
                    serverToDelete--;
                }
                updateConfiguration();
                deleteServerMsg = new cMessage("Delete server");
                scheduleAt(simTime() + 0.5, deleteServerMsg);
                timeToChange = new cMessage("add/delete new server.");
                free = true;
                cancelEvent(reqTimeoutExpired);
                scheduleAt(simTime(), timeToChange);
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

int ConfigurationManager::addNewServer()
{
    cModule *Switch = gate("gateConfigurationManager$i", 0)->getPreviousGate()->getOwnerModule();
    cGate *newServerPortIN, *newServerPortOUT;
    cGate *newSwitchPortIN, *newSwitchPortOUT;
    cModule *tempClient;
    numberOfServers++;

    Switch->setGateSize("gateSwitch", Switch->gateSize("gateSwitch$o") + 1);
    int index = gate("gateConfigurationManager$o",0)->getNextGate()->size() - 1;

    newSwitchPortIN = Switch->gate("gateSwitch$i", index);
    newSwitchPortOUT = Switch->gate("gateSwitch$o", index);

    bubble("Adding new server!");
    cModuleType *moduleType = cModuleType::get("Server");
    string i = to_string(numberOfServers-1);

    string temp = "server[" + i + "]";
    const char *name = temp.c_str();
    cModule *module = moduleType->create(name, getSystemModule());

    cDelayChannel *delayChannelIN = cDelayChannel::create("myChannel");
    cDelayChannel *delayChannelOUT = cDelayChannel::create("myChannel");
    delayChannelIN->setDelay(0.01);
    delayChannelOUT->setDelay(0.01);

    module->setGateSize("gateServer", module->gateSize("gateServer$o") + 1);

    newServerPortIN = module->gate("gateServer$i",0);
    newServerPortOUT = module->gate("gateServer$o",0);

    newSwitchPortOUT->connectTo(newServerPortIN, delayChannelIN);
    newServerPortOUT->connectTo(newSwitchPortIN, delayChannelOUT);

    // create internals, and schedule it
    module->buildInside();
    module->callInitialize();

    ((Server *)module)->configureServer(initialConfiguration);
    return ((Server*)module)->getAddress();
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
        if (toDelete == serverIndex)
        {
            serverToDelete = Switch->gate("gateSwitch$o", i)->getNextGate()->getOwnerModule();
            serverToDelete->gate("gateServer$o", 0)->disconnect();
            Switch->gate("gateSwitch$o", i)->disconnect();
            // Delete the Server
            // serverToDelete->callFinish();
            // shutDownDeletedServer = new cMessage("SHUTDOWN SERVER");
            // scheduleAt(simTime(), shutDownDeletedServer);
        }
    }
}

// It sends a log message, under the assumption that the client already knows a leader
void ConfigurationManager::scheduleNewMessage(char operation, int serverAddr)
{
    bubble("Sending change configuration info");
    commandCounter++;
    //DA INSERIRE QUI I VARI ADDRESS DEL LogMsg
    notifyLeaderOfChangeConfig = new LogMessage("change configuration notify");
    notifyLeaderOfChangeConfig->setClientAddress(networkAddress);
    notifyLeaderOfChangeConfig->setLeaderAddress(leaderAddress);
    notifyLeaderOfChangeConfig->setOperation(operation);
    notifyLeaderOfChangeConfig->setOperandValue(0);
    notifyLeaderOfChangeConfig->setOperandName('Q');
    notifyLeaderOfChangeConfig->setSerialNumber(commandCounter);
    if(operation == '+')
    {
        notifyLeaderOfChangeConfig->setServerToAdd(serverAddr);
        notifyLeaderOfChangeConfig->setOperandValue(serverAddr - numberOfClients - 1);
    }
    else
    {
        notifyLeaderOfChangeConfig->setServerToRemove(serverAddr);
        notifyLeaderOfChangeConfig->setOperandValue(serverAddr);
    }
    lastLogMessage = notifyLeaderOfChangeConfig->dup();

    if(gate("gateConfigurationManager$o", 0)->isConnected())
        send(notifyLeaderOfChangeConfig, "gateConfigurationManager$o", 0);
    free = false;

    reqTimeoutExpired = new cMessage("Start countdown for my request.");
    scheduleAt(simTime() + 1, reqTimeoutExpired);
}

void ConfigurationManager::updateConfiguration()
{
    if (lastLogMessage->getServerToAdd() >= 0)
    {
        // CASE A: new server added to the configuration
        currentConfiguration.push_back(lastLogMessage->getServerToAdd());
    }
    else
    {
        // CASE B: server removed from the configuration
        int toDel = lastLogMessage->getServerToRemove();
        currentConfiguration.erase(remove(currentConfiguration.begin(), currentConfiguration.end(), toDel));
        outOfConfigurationServers.push_back(toDel);
    }
}

void ConfigurationManager::finish()
{
}
