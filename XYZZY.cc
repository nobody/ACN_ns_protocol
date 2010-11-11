#include "XYZZY/XYZZY.h"

int hdr_Xyzzy::offset_;

static class XyzzyHeaderClass : public PacketHeaderClass {
    public:
        XyzzyHeaderClass() : PacketHeaderClass("PacketHeader/Xyzzy", sizeof(hdr_Xyzzy))\
        {
            bind_offset(&hdr_Xyzzy::offset_);
        }

} class_xyzzyhdr;

static class XyzzyAgentClass : public TclClass {
    public:
        XyzzyAgentClass() : TclClass("Agent/Xyzzy") {}
        TclObject* create(int, const char*const*)
        {
            return (new XyzzyAgent());
        }
} class_xyzzyagent;


XyzzyAgent::XyzzyAgent() : Agent(PT_XYZZY)
{
    bind("packetSize_", &size_);

}

void XyzzyAgent::sendmsg(int nbytes, AppData* data, const char* flags) {
    Packet* p;

    if (size_ == 0)
        printf("Xyzzy size_ is 0!\n");

    if (data && nbytes > size_) {
        printf("Xyzzy data does not fit in a single packet. Abotr\n");
        return;
    }

    p = allocpkt();

    hdr_cmn::access(p)->ptype() = PT_XYZZY;
    hdr_cmn::access(p)->size() = nbytes;

    hdr_Xyzzy::access(p)->seqno() = ++seqno_;
    hdr_Xyzzy::access(p)->type() = T_normal;

    p->setdata(data);

    target_->recv(p);

}
void XyzzyAgent::recv(Packet* pkt, Handler*) {

    hdr_Xyzzy* oldHdr = hdr_Xyzzy::access(pkt);

    if (oldHdr->type() == T_normal){
        // Send back an ack
        Packet* ap = allocpkt();

        hdr_cmn::access(ap)->ptype() = PT_XYZZY;
        hdr_cmn::access(ap)->size() = sizeof(hdr_ip) + sizeof(hdr_Xyzzy);

        hdr_Xyzzy* newHdr = hdr_Xyzzy::access(ap);
        newHdr->type() = T_ack;
        newHdr->seqno() = oldHdr->seqno();

        // set up ip header
        hdr_ip* newip = hdr_ip::access(ap);
        hdr_ip* oldip = hdr_ip::access(pkt);

        newip->dst() = oldip->src();
        newip->prio() = oldip->prio();
        newip->flowid() = oldip->flowid();

        target_->recv(ap);

        if (app_) {
            app_->process_data(hdr_cmn::access(pkt)->size(), pkt->userdata());
        }

        if (pkt->userdata())
            printf("Received payload of size %d\n", pkt->userdata()->size());

    }

    Packet::free(pkt);

}
int XyzzyAgent::command(int argc, const char*const* argv) {

    if (argc == 4 && strcmp(argv[1], "send") == 0) {
        PacketData* d = new PacketData(strlen(argv[3]) + 1);
        strcpy((char*)d->data(), argv[3]);
        sendmsg(atoi(argv[2]), d);
        return TCL_OK;
    } else if (argc == 5 && strcmp(argv[1], "sendmsg") == 0) {
        PacketData* d = new PacketData(strlen(argv[3]) + 1);
        strcpy((char*)d->data(), argv[3]);
        sendmsg(atoi(argv[2]), d, argv[4]);
        return TCL_OK;
    }
        
    return Agent::command(argc, argv);
}



/* vi: set tabstop=4, softtabstop=4, shiftwidth=4, expandtab */
