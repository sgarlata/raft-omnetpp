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

