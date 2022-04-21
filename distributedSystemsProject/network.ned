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
    	 @display("i=block/routing");
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


network Network
{
    parameters:
        //this is the number of client that i want to create
        int numClient @prompt("Number of client") = default(1);
	    //this is the number of server that i want to create
        int numServer @prompt("Number of server") = default(3);

	
	types:
	    channel myChannel extends ned.DelayChannel{
	        delay = 50ms;
	    }
	
    submodules:
        client[numClient]: Client {
   			parameters:
                @display("i=,cyan");
        }
        
        server[numServer]: Server {
            parameters:
                @display("i=,gold");
        }
    connections:
        
        for i=0..numClient-1, for j=0..numServer-1 {
            client[i].gateClient++ <--> myChannel <--> server[j].gateServer++;
        }
        
        for i=0..numServer-1, for j=i+1..numServer-1 {
            server[i].gateServer++ <--> myChannel <--> server[j].gateServer++;
        }
}  
        
        
        
        
        //here i have connected all servers to each other
        //int save=numServer;
        //for i=0..(numServer-2), for j=(i+1)..(numServer-1) {
        //    server[i].gS[j] <--> server[j].gS[i];
        //}
        //here i have connected all clients to all servers.It's not necessary to 
        //connect all clients to each other DA RICONTROLLARE
        //for x=0..(save-2), for y=(x+1)..(numClient-1) {
        //    client[x].gC[y] <--> server[x].gS[y];
        //}



