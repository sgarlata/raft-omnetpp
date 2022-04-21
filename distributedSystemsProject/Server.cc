/*
 * server.cc
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

class Server: public cSimpleModule{
    //this is the class definition that is separated from implementation
    //sto estendendo la classe c simple module da cui eredito tutti i metodi, tra cui ad esempio il metodo get name
    private:
    int serverNumber;
    cMessage *leaderElection;

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
        int h = (gate)->getPathEndGate()->getOwnerModule()->getId();
        const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
        std::string ex = "server";

        // to avoid self msg and msg to client
        if (h != this->getId() && name == ex) {

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

    /*
    if (serverNumber == 1){
        send(msg, "gateServer[]");
    }else{
        int counter_printed = 1;
                for (cModule::GateIterator i(this); !i.end(); i++) {
                    cGate *gate = *i;
                    int h = (gate)->getPathEndGate()->getOwnerModule()->getId();
                    const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
                    std::string ex = "executor";

                    // to avoid self msg and msg to client
                    if (h != this->getId() && name == ex) {
                        msg->setDestination((gate)->getPathEndGate()->getOwnerModule()->getIndex());
                        send(msg, gate->getName(), gate->getIndex());
                        counter_printed++;
                    }
                }
                EV << "\nMessagio del client mandato a tutti i server!\n";

    }

*/
}

void Server::finish(){}
