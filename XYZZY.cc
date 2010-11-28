#include "XYZZY/XYZZY.h"


int hdr_Xyzzy::offset_;


//these two static classes represent the agent and the header
//we aren't sure exactly how they work, just that they are used to
//some how bind the TCL and C++ classes together
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

DestNode* buddyNode::getDest(){
    if(!dests)
        return NULL;
    DestNode* retval = dests;
    while(retval){
        if(retval->reachable)
            return retval;
        else
            retval = retval->next;
    }
    return NULL;
}

//when the retry timer expires it
//calls retryPackets()
void RetryTimer::expire(Event*){
    t_->retryPackets();
}

void AppPassTimer::expire(Event*){
    t_->sndPktToApp();
}

void BuddyTimer::expire(Event*){
    t_->sendBuddyHeartBeats();
}
// send a heartbeat when the timer expires
void HeartbeatTimer::expire(Event*){
    t_->heartbeat(dn_);
}

// Generic timeout timer -- call the given callback function
void TimeoutTimer::expire(Event*){
    (t_->*fn_)(arg_);
    if (del_)
        delete this;
}

void BuddyHeartbeatTimer::expire(Event*){
    bn_->missedHBS++;
    if(bn_->missedHBS > B_FAILED_BEATS){
        t_->buddyMissedBeats(bn_);
    }
}

//this is the constructor, it creates the super and the
//retry timer in the initialization statments
XyzzyAgent::XyzzyAgent() : 
        Agent(PT_XYZZY),
        state_(STATE_NO_CONN),
        seqno_(0),
        coreTarget(NULL),
        sndBufLoc_(0),
        rcvBufLoc_(0),
        retry_(this),
        rcvNextExpected(0),
        rcvHighestReceived(0)
{
    //bind the varible to a Tcl varible
    bind("packetSize_", &size_);
    bind("id_", &id_);

    // initialize buffer
    for (int i = 0; i < WINDOW_SIZE; ++i)
        sndWindow[i] = NULL;

    for (int i = 0; i < WINDOW_SIZE; ++i)
        rcvWindow[i] = NULL;
    //set ackList to null so that the acklist functions
    //don't break when the encounter an empty list
    ackList = new ackListNode;
    ackList->seqno = 0;
    ackList->next = NULL;

    buddies = NULL;
    currSetupBuddy = NULL;

    destList = NULL;

    //schedule the retry timer
    retry_.sched(RETRY_TIME+0.5);
}

//retry packet resends unacked packets every half second or so
void XyzzyAgent::retryPackets() {

    // if we are not associated, don't do anything yet...
    if (state_ != STATE_ASSOCIATED)
        return;


    //we want to get the time and compare it against the time that
    //each packet in our sndWindow was sent, if more thatn the RETRY_TIME
    //has elapsed we want to send it again
    double timeNow = Scheduler::instance().clock();
    printf(C_RED "[%d] Starting retry loop at %f\n" C_NORMAL, here_.addr_, timeNow);
    for (int i = 0; i < WINDOW_SIZE; ++i) {
        if (sndWindow[i] != NULL && timeNow - timeSent[i] > RETRY_TIME) {
            printf(C_RED "[%d] ** RETRYING PACKET %d **\n" C_NORMAL, here_.addr_, hdr_Xyzzy::access(sndWindow[i])->seqno());

            // send again.
            // this needs to be copied here because recv frees packets when
            // it gets them and if it frees a packet that is still in our sndWindow
            // and then it gets called again because it was dropped...Bad things happen
            // We need to call setupPacket to get the new destination information
            //target_->recv(sndWindow[i]->copy());
            setupPacket();
            Packet* newpkt = sndWindow[i]->copy();
                hdr_ip::access(newpkt)->daddr() = daddr();
            hdr_ip::access(newpkt)->dport() = dport();
            hdr_ip::access(newpkt)->saddr() = addr();
            hdr_ip::access(newpkt)->sport() = port();
            send(newpkt, 0);

            // TODO: let buddies know we retried

            //now we increment how many times we have tried to send the packet and update
            //the time stamp
            numTries[i]++;
            timeSent[i] = Scheduler::instance().clock();
        }
    }

    //set the timer to fire again
    if (retry_.status() == TIMER_IDLE)
        retry_.sched(RETRY_TIME);
    else if (retry_.status() == TIMER_PENDING){
        retry_.cancel();
    }
    retry_.resched(RETRY_TIME);
    printf(C_RED "[%d] Ending retry loop at %f\n" C_NORMAL, here_.addr_, timeNow);
}

bool XyzzyAgent::recordPacket(Packet* pkt, double time) {
    if (sndWindow[sndBufLoc_] != NULL){
        // TODO: delay somehow until there's room for another packet.
        // this is probably better handled in send
        //
        // For now, we'll switch to another destination if one is available
        /*
        primaryDest->reachable = false;
        nextDest();

        printf(C_BLUE "[%d] Trying to resend via another route!  packet lost. (seqno: %d) in the way of (seqno: %d)\n" C_NORMAL, here_.addr_, hdr_Xyzzy::access(sndWindow[sndBufLoc_])->seqno(), hdr_Xyzzy::access(pkt)->seqno());

        setupPacket();
        hdr_ip::access(pkt)->daddr() = daddr();
        hdr_ip::access(pkt)->dport() = dport();
        hdr_ip::access(pkt)->saddr() = addr();
        hdr_ip::access(pkt)->sport() = port();

        //send(pkt, 0);
        */

        // We can schedule a timeout timer to retry this
        printf(C_BLUE "[%d] No room for pkt: %d, schedulting rerecord\n", here_.addr_, hdr_Xyzzy::access(pkt)->seqno());
        TimeoutTimer* tt = new TimeoutTimer(this, &XyzzyAgent::retryRecordPacket, (void*)pkt, true);
        tt->sched(RETRY_TIME);
        
        return false;
    } else {
        //send to buddies
        forwardToBuddies(pkt, B_SENT_MSG);

        //store the packet we just sent in the
        //sndWindow and set all the relevant varibles
        sndWindow[sndBufLoc_] = pkt;
        numTries[sndBufLoc_] = 1;
        timeSent[sndBufLoc_] = time;

        //move the buffer location forward one.
        //and move it back around if it has moved
        //past the bounds of the array
        sndBufLoc_++;
        sndBufLoc_ %= WINDOW_SIZE;
        printf(C_GREEN "[%d]  New buffer location: %d for pkt %d\n" C_NORMAL, here_.addr_, sndBufLoc_, hdr_Xyzzy::access(pkt)->seqno());

        return true;
    }
}

