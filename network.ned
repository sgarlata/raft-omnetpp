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
        @display("i=device/pc_s");
    gates:
        inout gateClient[];
}

//this module represent the machine that participate to the consensus algorithm
simple Server
{
    parameters:
        @display("i=block/process");
    gates:
        inout gateServer[];
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
        //this is the number of client that i want to create
        int numClient @prompt("Number of client") = default(1);
	    //this is the number of server that i want to create
        int numServer @prompt("Number of server") = default(5);
        double clientsDeadProbability = default(0.7);
        double serverDeadProbability = default(0.7); // 0.001
        double clientsMaxDeathStart = default(6);
        double clientsMaxDeathDuration = default(20);
        double serverMaxDeathStart = default(20);
        double serverMaxDeathDuration = default(20);
        double leaderDeadProbability = default(0.7);
        double leaderMaxDeathDuration = default(10);
        double dieAgainProbability = default(0.7);

        @display("bgb=896,364");
    types:
	    channel myChannel extends ned.DelayChannel{
	        delay = 10ms;
	    }
    submodules:
        client[numClient]: Client {
            parameters:
                @display("i=,cyan;p=320,128");
        }

        server[numServer]: Server {
            parameters:
                @display("i=,bronze;");
        }
        switch: Switch {
            @display("p=380,200");
        }
    connections:
		
		for i=0..numServer-1 {
            server[i].gateServer++ <--> myChannel <--> switch.gateSwitch++;
        }
        
        for i=0..numClient-1 {
            client[i].gateClient++ <--> myChannel <--> switch.gateSwitch++;
        }
        
        
}
