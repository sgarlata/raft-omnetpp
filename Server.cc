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
//message list
#include "Ping_m.h"
#include "LeaderElection_m.h"
#include "HeartBeats_m.h"
#include "VoteReply_m.h"
#include "VoteRequest_m.h"
#include "LogMessage_m.h"

using namespace omnetpp;

class Server: public cSimpleModule{
    //this is the class definition that is separated from implementation
    //sto estendendo la classe c simple module da cui eredito tutti i metodi, tra cui ad esempio il metodo get name
    private:
    cMessage *initPingMessage;// its just for test
    cMessage *electionTimeoutExpired;//this will be a self message
    cMessage *heartBeatsReminder; //if the leader receive this autoMessage it send a broadcast heartbeat

    enum stateEnum {FOLLOWER,CANDIDATE,LEADER,DEAD};
    stateEnum serverState;// Current state (Follower, Leader or Candidate)
    int messageElectionReceived;
    int serverNumber;
    int temp;

    int currentTerm;
    double randomTimeout;//when it expires an election starts
    //int currentTerm; // Latest term server has seen (initialized to 0 on first boot)
    bool alreadyVoted; // ID of the candidate that received vote in current term (or null if none)
    int leader; // ID of the leader
    int numberVoteReceived = 0;// number of vote received by every server
    //int commitIndex; // index of highest log entry known to be committed
    std::vector<int> nextIndex;
    std::vector<int> matchIndex;
    protected:

//FOR TIMER IMPLEMENTATION SEE TXC8.CC FROM CUGOLA EXAMPLE, E'LA TIMEOUT EVENT
//GUARDARE SEMPRE ESEMPIO TXC8.CC E SIMULATION PER CAPIRE COME MANDARE PIU' MESSAGGI
//CONTEMPORANEAMENTE.

//TXC15 E TXC16 SERVONO INVECE A FINI STATISTICI, GUARDARE QUESTI DUE ESEMPI PER CAPIRE
//COME COLLEZIONARE DATI DURANTE LA LIVE SIMULATION

    virtual void initialize() override;
    virtual void handleMessage(cMessage *msg) override;
    virtual void finish() override;
    //virtual void sendBroadcastToAllServer();// this method is useful to send broadcast message from one server to all other server
};

Define_Module(Server);