void XyzzyAgent::retryRecordPacket(void* arg){
    Packet* pkt = (Packet*)arg;
    printf(C_BLUE "[%d] Retrying record packet: %d\n", here_.addr_, hdr_Xyzzy::access(pkt)->seqno());
    if (recordPacket(pkt, Scheduler::instance().clock()) == true)
        send(pkt, 0);
}


//this function is called by ns2 when the app attached to this agent wants
//to send a message over the link
void XyzzyAgent::sendmsg(int nbytes, AppData* data, const char* flags) {

    if (state_ == STATE_NO_CONN){
        // initiate a connection

        // build init packet
        setupPacket();
        Packet* pkt = allocpkt();

        // write interface addresses to the data section
        int length = sizeof(int)*(numIfaces_+numOfBuddies_);
        PacketData* iflist = new PacketData(length);

        int* addrs = (int*)iflist->data();
        int idx = 0;
        for(IfaceNode* current = ifaceList; current != NULL; current = current->next){
            addrs[idx] = current->iNsAddr;
            idx++;
        }
/*        for(buddyNode* current = buddies; current != NULL; current = current->next){
            addrs[idx] = current->iNsAddr;
            idx++;
        }
*/
        //set the type and size of the packet payload
        //in the common header
        hdr_cmn::access(pkt)->ptype() = PT_XYZZY;
        hdr_cmn::access(pkt)->size() = length;

        hdr_Xyzzy* hdr = hdr_Xyzzy::access(pkt);

        hdr->type() = T_init;
        hdr->seqno() = init_ = rand();

        pkt->setdata(iflist);

        send(pkt, 0);

        state_ = STATE_ASSOCIATING;

        Packet *sp = allocpkt();
        hdr_Xyzzy::access(sp)->seqno() = state_;
        sendToBuddies(sp, T_state);
    }



    // TODO: Once we have states, this should be satrted during connection initiation
    //      Maybe not... It may be good to have the timer start here for each destination. This way, it will only be running for the current destination and any that are down.

    // if the heartbeat timer isnt running, start it up...
    if (state_ == STATE_ASSOCIATED){
        if (primaryDest->hb_){
            if (primaryDest->hb_->status() == TIMER_IDLE)
                primaryDest->hb_->sched(HB_INTERVAL);
            else if (primaryDest->hb_->status() == TIMER_PENDING){
            } else {
                primaryDest->hb_->resched(HB_INTERVAL);
            }
        } else {
            primaryDest->hb_ = new HeartbeatTimer(this, primaryDest);
            primaryDest->hb_->sched(HB_INTERVAL);
        }
    }

    //create a packet to put data in
    Packet* p;

    //if the maximum packet size is zero...somthing has gone terribly wrong
    if (size_ == 0)
        printf(C_RED "[%d] Xyzzy size_ is 0!\n" C_NORMAL, here_.addr_);

    //this will eventually handle fragmentation
    if (data && nbytes > size_) {
        printf("[%d] Xyzzy data does not fit in a single packet. Abort\n", here_.addr_);
         return;
    }

    setupPacket();

    //allocate a new packet from the pool
    p = allocpkt();

    //set the type and size of the packet payload
    //in the common header
    hdr_cmn::access(p)->ptype() = PT_XYZZY;
    hdr_cmn::access(p)->size() = nbytes;

    //set the sequence number and type  of our header
    hdr_Xyzzy::access(p)->seqno() = ++seqno_;
    hdr_Xyzzy::access(p)->type() = T_normal;

    //put our data into the packet
    p->setdata(data);

    //copy the packet because as soon as we pass it to the receive function
    //it will be freed and we need to keep a copy of it just incase it was
    //dropped and needs to be retransmitted
    Packet* pcopy = p->copy();
    //target_->recv(p);

    forwardToBuddies(pcopy, B_SENT_MSG);

    // Only send if we are associated
    if (state_ == STATE_ASSOCIATED)
    {
        send(p, 0);
    }

    recordPacket(pcopy, Scheduler::instance().clock());

}

// this function should be called before sending a packet in order to setup
// the source and destination values in the header
void XyzzyAgent::setupPacket(Packet* pkt, DestNode* dn){
    DestNode* dest;
    if (pkt){
        // find the dest node matching the source in this packet
        dest = destList;
        while((dest)){
            if (dest->iNsAddr == hdr_ip::access(pkt)->src().addr_
                && dest->iNsPort == hdr_ip::access(pkt)->src().port_){
                break;
            } else {
                dest = dest->next;
            }
        }
        if (dest == NULL)
            dest = primaryDest;
    } else if (dn) {
        dest = dn;
    } else {
        dest = primaryDest;
    }
    // set source info
    if (coreTarget != NULL){
        daddr() = dest->iNsAddr;
        dport() = dest->iNsPort;
        Packet* routingPacket = allocpkt();

        Connector* conn = (Connector*) coreTarget->find(routingPacket);
        Packet::free(routingPacket);

        IfaceNode* current = ifaceList;

        while(current != NULL){
            if (current->opLink == conn){
                addr() = current->iNsAddr;
                port() = current->iNsPort;
                target_ = current->opTarget;
                break;
            } else {
                current = current->next;
            }
        }
    }

    // set dest info
    daddr() = dest->iNsAddr;
    dport() = dest->iNsPort;
}

