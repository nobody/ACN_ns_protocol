#ifndef ns2_XYZZY_
#define ns2_XYZZY_

// PT_XYZZY must be added to common/packet.h packet_t enum and p_info class
// Add "Agent/Xyzzy set packetSize_ 1024" to tcl/lib/ns-default.tcl
// Add "Xyzzy" to foreach prot{} list in tcl/lib/ns-packet.tcl
// Add "XYZZY/XYZZY.o \" to the Makefile as an object to be built

#include "../common/agent.h"
#include "../common/node.h"
#include "../common/packet.h"
#include "../common/ip.h"
#include "../common/scheduler.h"
#include <fcntl.h>
#include <unistd.h>

#define WINDOW_SIZE 40
#define RETRY_TIME 1

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

// this struct is used to maintain information about destination ips.
struct DestNode {
    int iNsAddr;
    int iNsPort;

    DestNode* next;

    DestNode():iNsAddr(0),iNsPort(0),next(NULL){}
};

// this is used to maintain the list of interfaces we have available
struct IfaceNode {
    int iNsAddr;
    int iNsPort;
    NsObject* opTarget;
    NsObject* opLink;

    IfaceNode* next;

    IfaceNode():iNsAddr(0),iNsPort(0),opTarget(NULL),opLink(NULL),next(NULL){}
};

//these are the types of headers our protocol uses

enum Xyzzy_header_types { T_normal, T_ack };

//this is the header struct
//it contains functions and varibles 
//not only for our headder but for the stupid way
//ns2 does packets
struct hdr_Xyzzy {
    int srcid_;
    int seqno_;
    int type_;
    int cumAck_;
    
    /* per-field member functions */
    int& srcid() { return (srcid_); }
    int& seqno() { return (seqno_); }
    int& type() { return (type_); }
    int& cumAck() { return (cumAck_); }

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

        //the following 2 functions maintain the ackList
        void updateCumAck(int);
        void ackListPrune();

        // this records packet information in the window, numTries,
        // and timeSent arrays
        void recordPacket(Packet*, double);

        // Adds an interface to the list
        void AddInterface(int, int, NsObject*, NsObject*);

        // Adds a destination to the list
        void AddDestination(int, int);

        //the current cumulative ack value
        int cumAck_;

        //the head of linked list of received packets
        ackListNode* ackList;
        
        // head of the interface list
        IfaceNode* ifaceList;

        // head of the destination list
        DestNode* destList;

        // the primary destination
        DestNode* primaryDest;
    protected:
        double CHECK_BUFFER_INT;
        double MAXDELAY;
        RetryTimer retry_;
};


#endif

/* vi: set tabstop=4 softtabstop=4 shiftwidth=4 expandtab : */
