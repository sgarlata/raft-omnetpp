/*
 * client.cc
 *
 *  Created on: 13 mar 2022
 *      Author: ste_dochio
 */
#include <stdio.h>
#include <string.h>
#include <omnetpp.h>
#include <Ping_m.h>
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
        int h = (gate)->getPathEndGate()->getOwnerModule()->getId(); //it returns the id of the client or server
        const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName(); //it returns a string name as "client" or "server"

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

}

//the client send only a number to the leader server that insert this number into the log;
//than the client receive a message from the leader that confirms this operation.
void Client::handleMessage(cMessage *msg){
    Ping *pong = dynamic_cast<Ping*>(msg);

     if (pong != nullptr){

        bubble("message from server received");

        for (cModule::GateIterator i(this); !i.end(); i++) {
            cGate *gate = *i;
            int h = (gate)->getPathEndGate()->getOwnerModule()->getIndex();
            const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
            std::string ex = "server";

            if (name == ex && h == 2) {
                EV << "Sending to server[" + std::to_string((gate)->getPathEndGate()->getOwnerModule()->getIndex()) + "] a ping message...\n";
                Ping *pong = new Ping("ping");
                pong->setClientIndex(this->getIndex());
                pong->setExecIndex((gate)->getPathEndGate()->getOwnerModule()->getIndex());
                send(pong,gate->getName(),gate->getIndex());
            }
        }
        bubble("here i sent a new ping");
    }
}

void Client::finish() {}




















