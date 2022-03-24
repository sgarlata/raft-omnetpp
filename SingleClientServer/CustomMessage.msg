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

message customMessage
{
    int numberToSave;
    int log[];//the size of this vector can be set dynamically;
    //this would be a vector to send a portion of the log in a message
    int numberToSendBack;//only for testing, i put here the number and then i send back to the client
    //the same number; this is a confirm of saving correctly the message in the log
    };