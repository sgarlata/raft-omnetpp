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
#include "LeaderElection_m.h"
#include <algorithm>
#include <list>
#include <random>
#include <sstream>

using namespace omnetpp;

class Server: public cSimpleModule{
    //this is the class definition that is separated from implementation
    //sto estendendo la classe c simple module da cui eredito tutti i metodi, tra cui ad esempio il metodo get name
    private:
    cMessage *initPingMessage;
    enum stateEnum {FOLLOWER, LEADER, NULLPT, UNDERELECTION};
    int serverStatus;// this variable contains each server status that could be follower, leader, under election or not reachable.
    int serverNumber;
    int temp;
    protected:

//FOR TIMER IMPLEMENTATION SEE TXC8.CC FROM CUGOLA EXAMPLE, E'LA TIMEOUT EVENT
//GUARDARE SEMPRE ESEMPIO TXC8.CC E SIMULATION PER CAPIRE COME MANDARE PIU' MESSAGGI
//CONTEMPORANEAMENTE.

//TXC15 E TXC16 SERVONO INVECE A FINI STATISTICI, GUARDARE QUESTI DUE ESEMPI PER CAPIRE
//COME COLLEZIONARE DATI DURANTE LA LIVE SIMULATION

    virtual void initialize() override;
    virtual void handleMessage(cMessage *msg) override;
    virtual void finish() override;
};


Define_Module(Server);

//here began the implementation of the class
//here i redefine initialize method
//invoked at simulation starting time
void Server::initialize(){
    serverNumber = getParentModule()->par("numServer");
    //#######this cycle send a message from one server to all other servers linked to it###########################################

    int counter_printed = 1;
    for (cModule::GateIterator i(this); !i.end(); i++) {
        cGate *gate = *i;
        int h = (gate)->getPathEndGate()->getOwnerModule()->getIndex();
        const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
        std::string ex = "server";

        // to avoid self msg and msg to client
        if (h != this->getIndex() && name == ex) {

            EV << std::to_string(counter_printed) + ". " + (gate)->getPathEndGate()->getOwnerModule()->getName() + "[" + std::to_string((gate)->getPathEndGate()->getOwnerModule()->getIndex()) + "]\n";
            LeaderElection *leaderElection = new LeaderElection("election");
            leaderElection->setLeaderId(100);
            send(leaderElection, gate->getName(), gate->getIndex());
            counter_printed++;
        }
    }
    //#####################################################################################################


    //here i send the first ping message
    initPingMessage = new cMessage("initPingMessage");
    double randomStartingDelay = uniform(0, 0.05);
    scheduleAt(simTime() + randomStartingDelay, initPingMessage);
}
//here i redefine handleMessage method
//invoked every time a message enters in the node
void Server::handleMessage(cMessage *msg){
    Ping *ping = dynamic_cast<Ping*>(msg);
    LeaderElection *leaderElection = dynamic_cast<LeaderElection*>(msg);

    if(msg == initPingMessage){
        //#############################################################################################

        //this cycle send a message to only one server linked to client

        for (cModule::GateIterator i(this); !i.end(); i++) {
            cGate *gate = *i;
            int h = (gate)->getPathEndGate()->getOwnerModule()->getIndex();
            const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
            std::string ex = "client";
            //this is the first ping message, i have to sent it to all clients; is the server leader that sent the first
            //ping to all this client
            if (name == ex) {
                EV << "Sending to server[" + std::to_string((gate)->getPathEndGate()->getOwnerModule()->getIndex()) + "] a ping message...\n";
                Ping *ping = new Ping("ping");
                ping->setExecIndex(this->getIndex());//this is the index of server, in this case the leader
                ping->setClientIndex((gate)->getPathEndGate()->getOwnerModule()->getIndex());
                send(ping,gate->getName(),gate->getIndex());

            }
        }
        bubble("SEND PING TO CLIENT");
        EV<<"HI IM THE FIRST SERVER PING";

    //#####################################################################################################
    }

    else if (ping != nullptr){
                EV<<"SONO NEL PING";
                //rimando il ping al client
                bubble("message from client received");
                int clientId = ping->getClientIndex();


                //ping->setExecIndex((gate)->getPathEndGate()->getOwnerModule()->getIndex());
                //send(ping,gate->getName(),gate->getIndex());


                Ping *ping = new Ping("pong");
                ping->setExecIndex(this->getIndex());

                ping->setClientIndex(clientId);

                for (cModule::GateIterator i(this); !i.end(); i++) {
                    cGate *gate = *i;
                    int h = (gate)->getPathEndGate()->getOwnerModule()->getIndex();
                    const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
                    std::string cl = "client";
                    // To send the msg only to the client chosen, not the exec with same index
                    if (h == ping->getClientIndex() && name == cl) {
                        EV << "\n##################### \n";
                        EV << "... answering to the client[" + std::to_string(ping->getClientIndex()) + "]";
                        EV << "\n##################### \n";
                        send(ping, gate->getName(), gate->getIndex());
                    }
                }
            }

    else if(leaderElection != nullptr){
            bubble("leader election start");
            for (cModule::GateIterator i(this); !i.end(); i++) {
                cGate *gate = *i;
                int h = (gate)->getPathEndGate()->getOwnerModule()->getIndex();
                const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
                std::string ex = "server";

                // to avoid self msg and msg to client
                if (h != this->getIndex() && name == ex) {
                    send(msg->dup(), gate->getName(), gate->getIndex());
                }
            }
    }


}

void Server::finish(){}
