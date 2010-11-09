#ifndef ns2_XYZZY_
#define cs2_XYZZY_

// PT_XYZZY must be added to common/packet.h packet_t enum and p_info class
// Add "Agent/Xyzzy set packetSize_ 1024" to tcl/lib/ns-default.tcl
// Add "Xyzzy" to foreach prot{} list in tcl/lib/ns-packet.tcl
// Add "XYZZY/XYZZY.o \" to the Makefile as an object to be built

#include "../common/agent.h"
#include "../common/packet.h"

#define WINDOW_SIZE

struct hdr_Xyzzy {
    int srcid_;
    int seqno_;
    
    /* per-field member functions */
    int& srcid() { return (srcid_); }
    int& seqno() { return (seqno_); }

    /* Packet header access functions */
    static int offset_;
    inline static int& offset() { return offset_; }
    inline static hdr_Xyzzy* access(const Packet* p) {return (hdr_Xyzzy*) p->access(offset_);}
};

class XyzzyAgent : public Agent {
    public:
        friend class CheckBufferTimer;
        XyzzyAgent();
        XyzzyAgent(packet_t);
        virtual void sendmsg(int nbytes, const char *flags = 0){
            sendmsg(nbytes, NULL, flags);
        }
        virtual void sendmsg(int nbytes, AppData* data, const char *flags = 0);
        virtual void recv(Packet* pkt, Handler*);
        virtual int command(int argc, const char*const* argv);
    private:
        int seqno_;
        Packet *packetsSent[WINDOW_SIZE];
        double timesSent[WINDOW_SIZE];
    protected:
        double CHECK_BUFFER_INT;
        double MAXDELAY;
};



#endif

/* vi: set tabstop=4, softtabstop=4, shiftwidth=4, expandtab */
