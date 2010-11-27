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

#define C_NORMAL    "\033[0m"
#define C_BALCK     "\033[0;30m"
#define C_RED       "\033[0;31m"
#define C_GREEN     "\033[0;32m"
#define C_YELLOW    "\033[0;33m"
#define C_BLUE      "\033[0;34m"
#define C_PURPLE    "\033[0;35m"
#define C_CYAN      "\033[0;36m"
#define C_WHITE     "\033[0;37m"

#define WINDOW_SIZE 40
#define RETRY_TIME 0.25
#define MAX_HB_MISS 2
#define HB_INTERVAL 0.5

#define B_DEAD 0
#define B_ACTIVE 1
#define B_LEADING 2

#define B_SENT_ACK 0
#define B_SENT_MSG 1
#define B_RCVD_ACK 2
#define B_RCVD_MSG 3


#define B_FAILED_BEATS 5

class HeartbeatTimer;
class TimeoutTimer;
class BuddyHeartbeatTimer;
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
    bool reachable;
    int rcount;
    
    HeartbeatTimer* hb_;
    TimeoutTimer* hbTimeout_;
    DestNode* next;

    DestNode(): iNsAddr(0),iNsPort(0),reachable(true),rcount(0),
                hb_(NULL),hbTimeout_(NULL),next(NULL) {}
};
struct buddyNode{
    int iNsAddr;
    int status;
    int missedHBS;

    DestNode* dests;
    BuddyHeartbeatTimer * hb_;

    buddyNode* next;

    buddyNode() : iNsAddr(0), next(NULL), status(B_ACTIVE), hb_(NULL){}
    DestNode* getDest();
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

enum Xyzzy_header_types { T_init, T_initack, T_normal, T_ack, T_heartbeat,
    //buddy states
    T_buddy, T_beat T_state, T_rne, T_elect};

enum Xyzzy_states { STATE_NO_CONN, STATE_ASSOCIATING, STATE_ASSOCIATED };

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
    int heartbeat_;
    int numTries_;
    double timeSent_;
    
    /* per-field member functions */
    int& srcid() { return (srcid_); }
    int& seqno() { return (seqno_); }
    int& type() { return (type_); }
    int& cumAck() { return (cumAck_); }
    char& sndRcv() {return (sndRcv_); }
    int& heartbeat() { return (heartbeat_); }
    int& numTries() {return (numTries_);}
    double& timeSent(){return (timeSent_);}

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

class BuddyHeartbeatTimer : public TimerHandler {
    public:
        BuddyHeartbeatTimer(XyzzyAgent* t, buddyNode* bn) : TimerHandler(), t_(t), bn_(bn) {}
        virtual void expire(Event*);
    protected:
        XyzzyAgent* t_;
        buddyNode* bn_;
};


class HeartbeatTimer : public TimerHandler {
    public:
        HeartbeatTimer(XyzzyAgent* t, DestNode* dn) : TimerHandler(), t_(t), dn_(dn) {}
        virtual void expire(Event*);
    protected:
        XyzzyAgent* t_;
        DestNode* dn_;
};

class AppPassTimer : public TimerHandler {
    public :
        AppPassTimer(XyzzyAgent* t): TimerHandler(), t_(t){}
        virtual void expire(Event*);
    protected:
        XyzzyAgent* t_;

};

// Generic Timeout Timer. 
// Given a function pointer with signature void fn(void*) and a void*,
// it will call the function with the given argument when it expires
class TimeoutTimer : public TimerHandler {
    public:
        TimeoutTimer(XyzzyAgent* t, void(XyzzyAgent::*fn)(void*), void* arg, bool del = false) : TimerHandler(), t_(t), fn_(fn), arg_(arg), del_(del) {}
        virtual void expire(Event*);
    protected:
        XyzzyAgent* t_;
        void(XyzzyAgent::*fn_)(void *);
        void* arg_;
        bool del_;
};

//this is our actuall agent class our actuall protocol
//for more specifics on how methods work and what they are
//used for look at the .cc file
class XyzzyAgent : public Agent {
    public:
        friend class HeartbeatTimer;
        friend class TimeoutTimer;
        friend class BuddyTimer;
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
        void buddyMissedBeats(buddyNode*);
        void sndPktToApp();

    private:
        // Agent state
        int state_;
        int init_;
        
        //our present sequence number
        int seqno_;
        Classifier* coreTarget;

        //thes packets govern our retransmission of packets
        //sndWindow is where packets are stored until they are acked
        //numTries is where we record how many times we have tried 
        //to resend the packet
        //timeSent is a time stamp so we know when to try to resend
        //a packet
        Packet *sndWindow[WINDOW_SIZE];
        double numTries[WINDOW_SIZE];
        double timeSent[WINDOW_SIZE];

        //this keeps track of the head of the sndWindow buffer since
        //it is a circular buffer
        int sndBufLoc_;

        //circular buffer for receiver
        Packet* rcvWindow[WINDOW_SIZE];
        int rcvBufLoc_;
        int rcvNextExpected;
        int rcvHighestReceived;

        //the following 2 functions maintain the ackList
        void updateCumAck(int);
        void ackListPrune();

        // this records packet information in the sndWindow, numTries,
        // and timeSent arrays
        bool recordPacket(Packet*, double);
        void retryRecordPacket(void*);

        // set up source and dest for packet
        void setupPacket(Packet* pkt = NULL, DestNode* dn = NULL);

        // Adds an interface to the list
        void AddInterface(int, int, NsObject*, NsObject*);

        // Adds a destination to the list
        void AddDestination(int, int);

        // Find a destination in the list by address
        DestNode* findDest(int);

        //the current cumulative ack value
        int cumAck_;

        //the head of linked list of received packets
        ackListNode* ackList;

        //buddy stuff
        void forwardToBuddies(Packet*, char);
        void sendToBuddies(Packet*, int);

        bool buddySend(Packet*, DestNode*);

        bool isActiveBuddy;
        int activeBuddyID_;
        buddyNode* buddies;
        int numOfBuddies_;
        buddyNode* currSetupBuddy;

        
        // head of the interface list
        IfaceNode* ifaceList;
        int numIfaces_;

        // head of the destination list
        DestNode* destList;

        // the primary destination
        DestNode* primaryDest;

        // set primary destination to next available
        void nextDest();

        // Send a heartbeat to the active node and set up a timeout timer
        void heartbeat(DestNode*);
        void heartbeatTimeout(void*);
    protected:
        double CHECK_BUFFER_INT;
        double MAXDELAY;
        RetryTimer retry_;
        //HeartbeatTimer hb_;
};


#endif

/* vi: set tabstop=4 softtabstop=4 shiftwidth=4 expandtab : */