//this function is used to add a sequence number to our list of
//received packets when we get a new packet
void XyzzyAgent::updateCumAck(int seqno){
    ackListNode* usefulOne = NULL;

    //if the list is empty we want to create a
    //new list node to be the head of the list
    //and set it to be the useful one that we will add thigns to later
    if(!ackList){
        ackList = new ackListNode;
        usefulOne = ackList;

    //if there exists a list, we need to find the point in the list were this
    //packet fits and put it there
    }else{
        ackListNode* current = ackList;

        //crawl through the list of pakcets
        do{
            //if the current node is still less then the seqence number we want to
            //place advance usefulOne and loop again if there is still list
            if(current->seqno < seqno){
                usefulOne = current;

            //if the next element has a larger sequence number
            //we need to escape the loop because useful one contains the node we need
            }else if (current->seqno > seqno){
                break;

            //if we receive a packet we already acked just die
            //noting needs to be done
            }else{
                return;
            }
        }while((current = current->next));

        //once we have found the last node with a lower sequence number
        //we need to create a new node for our sequence numbe and then
        //repair the chain
        if (usefulOne){
            usefulOne->next = new ackListNode;
            usefulOne->next->next = current;

            //set useful one to the new node
            usefulOne = usefulOne->next;
        }
    }

    //set the sequence no in useful one because it is now set to the node that
    //needs to be set in either case
    if (usefulOne)
        usefulOne->seqno = seqno;

}

//as we receive and ack more messages eventually the end of the list closest to the
//head will become consecutive and thus nolonger nesscary as the head of the list
//represents a cumulative ack, so we prune them off
void XyzzyAgent::ackListPrune(){

    //check to make sure there atleast two
    //elements in the list
    if(!ackList || !ackList->next)
        return;

    //declare book keeping stuff
    ackListNode* usefulOne;
    ackListNode* current = ackList;
    int last_seqno = ackList->seqno;
    int count = 0;

    //step through the list and count the number of consecutive
    //sequence numbers at the beginning. break as soon as you hit a
    //sequence number that is not one more than the previous one
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

    //delete the packets we counted before
    for(int i = 0; i < count; ++i){
        if (current->next){
            n = current->next;
            delete current;
            current = n;
        }
    }
    ackList = n;
}

