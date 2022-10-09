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
};

struct last_req {
    int clientAddress;
    int lastArrivedIndex = 0;
    int lastAppliedSerial = 0;
};

struct client_requests_table {
    int size;
    vector<vector<last_req>> entries;
};

