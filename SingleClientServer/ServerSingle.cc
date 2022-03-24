/*
 * ServerSingle.cc
 *
 *  Created on: 16 mar 2022
 *      Author: ste_dochio
 */
#include <stdio.h>
#include <string.h>
#include <omnetpp.h>
#include <CustomMessage_m.h>

using namespace omnetpp;
class ServerSingle: public cSimpleModule{
    //this is the class definition that is separated from implementation
    //sto estendendo la classe c simple module da cui eredito tutti i metodi, tra cui ad esempio il metodo get name
private:
    int temp;//this variable save the number that i sent with message

protected:
        customMessage *confirmSaved;
        virtual void initialize() override;
        virtual void handleMessage(cMessage *msg);
};


Define_Module(ServerSingle);

//here began the implementation of the class
//here i redefine initialize method
//invoked at simulation starting time
void ServerSingle::initialize(){
    WATCH(temp);
    //in this case the server waits for the client message; no need of any kind of initialization
}


//here i redefine handleMessage method
//invoked every time a message enters in the node
void ServerSingle::handleMessage(cMessage *msg){

    temp = msg->getNumberToSave();//i sent back to client his number-1
    temp--;
    confirmSaved = new customMessage("serverConfimation");
    confirmSaved->setNumberToSendBack(temp);
    EV << "message received and sent back\n";
    send(confirmSaved,"outS");
}




