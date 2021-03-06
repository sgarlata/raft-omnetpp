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
#include "LogMessage_m.h"

using namespace omnetpp;

class Client: public cSimpleModule{
    private:
        int numberToSend; // this is the number that the client send to the leader, we register this number into the log
        cMessage *sendToLog;//this is a message only for leader server, this message contains the number for log
    protected:
        virtual void initialize() override;
        virtual void handleMessage(cMessage *msg) override;
        virtual void finish() override;
        virtual LogMessage* generateMessageForLog(int number, char operations);//this method is useful to generate a message that a client have to send to the log in the leader server
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
         int execIndx = pong->getExecIndex();

        bubble("message from server received");
        int leaderServerId = pong->getLeaderId();
        delete pong;

        for (cModule::GateIterator i(this); !i.end(); i++) {
            cGate *gate = *i;
            int h = (gate)->getPathEndGate()->getOwnerModule()->getIndex();
            const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
            std::string ex = "server";

            // execIndx == h perch? ? solo il leader a pingare i/il client e quindi l'index del server salvato in h
            // deve essere uguale all'index del server che mi ha mandato il messaggio
            if (name == ex && execIndx == h) {
                //QUI ANZICHE' IL PONG DOVRANNO ESSERE INVIATI I MESSAGGI PER IL LOG
                //CHE SONO STATI CREATI GRAZIE ALLA generateMessageForLog
                EV << "Sending to server[" + std::to_string((gate)->getPathEndGate()->getOwnerModule()->getIndex()) + "] a ping message...\n";
                Ping *pong = new Ping("ping");
                pong->setClientIndex(this->getIndex());
                pong->setExecIndex(leaderServerId);
                send(pong,gate->getName(),gate->getIndex());
            }
        }
        bubble("here i sent a new ping");
    }
}

LogMessage* Client::generateMessageForLog(int number, char operations) {
    //DA RIVEDERE, MANCANO ALCUNE COSE
    int srcClient = this->getIndex();
    int num = number;
    char op = operations;

    LogMessage *logMessage = new LogMessage ("new entry for log");
    logMessage->setClientId(srcClient);
    logMessage->setNum(num);
    logMessage->setOperation(op);

    return logMessage;
}

void Client::finish() {}



