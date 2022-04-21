/*
 * client.cc
 *
 *  Created on: 13 mar 2022
 *      Author: ste_dochio
 */
#include <stdio.h>
#include <string.h>
#include <omnetpp.h>
#include <algorithm>
#include <list>
#include <random>
#include <sstream>

using namespace omnetpp;

class Client: public cSimpleModule{
    private:
        int numberToSend; // this is the number that the client send to the leader, we register this number into the log
        cMessage *sendToLog;//this is a message only for leader server, this message contains the number for log
    protected:
        virtual void initialize() override;
        virtual void handleMessage(cMessage *msg) override;
        virtual void finish() override;
};


Define_Module(Client);

void Client::initialize(){

//##########################HERE I CHECK IF ALL SERVERS ARE LINKED TO CLIENTS#########################################################
    //this is only for debug
    EV << "Lista dei moduli connessi ai gate del client: \n";
    int counter_printed = 1;
    for (cModule::GateIterator i(this); !i.end(); i++) {
        cGate *gate = *i;
        int h = (gate)->getPathEndGate()->getOwnerModule()->getId();
        const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();

        if (h != this->getId()) {
            EV << std::to_string(counter_printed) + ". " + (gate)->getPathEndGate()->getOwnerModule()->getName() + "["
            + std::to_string((gate)->getPathEndGate()->getOwnerModule()->getIndex()) + "]\n";
            counter_printed++;
        }
    }
//###################################################################################


    WATCH(numberToSend);
    //here i sent to the server the first number
    numberToSend = uniform(1, 30);

    // Generate and send initial message.
    EV << "Sending initial message\n";


//####### this cycle send a message to all server linked to client ###########################################
    for (cModule::GateIterator i(this); !i.end(); i++) {
                        cGate *gate = *i;
                        const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
                        std::string ex = "server";

                        if (name == ex) {
                            EV << "Sending to server[" + std::to_string((gate)->getPathEndGate()->getOwnerModule()->getIndex()) + "] a ping message...\n";

                            sendToLog = new cMessage("numberForLog");
                            send(sendToLog, gate->getName(), gate->getIndex());
                        }
     }

//#####################################################################################################
}

//the client send only a number to the leader server that insert this number into the log;
//than the client receive a message from the leader that confirms this operation.
void Client::handleMessage(cMessage *msg){

}






















void Client::finish() {}
