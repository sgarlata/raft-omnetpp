/*
 * structs.h
 *
 *  Created on: Aug 16, 2022
 *      Author: manfredi
 */
using namespace omnetpp;
using std::vector;

struct log_entry {
    int clientAddress;
    int entryLogIndex;
    int entryTerm;
    int serialNumber;
    char operandName;
    int operandValue;
    char operation;
    int addressServerToRemove = -1;    // n == remove server n
    int addressServerToAdd = -1;       // n == add server n
};

struct last_req {
    int clientAddress;
    int lastArrivedSerial = 0;     // serial number of the last arrived from this client. It may either be in the log or not (change membership messages are stored later on)
    int lastLoggedIndex = 0;       // index of the last entry added to the log
    int lastAppliedSerial = 0;
};

struct client_requests_table {
    int size;
    vector<vector<last_req>> entries;
};

