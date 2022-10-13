//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
//

//this module represent the client that send the message that must be saved in the log
//the client communicate with the leader
simple Client
{
    parameters:
        @display("i=device/pc");
    gates:
        inout gateClient[];
}

//this module represent the machine that participate to the consensus algorithm
simple Server
{
    parameters:
        @display("i=device/server2");
 		bool addedByManager = default(false);
 		int catchUpPhaseRoundDuration = default(2);
 		int maxNumberRound = default(5);
 		double minElectionTimeout = default(2);
 		double maxElectionTimeout = default(4);
 		double applyChangePeriod = default(1);
 		double heartbeatsPeriod = default(0.3);
    gates:
        inout gateServer[];
}

//this module represent the the entity that manages configuration changes
simple ConfigurationManager
{
    parameters:
        @display("i=abstract/penguin_l");
    gates:
        inout gateConfigurationManager[];
}


simple Switch
{
    parameters:
        @display("i=device/switch");
    gates:
        inout gateSwitch[];
}

network Network
{
    parameters:
        // initial number of clients
        int numClient @prompt("Number of client") = default(2);
        // initial number of servers
        int numServer @prompt("Number of server") = default(3);
        
        // client crash parameters
        double clientsCrashProbability = default(0.7);
        double clientsMaxCrashDelay = default(6);
        double clientsMaxCrashDuration = default(2);
        
        // server crash parameters
        double serverCrashProbability = default(0.7); // 0.001
        double serverMaxCrashDelay = default(20);
        double serverMaxCrashDuration = default(3);
        
        double leaderCrashProbability = default(0.7);
        double leaderMaxCrashDuration = default(2);
        double dieAgainProbability = default(0.7);
        int activationlinkCrashClient = default(0);
        int activationlinkCrashServer = default(0);
        
        // Switch reliability
        double channelsReliability = default(0.95);
        
        @display("bgb=896,364");
    types:
        channel myChannel extends ned.DelayChannel{
                delay = 10ms;
        }
    submodules:
        client[numClient]: Client {
            parameters:
                @display("i=,cyan;");
        }
        
        server[numServer]: Server {
            parameters:
                @display("i=,bronze;");
        }
        
        configurationManager: ConfigurationManager {        		
        }
        
        switch: Switch {
                @display("");
        }
    connections:
        
        for i=0..numServer-1 {
                server[i].gateServer++ <--> myChannel <--> switch.gateSwitch++;
        }
        
        for i=0..numClient-1 {
                client[i].gateClient++ <--> myChannel <--> switch.gateSwitch++;
        }
        
        configurationManager.gateConfigurationManager++ <--> myChannel <--> switch.gateSwitch++;
        
}
