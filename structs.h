/*
 * structs.h
 *
 *  Created on: Aug 16, 2022
 *      Author: manfredi
 */
using namespace omnetpp;

struct log_entry {
        int clientId = 0; // ??? forse meglio l'index? ???
        int entryLogIndex;
        int entryTerm;
        char operandName;
        char operandValue;
        char operation;
};

struct state_machine_variable {
        char variable = 0;
        int val = 0;
};

enum stateEnum {
    FOLLOWER, CANDIDATE, LEADER, DEAD
};

