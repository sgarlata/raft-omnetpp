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

using namespace omnetpp;

class Server: public cSimpleModule{
    //this is the class definition that is separated from implementation
    //sto estendendo la classe c simple module da cui eredito tutti i metodi, tra cui ad esempio il metodo get name
    private:
    enum stateEnum {FOLLOWER, LEADER, NULLPT, CANDIDATE};
    stateEnum serverState;// Current state (Follower, Leader or Candidate)
    int serverNumber; // Number of servers
    cMessage *leaderElection;
    int currentTerm; // Latest term server has seen (initialized to 0 on first boot)
    int votedFor; // ID of the candidate that received vote in current term (or null if none)
    int leader; // ID of the leader
    int commitIndex; // index of highest log entry known to be committed
    std::vector<int> nextIndex;
    std::vector<int> matchIndex;


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

    currentTerm = 0; // or 1
    votedFor = -1;
    state = FOLLOWER;
    leader = -1;
    commitIndex = 0;
    for (int i = 0; i < serverNumber; ++i) {
        nextIndex.push_back(1);
        matchIndex.push_back(0);
    }


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
            leaderElection = new cMessage("election");
            send(leaderElection, gate->getName(), gate->getIndex());
            counter_printed++;
        }
    }
    EV << "\nRichiesta di election mandata a tutti gli altri server!\n";


    //#####################################################################################################
}
//here i redefine handleMessage method
//invoked every time a message enters in the node
void Server::handleMessage(cMessage *msg){
    Ping *ping = dynamic_cast<Ping*>(msg);

    if (ping != nullptr){
                EV<<"SONO NEL PING";
                //rimando il ping al client
                bubble("message from client received");
                int clientId = ping->getClientIndex();

                Ping *pong = new Ping("pong");
                pong->setClientIndex(clientId);
                pong->setExecIndex(this->getIndex());
                for (cModule::GateIterator i(this); !i.end(); i++) {
                    cGate *gate = *i;
                    int h = (gate)->getPathEndGate()->getOwnerModule()->getIndex();
                    const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
                    std::string cl = "client";
                    // To send the msg only to the client chosen, not the exec with same index
                    if (h == ping->getClientIndex() && name == cl) {
                        EV << "\n##################### \n";
                        EV << "... answering to the client[" + std::to_string(pong->getClientIndex()) + "]";
                        EV << "\n##################### \n";
                        send(pong, gate->getName(), gate->getIndex());
                    }
                }
            }
/*
    //else if(msg == leaderElection){
            bubble("leader election start");
            for (cModule::GateIterator i(this); !i.end(); i++) {
                cGate *gate = *i;
                int h = (gate)->getPathEndGate()->getOwnerModule()->getId();
                const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
                std::string ex = "server";

                // to avoid self msg and msg to client
                if (h != this->getId() && name == ex) {
                    send(msg->dup(), gate->getName(), gate->getIndex());
                }
            }
               EV << "\nMessagio del server mandato a tutti i server!\n";
    //}*/


}

void Server::finish(){}
