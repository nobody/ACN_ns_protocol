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

}
void XyzzyAgent::recv(Packet* pkt, Handler*) {

}
int XyzzyAgent::command(int argc, const char*const* argv) {

    return 1;
}



/* vi: set tabstop=4, softtabstop=4, shiftwidth=4, expandtab */
