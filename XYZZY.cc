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

void RetryTimer::expire(Event*){
	t_->retryPackets();
}


XyzzyAgent::XyzzyAgent() : Agent(PT_XYZZY), retry_(this)
{
    bind("packetSize_", &size_);

    // initialize buffer
    for (int i = 0; i < WINDOW_SIZE; ++i)
        window[i] = NULL;

    ackList = NULL;

    retry_.sched(RETRY_TIME+0.5);
}

void XyzzyAgent::retryPackets() {
    double timeNow = Scheduler::instance().clock();
    printf("[%d] Starting retry loop at %f\n", here_.addr_, timeNow);
    for (int i = 0; i < WINDOW_SIZE; ++i) {
        if (window[i] != NULL && timeNow - timeSent[i] > RETRY_TIME) {
            printf("[%d] ** RETRYING PACKET %d **\n", here_.addr_, hdr_Xyzzy::access(window[i])->seqno());

            // send again.
            target_->recv(window[i]);

            // TODO: let buddies know we retried
            numTries[i]++;
            timeSent[i] = Scheduler::instance().clock();
        }
    }

    if (retry_.status() == TIMER_IDLE)
        retry_.sched(RETRY_TIME);
    else if (retry_.status() == TIMER_PENDING){
        retry_.cancel();
    }
    retry_.resched(RETRY_TIME);
    printf("[%d] Ending retry loop at %f\n", here_.addr_, timeNow);
}

void XyzzyAgent::recordPacket(Packet* pkt, double time) {
    if (window[bufLoc_] != NULL){
        // TODO: delay somehow until there's room for another packet.
        printf("Send buffer full, but I don't know how to wait yet!, packet lost. (seqno: %d) in the way of (seqno: %d)\n", hdr_Xyzzy::access(window[bufLoc_])->seqno(), hdr_Xyzzy::access(pkt)->seqno());
    } else {
        // TODO: send to buddies

        window[bufLoc_] = pkt->copy();
        numTries[bufLoc_] = 1;
        timeSent[bufLoc_] = time;

        bufLoc_++;
        bufLoc_ %= WINDOW_SIZE;
        printf("  New buffer location: %d\n", bufLoc_);
    }
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

    recordPacket(p, Scheduler::instance().clock());

}
void XyzzyAgent::updateCumAck(int seqno){
    ackListNode* usefulOne;
    if(!ackList){
        ackList = new ackListNode;
        usefulOne = ackList;
    }else{
        ackListNode* current = ackList;
        do{
            if(current->seqno < seqno){
                usefulOne = current;
            }else if (current->seqno > seqno){
                break; 
            }
        }while((current = current->next));
        usefulOne->next = new ackListNode;
        usefulOne->next->next = current;

        usefulOne = usefulOne->next;
    }

    usefulOne->seqno = seqno;

}
void XyzzyAgent::ackListPrune(){
    if(!ackList || !ackList->next)
        return;

    ackListNode* usefulOne;
    ackListNode* current = ackList;
    int last_seqno = ackList->seqno;
    int count = 0;

    while((current = current->next)){
        if(current->seqno == last_seqno + 1){
            usefulOne = current;
            last_seqno = current->seqno;
            count++;
        }else {
            break; 
        }
    }

    current = ackList;
    ackListNode* n = current;
    for(int i = 0; i < count; ++i){
        if (current->next){
            n = current->next;
            delete current;
            current = n;
        }
    }
    ackList = n;
}
void XyzzyAgent::recv(Packet* pkt, Handler*) {

    hdr_Xyzzy* oldHdr = hdr_Xyzzy::access(pkt);

    if (oldHdr->type() == T_normal){

        // Send back an ack
        Packet* ap = allocpkt();

        hdr_cmn::access(ap)->ptype() = PT_XYZZY;
        hdr_cmn::access(ap)->size() = 0;

        //TODO: wrap around oh noes!
        updateCumAck(oldHdr->seqno());
        ackListPrune();

        hdr_Xyzzy* newHdr = hdr_Xyzzy::access(ap);
        newHdr->type() = T_ack;
        newHdr->seqno() = oldHdr->seqno();
        newHdr->cumAck() = ackList->seqno;
        printf("[%d] Generating ack for seqno %d (cumAck: %d)\n", here_.addr_, newHdr->seqno(), newHdr->cumAck());

        // set up ip header
        hdr_ip* newip = hdr_ip::access(ap);
        hdr_ip* oldip = hdr_ip::access(pkt);

        newip->dst() = oldip->src();
        newip->prio() = oldip->prio();
        newip->flowid() = oldip->flowid();

        target_->recv(ap);

        // log packet contents to file
        char fname[13];
        snprintf(fname, 12, "%d-recv.log", this->addr());
        FILE* lf = fopen(fname, "a");
        if (pkt->userdata()){
            PacketData* data = (PacketData*)pkt->userdata();
            fwrite(data->data(), data->size(), 1, lf);
        } else
            fwrite("NULL Data ", 9, 1, lf);
        fwrite("\n\n\n", 2, 1, lf);
        fclose(lf);

        // Forward data on to application.
        if (app_) {
            app_->process_data(hdr_cmn::access(pkt)->size(), pkt->userdata());
        }

        if (pkt->userdata())
            printf("Received payload of size %d\n", pkt->userdata()->size());

    } else if (oldHdr->type() == T_ack) {
        for (int i = 0; i < WINDOW_SIZE; ++i){
            if (window[i] != NULL) {
                hdr_Xyzzy* hdr = hdr_Xyzzy::access(window[i]);
                if (hdr->seqno() == oldHdr->seqno()){
                    printf("Got an ack for %d and marking the packet in the buffer.\n", oldHdr->seqno());
                    // found it, delete the packet
                    Packet::free(window[i]);
                    window[i] = NULL;
                    
                    break;
                } else if(hdr->seqno() < oldHdr->cumAck()) {
                    printf("[%d] Got a cumAck for %d and marking the packet %d in the buffer.\n", here_.addr_, oldHdr->cumAck(), hdr->seqno());
                    Packet::free(window[i]);
                    window[i] = NULL;
                }
            }
        }
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