//this is the function called by ns2 when the node this agent is bound
//to receives a message
//
//WARNING:  THIS METHOD RELEASES ANY PACKETS IT RECEIVES...MAKE SURE IF
//A PACKET IS PASSED TO RECEIVE IT IS EITHER COPIED FOR BOOK KEEPING STRUCTURES,
//COPIED FOR RECEIVE, OR YOU ARE HAPPY WITH IT DISSAPPEARING
void XyzzyAgent::recv(Packet* pkt, Handler*) {
    printf("[%d] receiving...\n", here_.addr_); 
    //get the header out of the packet we just got
    hdr_Xyzzy* oldHdr = hdr_Xyzzy::access(pkt);
     printf("[%d] seqno: %d \n", here_.addr_, oldHdr->seqno());

    // if we are not assiciated, only allow association packets to be processed
    if (state_ != STATE_ASSOCIATED) {
        if (state_ == STATE_NO_CONN) {
            if (oldHdr->type() == T_init){
                
                setupPacket(pkt);
                Packet* p = allocpkt();

                int length = sizeof(int)*(numIfaces_+numOfBuddies_);
                PacketData* iflist = new PacketData(length);

                //set up its common header values ie protocol type and size of data
                hdr_cmn::access(p)->ptype() = PT_XYZZY;
                hdr_cmn::access(p)->size() = length;

                int* addrs = (int*)iflist->data();
                int idx = 0;
                for(IfaceNode* current = ifaceList; current != NULL; current = current->next){
                    addrs[idx] = current->iNsAddr;
                    idx++;
                }
/*
                for(buddyNode* current = buddies; current != NULL; current = current->next){
                    addrs[idx] = current->iNsAddr;
                    idx++;
                }
*/
                p->setdata(iflist);

                //create a new protocol specific header for the packet
                hdr_Xyzzy* initHdr= hdr_Xyzzy::access(p);

                //set the fields in our new header to the ones that were
                //in the packet we just received
                initHdr->type() = T_initack;
                initHdr->seqno() = oldHdr->seqno();

                //save the first seqno()
                rcvHighestReceived = 0; 
                rcvNextExpected = rcvHighestReceived + 1;
                // generate random number and save it for comparison
                initHdr->cumAck() = init_ = rand();

                printf(C_WHITE "[%d] Got an init (%d), generating initack (%d)\n" C_NORMAL, here_.addr_, initHdr->seqno(), initHdr->cumAck());

                // set up ip header
                hdr_ip* newip = hdr_ip::access(p);
                hdr_ip* oldip = hdr_ip::access(pkt);

                //set the ip header so that the ack packet will
                //go back to the data packet's source
                newip->dst() = oldip->src();
                newip->prio() = oldip->prio();
                newip->flowid() = oldip->flowid();

                send(p, 0);

                state_ = STATE_ASSOCIATING;

                Packet *sp = allocpkt();
                hdr_Xyzzy::access(sp)->seqno() = state_;
                sendToBuddies(sp, T_state);

                // TODO: pull addresses out of packet


            } else {
                return;
            }
        } else if (state_ == STATE_ASSOCIATING) {
            // we should have an initack now.
            if (oldHdr->type() == T_initack){
                // if the cumAck is -1, then we are the initiator and should respond
                if (oldHdr->seqno() == init_){
                    
                    setupPacket(pkt);
                    Packet* p = allocpkt();

                    //set up its common header values ie protocol type and size of data
                    hdr_cmn::access(p)->ptype() = PT_XYZZY;
                    hdr_cmn::access(p)->size() = 0;

                    //create a new protocol specific header for the packet
                    hdr_Xyzzy* initHdr= hdr_Xyzzy::access(p);

                    //set the fields in our new header to the ones that were
                    //in the packet we just received
                    initHdr->type() = T_initack;
                    initHdr->seqno() = -1;
                    initHdr->cumAck() = oldHdr->cumAck();

                    printf(C_WHITE "[%d] Got an initack (%d,%d), generating initack (%d,%d)\n" C_NORMAL, here_.addr_, oldHdr->seqno(), oldHdr->cumAck(), initHdr->seqno(), initHdr->cumAck());

                    // set up ip header
                    hdr_ip* newip = hdr_ip::access(p);
                    hdr_ip* oldip = hdr_ip::access(pkt);

                    //set the ip header so that the ack packet will
                    //go back to the data packet's source
                    newip->dst() = oldip->src();
                    newip->prio() = oldip->prio();
                    newip->flowid() = oldip->flowid();

                    send(p, 0);

                    state_ = STATE_ASSOCIATED;

                    Packet *sp = allocpkt();
                    hdr_Xyzzy::access(sp)->seqno() = state_;
                    sendToBuddies(sp, T_state);

                    printf(C_WHITE "[%d] Associated.\n" C_NORMAL, here_.addr_);

                    // TODO: pull addresses outr of packet

                // if cumAck is correct, we have established the connection
                } else if (oldHdr->cumAck() == init_){
                    state_ = STATE_ASSOCIATED;

                    Packet *sp = allocpkt();
                    hdr_Xyzzy::access(sp)->seqno() = state_;
                    sendToBuddies(sp, T_state);

                    printf(C_WHITE "[%d] Associated.\n" C_NORMAL, here_.addr_);
                } else {
                    // TODO: Maybe send a kill connection message?
                }
            } else {
                return;
            }
        }
        return;
    }

    //switch on the packet type
    //if it is a normal header
    if (oldHdr->type() == T_normal) {
         printf("[%d] seqno: %d NE: %d HR %d\n", here_.addr_, oldHdr->seqno(), rcvNextExpected, rcvHighestReceived);

        updateRcvWindow(pkt);
        forwardToBuddies(pkt, B_RCVD_MSG);

        // Send back an ack, so allocate a packet
        setupPacket(pkt);
        Packet* ap = allocpkt();

        //set up its common header values ie protocol type and size of data
        hdr_cmn::access(ap)->ptype() = PT_XYZZY;
        hdr_cmn::access(ap)->size() = 0;

        //TODO: wrap around oh noes!

        //update the list of acked packets
        //and prune any consecutive ones from the front
        //of the list...we don't need them any more
        updateCumAck(oldHdr->seqno());
        ackListPrune();

        //create a new protocol specific header for the packet
        hdr_Xyzzy* newHdr = hdr_Xyzzy::access(ap);

        //set the fields in our new header to the ones that were
        //in the packet we just received
        newHdr->type() = T_ack;
        newHdr->seqno() = oldHdr->seqno();
        newHdr->cumAck() = ackList->seqno;

        if (pkt->userdata())
            printf(C_CYAN "[%d] Generating ack for seqno %d (cumAck: %d).  Received payload of size %d\n" C_NORMAL, here_.addr_, newHdr->seqno(), newHdr->cumAck(), pkt->userdata()->size());
        else
            printf(C_CYAN "[%d] Generating ack for seqno %d (cumAck: %d)\n" C_NORMAL, here_.addr_, newHdr->seqno(), newHdr->cumAck());

        // set up ip header
        hdr_ip* newip = hdr_ip::access(ap);
        hdr_ip* oldip = hdr_ip::access(pkt);

        //set the ip header so that the ack packet will
        //go back to the data packet's source
        newip->dst() = oldip->src();
        newip->prio() = oldip->prio();
        newip->flowid() = oldip->flowid();

        //send the ack packet, since it contains a cumulative
        //storage and retransmission is not nesscary
        //target_->recv(ap);
        Packet *apClone = ap->copy();
        send(ap, 0);
        forwardToBuddies(apClone, B_SENT_ACK);
        Packet::free(apClone);
        
        // log packet contents to file so we can see what we got
        // and what we lossed and how many copies of each O.o
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
        /*if (app_) {
            app_->process_data(hdr_cmn::access(pkt)->size(), pkt->userdata());
        }*/

        //pump a packet from the buffer
        sndPktToApp();

    //if the packet was an ack
    } else if (oldHdr->type() == T_ack) {
        forwardToBuddies(pkt, B_RCVD_ACK);
        updateSndWindow(pkt);
    // if this is a heartbeat message
    } else if (oldHdr->type() == T_heartbeat) {
        // check whether this is a response
        if(oldHdr->heartbeat() > 0){
            // send back a response
            
            setupPacket(pkt);
            Packet* ap = allocpkt();

            //set up its common header values ie protocol type and size of data
            hdr_cmn::access(ap)->ptype() = PT_XYZZY;
            hdr_cmn::access(ap)->size() = sizeof(hdr_Xyzzy);

            //create a new protocol specific header for the packet
            hdr_Xyzzy* newHdr = hdr_Xyzzy::access(ap);

            //set the fields in our new header to the ones that were
            //in the packet we just received
            newHdr->type() = T_heartbeat;
            newHdr->seqno() = hdr_Xyzzy::access(pkt)->seqno();
            newHdr->heartbeat() = hdr_Xyzzy::access(pkt)->heartbeat()*-1;

            printf(C_PURPLE "[%d] I'm not dead yet! (hb:%d, dest:%d)\n" C_NORMAL, here_.addr_, newHdr->heartbeat(), hdr_ip::access(ap)->daddr());

            // set up ip header
            hdr_ip* newip = hdr_ip::access(ap);
            hdr_ip* oldip = hdr_ip::access(pkt);

            //set the ip header so that the heartbeat response packet will
            //go back to the data packet's source
            newip->dst() = oldip->src();
            newip->prio() = oldip->prio();
            newip->flowid() = oldip->flowid();

            send(ap, 0);

        // if we got the heartbeat and there's a timeout timer running, stop it.
        // TODO: Expand this to check which dest this was and have separate timers per destination
        } else {
            DestNode* dest = findDest(hdr_ip::access(pkt)->saddr());
            if (dest){
                printf(C_PURPLE "[%d] 'Ere, he says he's not dead. (hb:%d, dest:%d)\n" C_NORMAL, here_.addr_, hdr_Xyzzy::access(pkt)->heartbeat(), dest->iNsAddr);
                if (dest->hbTimeout_ != NULL){
                    if (dest->hbTimeout_->status() == TIMER_PENDING)
                        dest->hbTimeout_->cancel();
                    delete dest->hbTimeout_;
                    dest->hbTimeout_ = NULL;
                }
                dest->reachable = true;
                dest->rcount = 0;
            } else {
                printf(C_PURPLE "[%d] Got a heartbeat from someone I don't know... (hb:%d, dest:%d)\n" C_NORMAL, here_.addr_, hdr_Xyzzy::access(pkt)->heartbeat(), hdr_ip::access(pkt)->saddr());
            }
        }
    }
    else if(hdr_Xyzzy::access(pkt)->type() == T_buddy) {
        switch(hdr_Xyzzy::access(pkt)->sndRcv())
        {
            case B_SENT_MSG:
                buddyRecordPacket(pkt->copy());
                break;
            case B_SENT_ACK:
                updateCumAck(hdr_Xyzzy::access(pkt)->seqno());
                ackListPrune();
                break;
            case B_RCVD_MSG:
                updateRcvWindow(pkt);
                break;
            case B_RCVD_ACK:
                updateSndWindow(pkt);
                break;
            default:
                break;
        }
    }
    else if(hdr_Xyzzy::access(pkt)->type() == T_state) {
        state_ = hdr_Xyzzy::access(pkt)->seqno();
    }
    else if(hdr_Xyzzy::access(pkt)->type() == T_rne) {
        int i;
        for(i = rcvNextExpected; i < hdr_Xyzzy::access(pkt)->seqno(); i++)
        {
            rcvWindow[i % WINDOW_SIZE] = NULL;
        }
        rcvNextExpected = hdr_Xyzzy::access(pkt)->seqno();
    }

    //free the packet we just received
    //BEWARE THAT THIS FUCTION DOES THIS
    Packet::free(pkt);
}

