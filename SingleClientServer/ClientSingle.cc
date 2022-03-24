/*
 * ClientSingle.cc
 *
 *  Created on: 16 mar 2022
 *      Author: ste_dochio
 */
#include <stdio.h>
#include <string.h>
#include <omnetpp.h>
#include <CustomMessage_m.h>

using namespace omnetpp;
class ClientSingle: public cSimpleModule{

private:
        int numberToSend; // this is the number that the client send to the leader, we register this number into the log

    protected:
        customMessage *sendToLog;
        virtual void initialize() override;
        virtual void handleMessage(cMessage *msg);

};
Define_Module(ClientSingle);

void ClientSingle::initialize(){
    WATCH(numberToSend);
    //here i sent to the server the first number
    numberToSend = rand();
    // Generate and send initial message.
    EV << "Sending initial message\n";
    sendToLog = new customMessage("SendNumberToServer");
    sendToLog->setNumberToSave(numberToSend);
    //this is a cast used only to send the message with the handle message
    send(dynamic_cast<cMessage>(sendToLog), "outC");//è un tentativo di cast,NON FUNZIONA. CAPIRE COME MANDARE I CUSTOM MESSAGE SENZA LA HANDLE MESSSAGE
}

//the client send only a number to the leader server that insert this number into the log;
//than the client receive a message from the leader that confirms this operation.
void ClientSingle::handleMessage(cMessage *msg){
    int appoggio;
    appoggio = (dynamic_cast<customMessage>(msg))->getNumberToSendBack();

    if(numberToSend-1 == appoggio){
        //here i continue to send other messages
        EV << "Sending another message\n";
        //if i enter here server has saved in the log the random number and
        //i can send to it another one
        numberToSend = rand();
        sendToLog = new customMessage("SendNumberToServer");
        sendToLog->setNumberToSave(numberToSend);
        send(sendToLog, "outC");
        EV << "Another message already sent\n";
    }

}


