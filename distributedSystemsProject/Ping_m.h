//
// Generated file, do not edit! Created by nedtool 5.7 from Ping.msg.
//

#ifndef __PING_M_H
#define __PING_M_H

#if defined(__clang__)
#  pragma clang diagnostic ignored "-Wreserved-id-macro"
#endif
#include <omnetpp.h>

// nedtool version check
#define MSGC_VERSION 0x0507
#if (MSGC_VERSION!=OMNETPP_VERSION)
#    error Version mismatch! Probably this file was generated by an earlier version of nedtool: 'make clean' should help.
#endif



/**
 * Class generated from <tt>Ping.msg:15</tt> by nedtool.
 * <pre>
 * //
 * // This program is free software: you can redistribute it and/or modify
 * // it under the terms of the GNU Lesser General Public License as published by
 * // the Free Software Foundation, either version 3 of the License, or
 * // (at your option) any later version.
 * // 
 * // This program is distributed in the hope that it will be useful,
 * // but WITHOUT ANY WARRANTY; without even the implied warranty of
 * // MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * // GNU Lesser General Public License for more details.
 * // 
 * // You should have received a copy of the GNU Lesser General Public License
 * // along with this program.  If not, see http://www.gnu.org/licenses/.
 * //
 * packet Ping
 * {
 *     int clientIndex;
 *     int execIndex;
 * }
 * </pre>
 */
class Ping : public ::omnetpp::cPacket
{
  protected:
    int clientIndex;
    int execIndex;

  private:
    void copy(const Ping& other);

  protected:
    // protected and unimplemented operator==(), to prevent accidental usage
    bool operator==(const Ping&);

  public:
    Ping(const char *name=nullptr, short kind=0);
    Ping(const Ping& other);
    virtual ~Ping();
    Ping& operator=(const Ping& other);
    virtual Ping *dup() const override {return new Ping(*this);}
    virtual void parsimPack(omnetpp::cCommBuffer *b) const override;
    virtual void parsimUnpack(omnetpp::cCommBuffer *b) override;

    // field getter/setter methods
    virtual int getClientIndex() const;
    virtual void setClientIndex(int clientIndex);
    virtual int getExecIndex() const;
    virtual void setExecIndex(int execIndex);
};

inline void doParsimPacking(omnetpp::cCommBuffer *b, const Ping& obj) {obj.parsimPack(b);}
inline void doParsimUnpacking(omnetpp::cCommBuffer *b, Ping& obj) {obj.parsimUnpack(b);}


#endif // ifndef __PING_M_H