void XyzzyAgent::updateRcvWindow(Packet *pkt)
{
    // Determine if we have room for this in the buffer
    //
    //Packet == NE > HR ... or anything else;
    //we have room in the buffer;
    printf("[%d] seqno: %d NE: %d HR %d\n", here_.addr_, hdr_Xyzzy::access(pkt)->seqno(), rcvNextExpected, rcvHighestReceived);
    if(hdr_Xyzzy::access(pkt)->seqno() == rcvNextExpected) {
        int tmp = hdr_Xyzzy::access(pkt)->seqno() % WINDOW_SIZE;
        rcvWindow[tmp] = pkt->copy();

        //if this is the new highes packet...fix that
        if(hdr_Xyzzy::access(pkt)->seqno() >  rcvHighestReceived)
            ++rcvHighestReceived ;
        //need to figure out how to move next expected forward...
        /*for(rcvNextExpected; rcvNextExpected < rcvHighestReceived + 1; ++rcvNextExpected){
            if(rcvWindow[rcvNextExpected % WINDOW_SIZE] == NULL)
                break;
        }*/

    }
    // HR > packet >=   NE 
    else if(hdr_Xyzzy::access(pkt)->seqno() < rcvHighestReceived && hdr_Xyzzy::access(pkt)->seqno() > rcvNextExpected) {

        //see if the space in the buffer is empty...
        if(rcvWindow[hdr_Xyzzy::access(pkt)->seqno() % WINDOW_SIZE] == NULL) {
            rcvWindow[hdr_Xyzzy::access(pkt)->seqno() % WINDOW_SIZE] = pkt->copy();

        //uh oh there is a packet there with the wrong sequnce number, this shouldn't happen...
        } else if(hdr_Xyzzy::access(rcvWindow[hdr_Xyzzy::access(pkt)->seqno() % WINDOW_SIZE])->seqno() != hdr_Xyzzy::access(pkt)->seqno()) {
             printf(C_RED "[%d] Receive window has packet with different seqno," 
                     "dropping packet %d, this shouldn't happen, misbehaving packet is %d\n" C_NORMAL,
                     here_.addr_, hdr_Xyzzy::access(pkt)->seqno(), hdr_Xyzzy::access(rcvWindow[hdr_Xyzzy::access(pkt)->seqno() % WINDOW_SIZE])->seqno());
        }

        //else we already have it and should just ack it again...

        
    //packet > HR > NE
    } else if(rcvHighestReceived > rcvNextExpected && hdr_Xyzzy::access(pkt)->seqno() >  rcvHighestReceived) {
        
        //This should wraparounds right
        if(hdr_Xyzzy::access(pkt)->seqno() >=  rcvNextExpected + WINDOW_SIZE){
            printf(C_RED "[%d] Receive window full (wrap around), dropping packet %d\n" C_NORMAL, here_.addr_, hdr_Xyzzy::access(pkt)->seqno());
            return;
        }


        if(rcvWindow[hdr_Xyzzy::access(pkt)->seqno() % WINDOW_SIZE] == NULL){
            rcvWindow[hdr_Xyzzy::access(pkt)->seqno() % WINDOW_SIZE] = pkt->copy();
            rcvHighestReceived = hdr_Xyzzy::access(pkt)->seqno();
        }



    } else if(rcvHighestReceived+1 == rcvNextExpected) {
        //catch wrap arounds 
        if(hdr_Xyzzy::access(pkt)->seqno() >=  rcvNextExpected + WINDOW_SIZE){
            
            printf(C_RED "[%d] Receive window full (wrap around), dropping packet %d\n" C_NORMAL, here_.addr_, hdr_Xyzzy::access(pkt)->seqno());
            return;
        }


        if(rcvWindow[hdr_Xyzzy::access(pkt)->seqno() % WINDOW_SIZE] == NULL){
            rcvWindow[hdr_Xyzzy::access(pkt)->seqno() % WINDOW_SIZE] = pkt->copy();
            rcvHighestReceived = hdr_Xyzzy::access(pkt)->seqno();
        }else{

            printf(C_RED "[%d] THE DEMON IS HERE, dropping packet %d\n" C_NORMAL, here_.addr_, hdr_Xyzzy::access(pkt)->seqno());
        }
    } else {
        printf(C_RED "[%d] Receive window full, dropping packet %d\n" C_NORMAL, here_.addr_, hdr_Xyzzy::access(pkt)->seqno());
        return;
    }
}

