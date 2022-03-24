import ned.DelayChannel;


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
simple ClientSingle
{
    parameters:
        //this is the time between sending a message to the leader for saving the message in the log
        //int messageInterval; // it is in milliseconds
        //i use this boolean to stop the sending message
        //bool stopC = false;
        
    gates:
        input inC;
        output outC;
}

simple ServerSingle
{
    parameters:
        //after this time a follower became candidate
        //int candidateTimer = uniform(150ms,300ms);
		//this is the log vector dimension, it must be >0
        //int dimLog;
        //i use this boolean to stop the sending message
        //bool stopS = false;
        
    gates:
        input inS;
        output outS;
}

network Network
{
    @display("bgb=332,195");
    submodules:
        client: ClientSingle {
            @display("p=45,83");
        }
        server: ServerSingle {
            @display("p=250,83");
        }
    connections:
        server.outS --> {  delay = 100ms; } --> client.inC;
        client.outC --> {  delay = 100ms; } --> server.inS;
}

