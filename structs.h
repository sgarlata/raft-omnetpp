/*
 * structs.h
 *
 *  Created on: Aug 16, 2022
 *      Author: manfredi
 */
using namespace omnetpp;

struct log_entry {
        int clientAddress;
        int entryLogIndex;
        int entryTerm;
        int serialNumber;
        char operandName;
        int operandValue;
        char operation;
};

//struct state_machine_variable {
//        char variable = 0;
//        int val = 0;
//};

struct lastClientRequestEntry
{
        int clientAddress;
        int serialNumber;
};

struct chainHead
{
    int key;
    std::vector<lastClientRequestEntry> chain;
};

struct clientRequestTable {
    // Contains an array of pointers to hashBucket
    std::vector<chainHead> buckets;
    int size;
};

enum operation {
    SET = 0,
    ADD = 1,
    MUL = 2
};