void XyzzyAgent::updateSndWindow(Packet* pkt)
{
    //need to find the packet that was acked in our sndWindow
    for (int i = 0; i < WINDOW_SIZE; ++i){

        //if the packet is a packet in this element...
        if (sndWindow[i] != NULL) {
            hdr_Xyzzy* hdr = hdr_Xyzzy::access(sndWindow[i]);

            //and its sequence number is == to the one packet we just got
            if (hdr->seqno() == hdr_Xyzzy::access(pkt)->seqno()){
                printf(C_CYAN "[%d] Got an ack for %d and marking the packet in the buffer.\n" C_NORMAL, here_.addr_, hdr_Xyzzy::access(pkt)->seqno());
                // found it, delete the packet
                Packet::free(sndWindow[i]);
                sndWindow[i] = NULL;

                break;
            //if the packet is less than the cumulative ack that came in
            //on the newest packet we can dump it too while were at it.
            } else if(hdr->seqno() < hdr_Xyzzy::access(pkt)->cumAck()) {
                printf(C_CYAN "[%d] Got a cumAck for %d and marking the packet %d in the buffer.\n" C_NORMAL, here_.addr_, hdr_Xyzzy::access(pkt)->cumAck(), hdr->seqno());
                Packet::free(sndWindow[i]);
                sndWindow[i] = NULL;
            }
        }
    }
}

// set primaryDest to next available ip
void XyzzyAgent::nextDest(){
    DestNode* current;
    for(current = destList; current!=NULL; current = current->next){
        if (current->reachable){
            primaryDest = current;
            break;
        }
    }
    if(current == NULL)
        printf(C_YELLOW "[%d] No more destination IPs!\n" C_NORMAL, here_.addr_);
}

DestNode* XyzzyAgent::findDest(int addr) {
    for(DestNode* current = destList; current != NULL; current = current->next){
        if (current->iNsAddr == addr)
            return current;
    }
    return NULL;
}

void XyzzyAgent::heartbeat(DestNode* dest){
    //DestNode* dest = primaryDest;

    // construct a heartbeat packet and send
    setupPacket(NULL, dest);
    Packet* p = allocpkt();

    //set the type and size of the packet payload
    //in the common header
    hdr_cmn::access(p)->ptype() = PT_XYZZY;
    hdr_cmn::access(p)->size() = sizeof(hdr_Xyzzy);

    //set the type  of our header
    // the sequence number is -1, and the heartbeat 
    // variable is set to the current seqno
    // when the other host responds, it will have heartbeat*-1
    hdr_Xyzzy::access(p)->seqno() = -1;
    hdr_Xyzzy::access(p)->heartbeat() = seqno_;
    hdr_Xyzzy::access(p)->type() = T_heartbeat;


    // setup timeout timer
    if (dest->hbTimeout_ == NULL){
        dest->hbTimeout_ = new TimeoutTimer(this, &XyzzyAgent::heartbeatTimeout, (void*)dest);
        dest->hbTimeout_->sched(0.2);
    }

    printf(C_PURPLE "[%d] Bring out yer dead! (hb:%d, dest:%d)\n" C_NORMAL, here_.addr_, seqno_, dest->iNsAddr);
    send(p, 0);
}

void XyzzyAgent::heartbeatTimeout(void* arg) {
    DestNode* dn = (DestNode*) arg;

    if (dn){
        dn->rcount++;
        printf(C_PURPLE "[%d] No you're not, you'll be stone dead in a moment. (hb:%d, rcount:%d, dest: %d)\n" C_NORMAL, here_.addr_, seqno_, dn->rcount, dn->iNsAddr);
        if (dn->rcount > MAX_HB_MISS){
            dn->reachable = false;
            nextDest();
            printf(C_YELLOW "[%d] New destination: %d\n" C_NORMAL, here_.addr_, primaryDest->iNsAddr);

            // schedule another heartbeat 
            if (dn->hb_){
                if (dn->hb_->status() == TIMER_IDLE)
                    dn->hb_->sched(HB_INTERVAL);
                else if (dn->hb_->status() == TIMER_PENDING){
                } else {
                    dn->hb_->resched(HB_INTERVAL);
                }
            } else {
                dn->hb_ = new HeartbeatTimer(this, dn);
                dn->hb_->sched(HB_INTERVAL);
            }
        }
    }
    delete dn->hbTimeout_;
    dn->hbTimeout_ = NULL;
}


