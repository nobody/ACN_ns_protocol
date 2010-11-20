#ifndef ns2_XYZZY_
#define ns2_XYZZY_

// PT_XYZZY must be added to common/packet.h packet_t enum and p_info class
// Add "Agent/Xyzzy set packetSize_ 1024" to tcl/lib/ns-default.tcl
// Add "Xyzzy" to foreach prot{} list in tcl/lib/ns-packet.tcl
// Add "XYZZY/XYZZY.o \" to the Makefile as an object to be built

#include "../common/agent.h"
#include "../common/packet.h"
#include "../common/ip.h"
#include "../common/scheduler.h"
#include <fcntl.h>
#include <unistd.h>

#define WINDOW_SIZE 40
#define RETRY_TIME 0.1

#define B_DEAD 0
#define B_ACTIVE 1
#define B_LEADING 2

#define B_SENT 0
#define B_RCVD 1

//this struct is used to keep a linked list of the 
//messages for which we have received acks. we periodically
//cull all sequential messages from the front of the list
//this lets us keep track of our "holes" like sctp
struct ackListNode{
    int seqno;
    ackListNode* next;

    public:
    ackListNode():seqno(0),next(NULL){}
};

struct buddyNode{
    int id;
    int status;
    int missedHBS;
};

//these are the types of headers our protocol uses

enum Xyzzy_header_types { T_normal, T_ack, T_buddy, T_beat};

//this is the header struct
//it contains functions and varibles 
//not only for our headder but for the stupid way
//ns2 does packets
struct hdr_Xyzzy {
    int srcid_;
    int seqno_;
    int type_;
    int cumAck_;
    char sndRcv_;
    
    /* per-field member functions */
    int& srcid() { return (srcid_); }
    int& seqno() { return (seqno_); }
    int& type() { return (type_); }
    int& cumAck() { return (cumAck_); }
    char& sndRcv() {return (sndRcv_); }

    /* Packet header access functions */
    static int offset_;
    inline static int& offset() { return offset_; }
    inline static hdr_Xyzzy* access(const Packet* p) {return (hdr_Xyzzy*) p->access(offset_);}
};


class XyzzyAgent;

//timer for auto firing the our attempts to retransmit packets
//every time the timer goes off, or retransmit function executes
class RetryTimer : public TimerHandler {
    public:
    RetryTimer(XyzzyAgent* t) : TimerHandler(), t_(t) {}
    virtual void expire(Event*);
    protected:
    XyzzyAgent* t_;
};

//timer to fire heartbeats to buddies
class BuddyTimer : public TimerHandler{
    public:
        BuddyTimer(XyzzyAgent* t) : TimerHandler(), t_(t){}
        virtual void expire(Event*);
    protected:
        XyzzyAgent* t_;
};

//this is our actuall agent class our actuall protocol
//for more specifics on how methods work and what they are
//used for look at the .cc file
class XyzzyAgent : public Agent {
    public:
        friend class CheckBufferTimer;
        XyzzyAgent();
        XyzzyAgent(packet_t);
        virtual void sendmsg(int nbytes, AppData* data, const char *flags = 0);
        virtual void sendmsg(int nbytes, const char *flags = 0){
            sendmsg(nbytes, NULL, flags);
        }
        virtual void recv(Packet* pkt, Handler*);
        virtual int command(int argc, const char*const* argv);
        void retryPackets();
        void sendBuddyHeartBeats();
    private:
        //our present sequence number
        int seqno_;

        //thes packets govern our retransmission of packets
        //window is where packets are stored until they are acked
        //numTries is where we record how many times we have tried 
        //to resend the packet
        //timeSent is a time stamp so we know when to try to resend
        //a packet
        Packet *window[WINDOW_SIZE];
        double numTries[WINDOW_SIZE];
        double timeSent[WINDOW_SIZE];

        //this keeps track of the head of the window buffer since
        //it is a circular buffer
        int bufLoc_;

        void updateCumAck(int);
        void ackListPrune();
        void recordPacket(Packet*, double);

        //the current cumulative ack value
        int cumAck_;

        //the head of linked list of received packets
        ackListNode* ackList;

        //buddy stuff
        void forwardToBuddies(Packet*, char);

        bool isActiveBuddy;
        int activeBuddyID_;
        buddyNode* buddies;
        int numOfBuddies_;

    protected:
        double CHECK_BUFFER_INT;
        double MAXDELAY;
        RetryTimer retry_;
};


#endif

/* vi: set tabstop=4 softtabstop=4 shiftwidth=4 expandtab : */