//here began the implementation of the class
//here i redefine initialize method
//invoked at simulation starting time
void Server::initialize(){
    //parameter initialization
    serverNumber = getParentModule()->par("numServer");
    double realProbability = getParentModule()->par("componentDeadProbability");
    currentTerm = 0; // or 1
    alreadyVoted = false;
    serverState = FOLLOWER;
    leader = -1;
    //commitIndex = 0;
    for (int i = 0; i < serverNumber; ++i) {
        nextIndex.push_back(1);
        matchIndex.push_back(0);
    }

    // We define a probability of death
        double deadProbability = uniform(0, 1);
        if (deadProbability < realProb) {
            double randomDelay = uniform(1, maxDeathStart);
            //failureMsg = new cMessage("failureMsg");
            //EV << "Here is client[" + std::to_string(this->getIndex()) + "]: I will be dead in " + std::to_string(randomDelay) + " seconds...\n";
            //scheduleAt(simTime() + randomDelay, failureMsg);
        }

    //this is a self message
    //here expires the first timeout; so the first server with timeout expired sends the first leader election message
    electionTimeoutExpired = new cMessage("ElectionTimeoutExpired");
    double randomTimeout = uniform(0.25, 0.7);
    scheduleAt(simTime() + randomTimeout, electionTimeoutExpired);

    //#######this cycle send a message from one server to all other servers linked to it###########################################
   /* int counter_printed = 1;
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
    }*/
    //#####################################################################################################
}
//here i redefine handleMessage method
//invoked every time a message enters in the node
void Server::handleMessage(cMessage *msg){
    Ping *ping = dynamic_cast<Ping*>(msg);
    VoteReply *voteReply = dynamic_cast<VoteReply*>(msg);
    VoteRequest *voteRequest = dynamic_cast<VoteRequest*>(msg);
    LeaderElection *leaderElection = dynamic_cast<LeaderElection*>(msg);
    HeartBeats *heartBeats = dynamic_cast<HeartBeats*>(msg);


    if(msg == electionTimeoutExpired){ //I only enter here if a new election has to be done
            bubble("timeout expired, new election start");

            currentTerm++;
            serverState = CANDIDATE;
            numberVoteReceived++; //it goes to 1, the server vote for himself
            this->alreadyVoted = true; // each server can vote just one time per election; if the server is in a candidate state it vote for himself

            //i set a new timeout range
            electionTimeoutExpired = new cMessage("NewElectionTimeoutExpired");
            double randomTimeout = uniform(0.5, 1);
            scheduleAt(simTime() + randomTimeout, electionTimeoutExpired);

            //now i send in broadcast to all other server the vote request
            for (cModule::GateIterator i(this); !i.end(); i++) {
                cGate *gate = *i;
                int h = (gate)->getPathEndGate()->getOwnerModule()->getIndex();
                const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
                std::string ex = "server";

                // to avoid message to client and self message
                if (h != this->getIndex() && name == ex) {
                    VoteRequest *voteRequest = new VoteRequest("voteRequest");
                    voteRequest->setCandidateIndex(this->getIndex());
                    send(voteRequest, gate->getName(), gate->getIndex());
                }
            }
        }

    else if(voteRequest != nullptr && alreadyVoted == false){//if arrives a vote request and i didn't already vote i can vote and send this vote to the candidate

        cancelEvent(electionTimeoutExpired);
        alreadyVoted = true;
        electionTimeoutExpired = new cMessage("NewElectionTimeoutExpired");
        double randomTimeout = uniform(0.5, 1);
        scheduleAt(simTime() + randomTimeout, electionTimeoutExpired);
        //now i send to the candidate server that sends to me the vote request a vote reply;
        //i send this message only to him
        bubble("vote reply");
        int candidateIndex = voteRequest->getCandidateIndex();

        //this cycle is useful to send the message only to the candidate server that asks for a vote
        for (cModule::GateIterator i(this); !i.end(); i++) {
                cGate *gate = *i;
                int h = (gate)->getPathEndGate()->getOwnerModule()->getIndex();
                const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
                std::string ex = "server";

                // to send message to candidate server
                if (h == candidateIndex && name == ex && h != this->getIndex()) {
                    VoteReply *voteReply = new VoteReply("voteReply");
                    voteReply->setCommonServerIndex(this->getIndex());// this is the id of the voting server
                    voteReply->setLeaderIndex(candidateIndex);
                    send(voteReply, gate->getName(), gate->getIndex());
                }
            }
    }

    else if(voteReply != nullptr){//here i received a vote so i increment the current term vote
        //bubble("vote reply received");
        numberVoteReceived = numberVoteReceived + 1;
        if(numberVoteReceived > (serverNumber/2)){//to became a leader a server must have the majority

            // if a server becomes leader I have to cancel the timer for a new election since it will
            //remain leader until the first failure, furthermore i have to reset all variables used in the election
            //so i reset here the leader variable, LE ALTRE VARIABILI NON SO DOVE RESETTARLE
            cancelEvent(electionTimeoutExpired);

            bubble("im the leader");
            serverState = LEADER;
            numberVoteReceived = 0;
            this->alreadyVoted = false;
            // i send in broadcast the heartBeats to all other server and a ping to all the client, in this way every client knows the leader and can send
            //information that must be saved in the log
            for (cModule::GateIterator i(this); !i.end(); i++) {
                cGate *gate = *i;
                int h = (gate)->getPathEndGate()->getOwnerModule()->getIndex();
                const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
                std::string server = "server";
                // to avoid message to client and self message
                if (h != this->getIndex() && name == server) {
                    HeartBeats *heartBeats = new HeartBeats("im the leader");
                    heartBeats->setLeaderIndex(this->getIndex());
                    send(heartBeats, gate->getName(), gate->getIndex());
                }
                std::string client = "client";
                // to send message only to all clients
                if (name == client) {
                    Ping *ping = new Ping("im the leader");
                    ping->setLeaderId(this->getIndex());
                    send(ping, gate->getName(), gate->getIndex());
                }
            }
            //the leader periodically send an heartBeats
             heartBeatsReminder = new cMessage("heartBeatsReminder");
             double randomTimeout = uniform(0.1, 0.3);
             scheduleAt(simTime() + randomTimeout, heartBeatsReminder);

         }
     }

    else if (heartBeats != nullptr){
        cancelEvent(electionTimeoutExpired);
        electionTimeoutExpired = new cMessage("NewElectionTimeoutExpired");
        double randomTimeout = uniform(2, 4);
        scheduleAt(simTime() + randomTimeout, electionTimeoutExpired);
    }

    else if (msg == heartBeatsReminder){
        for (cModule::GateIterator i(this); !i.end(); i++) {
            cGate *gate = *i;
            int h = (gate)->getPathEndGate()->getOwnerModule()->getIndex();
            const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
            std::string ex = "server";

            // to avoid message to client and self message
            if (h != this->getIndex() && name == ex) {
                HeartBeats *heartBeats = new HeartBeats("im the leader");
                heartBeats->setLeaderIndex(this->getIndex());
                send(heartBeats, gate->getName(), gate->getIndex());
            }
        }
        heartBeatsReminder = new cMessage("heartBeatsReminder");
        double randomTimeout = uniform(0.1, 0.3);
        scheduleAt(simTime() + randomTimeout, heartBeatsReminder);
    }









































    else if(msg == initPingMessage){
        //#############################################################################################
        //this cycle send a message to only one server linked to client
        for (cModule::GateIterator i(this); !i.end(); i++) {
            cGate *gate = *i;
            int h = (gate)->getPathEndGate()->getOwnerModule()->getIndex();
            const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
            std::string ex = "client";
            //this is the first ping message, i have to sent it to all clients; is the server leader that sent the first
            //ping to all this client
            if ((this->getIndex()== serverNumber-1) && name == ex) {
                bubble("SEND PING TO CLIENT");
                EV<<"HI IM THE FIRST SERVER PING";
                EV << "Sending to server[" + std::to_string((gate)->getPathEndGate()->getOwnerModule()->getIndex()) + "] a ping message...\n";
                Ping *ping = new Ping("ping");
                ping->setExecIndex(this->getIndex());//this is the index of server, in this case the leader
                ping->setClientIndex((gate)->getPathEndGate()->getOwnerModule()->getIndex());
                send(ping,gate->getName(),gate->getIndex());
            }
        }
       //#####################################################################################################
    }
/*
    else if (ping != nullptr){
                EV<<"SONO NEL PING";
                //rimando il ping al client
                bubble("message from client received");
                int clientId = ping->getClientIndex();
                // provaPerIlPing++;

                //int newIndexx = provaPerIlPing % 3;

                delete ping;

                Ping *ping = new Ping("pong");
                ping->setExecIndex(newIndexx);

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

    else if(leaderElection != nullptr && messageElectionReceived == 0){
            bubble("leader election start");
            delete leaderElection;
            for (cModule::GateIterator i(this); !i.end(); i++) {
                cGate *gate = *i;
                int h = (gate)->getPathEndGate()->getOwnerModule()->getIndex();
                const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
                std::string ex = "server";

                // to avoid self msg and msg to client
                if (h != this->getIndex() && name == ex) {
                    LeaderElection *leaderElection = new LeaderElection("election");
                    send(leaderElection, gate->getName(), gate->getIndex());
                }
            }
    }
*/

}

void Server::finish(){}