//This function processes commands to the agent from the TCL script
int XyzzyAgent::command(int argc, const char*const* argv) {

    if (argc == 3 && strcmp(argv[1], "set-multihome-core") == 0) {
        coreTarget = (Classifier *) TclObject::lookup(argv[2]);
        if(coreTarget == NULL)
        {
            return (TCL_ERROR);
        }
        return (TCL_OK);
    } else if (argc == 3 && strcmp(argv[1], "set-primary-destination") == 0) {
        Node* opNode = (Node *) TclObject::lookup(argv[2]);
        if(opNode == NULL)
          {
            return (TCL_ERROR);
          }
        
        // see if the dest is in the list and make it the primary
        if (destList == NULL){
            printf(C_YELLOW "[%d] Trying to set primary dest when there are no destinations\n" C_NORMAL, here_.addr_);
            return (TCL_ERROR);
        }

        for(DestNode* current = destList; current != NULL; current = current->next){
           if (current->iNsAddr == opNode->address()){
                primaryDest = current;
                return (TCL_OK);
           }
        }

        return (TCL_ERROR);
        
    } else if (argc == 5 && strcmp(argv[1], "add-buddy-destination") == 0) {
        XyzzyAgent* opAgent= (XyzzyAgent*) TclObject::lookup(argv[2]);
        if (opAgent == NULL){
            return (TCL_OK);
        }

        for (buddyNode* current = buddies; current != NULL; current = current->next){
            if (current->id == opAgent->id_){
                // this is the one...
                DestNode* newdest = new DestNode;
                newdest->iNsAddr = atoi(argv[3]);
                newdest->iNsPort = atoi(argv[4]);

                newdest->next = current->dests;
                current->dests = newdest;
            } else if (current->next == NULL){
                current->next = new buddyNode;
                current->next->id = opAgent->id_;
            }
        }

        return (TCL_OK);

    } else if (argc == 4 && strcmp(argv[1], "send") == 0) {
        PacketData* d = new PacketData(strlen(argv[3]) + 1);
        strcpy((char*)d->data(), argv[3]);
        sendmsg(atoi(argv[2]), d);
        return TCL_OK;
    } else if (argc == 4 && strcmp(argv[1], "add-multihome-destination") == 0) {
        int iNsAddr = atoi(argv[2]);
        int iNsPort = atoi(argv[3]);
        AddDestination(iNsAddr, iNsPort);
        return (TCL_OK);
    } else if (argc == 5 && strcmp(argv[1], "sendmsg") == 0) {
        PacketData* d = new PacketData(strlen(argv[3]) + 1);
        strcpy((char*)d->data(), argv[3]);
        sendmsg(atoi(argv[2]), d, argv[4]);
        return TCL_OK;
    }else if (argc == 6 && strcmp(argv[1], "add-multihome-interface") == 0) {
        // get interface information and pass it to AddInterface
        int iNsAddr = atoi(argv[2]);
        int iNsPort = atoi(argv[3]);
        NsObject* opTarget = (NsObject *) TclObject::lookup(argv[4]);
        if(opTarget == NULL)
            
        {
            return (TCL_ERROR);
        }
        NsObject* opLink = (NsObject *) TclObject::lookup(argv[5]);
        if(opLink == NULL)
        {
            return (TCL_ERROR);
        }
        AddInterface(iNsAddr, iNsPort, opTarget, opLink);
        return (TCL_OK);
    }

    return Agent::command(argc, argv);
}

void XyzzyAgent::sendBuddyHeartBeats(){
    //build heart beat packets and loop 
    //over buddies sending them to the first 
    //interface
    
    //build heartbeat packet
    Packet* pkt = allocpkt();
    
    
    //loop over buddies
    buddyNode* currentBuddy = buddies;
    while(currentBuddy){

        if(currentBuddy->status == B_DEAD)
            continue;

        DestNode* d = currentBuddy->getDest();

        if(!d)
            continue;

        buddySend(pkt, d);

        currentBuddy = currentBuddy->next;
    }


}
void XyzzyAgent::forwardToBuddies(Packet* p, char sndRcv){
    //this will loop over the buddies and send the packet I
    //just sent/received to them.  Not sure how the multiple interface
    //thing works so not sure how buddy will know where 
    //it came from /shrug
    
    Packet *pkt = p->copy();

    hdr_Xyzzy::access(pkt)->sndRcv() = sndRcv;
    hdr_Xyzzy::access(pkt)->type() = T_buddy;

    buddyNode* currentBuddy = buddies;

    while(currentBuddy){

        //needs to grab the active interface for a buddy
        //setup to send the packet and then copy it to the buddy
        
        if(currentBuddy->status == B_DEAD){
            currentBuddy = currentBuddy->next;
            continue;
        }

        DestNode* d = currentBuddy->getDest();

        if(!d){
            currentBuddy = currentBuddy->next;
            continue;
        }

        buddySend(pkt, d);

        send(pkt, 0);
        
        currentBuddy = currentBuddy->next;
    }

    Packet::free(pkt);

}

