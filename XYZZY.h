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

struct ackListNode{
    int seqno;
    ackListNode* next;

    public:
    ackListNode():seqno(0),next(NULL){}
};

enum Xyzzy_header_types { T_normal, T_ack };

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

class RetryTimer : public TimerHandler {
	public:
	RetryTimer(XyzzyAgent* t) : TimerHandler(), t_(t) {}
	virtual void expire(Event*);
	protected:
	XyzzyAgent* t_;
};

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
        int seqno_;
        Packet *window[WINDOW_SIZE];
        double numTries[WINDOW_SIZE];
        double timeSent[WINDOW_SIZE];
        int bufLoc_;
        void updateCumAck(int);
        void ackListPrune();
        void recordPacket(Packet*, double);
        int cumAck_;
        ackListNode* ackList;
    protected:
        double CHECK_BUFFER_INT;
        double MAXDELAY;
        RetryTimer retry_;
};


#endif

/* vi: set tabstop=4, softtabstop=4, shiftwidth=4, expandtab */