void XyzzyAgent::sendToBuddies(Packet* p, int type){
    //this will loop over the buddies and send the packet I
    //just sent/received to them.  Not sure how the multiple interface
    //thing works so not sure how buddy will know where 
    //it came from /shrug
    
    Packet *pkt = p->copy();

    buddyNode* currentBuddy = buddies;

    while(currentBuddy){

        //needs to grab the active interface for a buddy
        //setup to send the packet and then copy it to the buddy
        
        if(currentBuddy->status == B_DEAD){
            currentBuddy = currentBuddy->next;
            continue;
        }

        DestNode* d = currentBuddy->getDest();

        if(!d){
            currentBuddy = currentBuddy->next;
            continue;
        }

        buddySend(pkt, d);

        send(pkt->copy(), 0);
        
        currentBuddy = currentBuddy->next;
    }

    Packet::free(pkt);

}
bool XyzzyAgent::buddySend(Packet* p, DestNode* dest){

    return true;
}
// Add an interface to the interface list
void XyzzyAgent::AddInterface(int iNsAddr, int iNsPort,
                                NsObject *opTarget, NsObject *opLink) {
    IfaceNode* iface = new IfaceNode;
    iface->iNsAddr = iNsAddr;
    iface->iNsPort = iNsPort;
    iface->opTarget = opTarget;
    iface->opLink = opLink;

    IfaceNode* head = ifaceList;

    iface->next = head;
    ifaceList = iface;
    numIfaces_++;
}

//swap interfaces on the buddy and send a heartbeat
//or mark as dead if we don't have an extra interface.
void XyzzyAgent::buddyMissedBeats(buddyNode* buddy){

}

bool XyzzyAgent::buddyRecordPacket(Packet* pkt) {
    if (sndWindow[sndBufLoc_] != NULL){
        // We can schedule a timeout timer to retry this
        printf(C_BLUE "[%d] No room for pkt: %d, schedulting rerecord\n", here_.addr_, hdr_Xyzzy::access(pkt)->seqno());
        
        return false;
    } else {
        int i;
        for(i = 0; i < WINDOW_SIZE; i++)
        {
            if(sndWindow[i] == NULL)
                continue;

            if(hdr_Xyzzy::access(sndWindow[i])->seqno() == hdr_Xyzzy::access(pkt)->seqno())
            {        
                sndWindow[i] = pkt;
                numTries[i] = hdr_Xyzzy::access(pkt)->numTries();
                timeSent[i] = hdr_Xyzzy::access(pkt)->timeSent();
                return true;
            }
        }
        
        //store the packet we just sent in the
        //sndWindow and set all the relevant varibles
        sndWindow[sndBufLoc_] = pkt;
        numTries[sndBufLoc_] = hdr_Xyzzy::access(pkt)->numTries();
        timeSent[sndBufLoc_] = hdr_Xyzzy::access(pkt)->timeSent();

        //move the buffer location forward one.
        //and move it back around if it has moved
        //past the bounds of the array
        sndBufLoc_++;
        sndBufLoc_ %= WINDOW_SIZE;
        printf(C_GREEN "[%d]  New buffer location: %d for pkt %d\n" C_NORMAL, here_.addr_, sndBufLoc_, hdr_Xyzzy::access(pkt)->seqno());

        return true;
    }
}

// add a new destination to the destination list
void XyzzyAgent::AddDestination(int iNsAddr, int iNsPort) {
    // set source info
    DestNode* newDest = new DestNode;
    newDest->iNsAddr = iNsAddr;
    newDest->iNsPort = iNsPort;

    if (currSetupBuddy != NULL){
        // add destination to buddy instead of dest list
        newDest->next = currSetupBuddy->dests;
        currSetupBuddy->dests = newDest;
        printf(C_YELLOW "[%d] Adding dest %d to buddy \n" C_NORMAL, here_.addr_, iNsAddr);

    } else {
        // set the primary to the last destination added just in case the user does not set a primary
        primaryDest = newDest;

        DestNode* head = destList;

        newDest->next = head;
        destList = newDest;
        printf(C_YELLOW "[%d] Adding dest %d to node\n" C_NORMAL, here_.addr_, iNsAddr);
    }
}

//sends a packet in the rcv buffer to the listening app
void XyzzyAgent::sndPktToApp(){

    printf(C_GREEN "[%d]Forwarding packet to app...\n" C_NORMAL, here_.addr_);
    if(rcvWindow[rcvNextExpected % WINDOW_SIZE] == NULL){
        printf(C_RED "[%d]Still waiting on packet %d\n" C_NORMAL, here_.addr_, rcvNextExpected);
        return;
    }
    else{
        while(rcvWindow[rcvNextExpected % WINDOW_SIZE] != NULL){
              Packet* pkt = rcvWindow[rcvNextExpected % WINDOW_SIZE];
              rcvWindow[rcvNextExpected % WINDOW_SIZE] = NULL;
                
            printf(C_GREEN "[%d] Packet %d forwarded\n" C_NORMAL, here_.addr_, rcvNextExpected);
              char fname[13];  
              snprintf(fname, 12, "%d-pssd.log", this->addr());
              FILE* lf = fopen(fname, "a");
                
              if (pkt->userdata()){  
                  PacketData* data = (PacketData*)pkt->userdata();
                  fwrite(data->data(), data->size(), 1, lf);
              } else
                  fwrite("NULL Data ", 9, 1, lf);
              fwrite("\n\n\n", 2, 1, lf);
              fclose(lf);

              //pass it to an app if there is one
              
            if (app_) {
                app_->process_data(hdr_cmn::access(pkt)->size(), pkt->userdata());
            }

            Packet::free(pkt);

            ++rcvNextExpected;
        }
        
        Packet *sp = allocpkt();
        hdr_Xyzzy::access(sp)->seqno() = rcvNextExpected;
        sendToBuddies(sp, T_rne);
    }
}

/* vi: set tabstop=4 softtabstop=4 shiftwidth=4 expandtab : */
