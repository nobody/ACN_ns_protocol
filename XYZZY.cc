#include "XYZZY/XYZZY.h"


int hdr_Xyzzy::offset_;
int XyzzyAgent::id_counter;


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
    return retval;
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
    t_->buddyMissedBeats(bn_);
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
        buddyTimer_(this),
        rcvNextExpected(0),
        rcvHighestReceived(0)
{
    //bind the varible to a Tcl varible
    bind("packetSize_", &size_);
    bind("id_", &id_);
    bind("isActiveBuddy", &isActiveBuddy);
    bind("BuddyStatus", &myBuddyStatus);
    
    // Set up ID
    id_ = id_counter;
    id_counter++;

    isActiveBuddy = 0;

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

    myBuddyStatus = B_ACTIVE;
    isReceiving = false;

    //schedule the retry timer
    retry_.sched(RETRY_TIME+0.5);
    buddyTimer_.sched(0.2);

    char fileName[30];
    sprintf(fileName, "buddy%d.log", id_);
    buddyLog = fopen(fileName, "w");
}

XyzzyAgent::~XyzzyAgent()
{
    fclose(buddyLog);
}

//retry packet resends unacked packets every half second or so
void XyzzyAgent::retryPackets() {

    double timeNow = Scheduler::instance().clock();
    // if we are not associated, don't do anything yet...
    if (state_ == STATE_ASSOCIATED && myBuddyStatus == B_LEADING){

        //we want to get the time and compare it against the time that
        //each packet in our sndWindow was sent, if more thatn the RETRY_TIME
        //has elapsed we want to send it again
        printf(C_RED "[%d] Starting retry loop at %f\n" C_NORMAL, id_, timeNow);
        for (int i = 0; i < WINDOW_SIZE; ++i) {
            if (sndWindow[i] != NULL && timeNow - timeSent[i] > RETRY_TIME) {
                printf(C_RED "[%d] ** RETRYING PACKET %d **\n" C_NORMAL, id_, hdr_Xyzzy::access(sndWindow[i])->seqno());

                // send again.
                // this needs to be copied here because recv frees packets when
                // it gets them and if it frees a packet that is still in our sndWindow
                // and then it gets called again because it was dropped...Bad things happen
                // We need to call setupPacket to get the new destination information
                setupPacket();
                Packet* newpkt = sndWindow[i]->copy();
                hdr_ip::access(newpkt)->daddr() = daddr();
                hdr_ip::access(newpkt)->dport() = dport();
                hdr_ip::access(newpkt)->saddr() = addr();
                hdr_ip::access(newpkt)->sport() = port();
                send(newpkt, 0);

                //now we increment how many times we have tried to send the packet and update
                //the time stamp
                numTries[i]++;
                timeSent[i] = Scheduler::instance().clock();
            }
        }
    }

    //set the timer to fire again
    if (retry_.status() == TIMER_IDLE)
        retry_.sched(RETRY_TIME);
    else if (retry_.status() == TIMER_PENDING){
        retry_.cancel();
    }
    retry_.resched(RETRY_TIME);
    printf(C_RED "[%d] Ending retry loop at %f\n" C_NORMAL, id_, timeNow);
}

bool XyzzyAgent::recordPacket(Packet* pkt, double time) {
    if (sndWindow[sndBufLoc_] != NULL){
        // We can schedule a timeout timer to retry this
        printf(C_BLUE "[%d] No room for pkt: %d, scheduling rerecord\n", id_, hdr_Xyzzy::access(pkt)->seqno());
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
        printf(C_GREEN "[%d]  New buffer location: %d for pkt %d\n" C_NORMAL, id_, sndBufLoc_, hdr_Xyzzy::access(pkt)->seqno());

        return true;
    }
}

// callback to retry adding Packet to sent buffer
void XyzzyAgent::retryRecordPacket(void* arg){
    Packet* pkt = (Packet*)arg;
    printf(C_BLUE "[%d] Retrying record packet: %d\n", id_, hdr_Xyzzy::access(pkt)->seqno());
    if (recordPacket(pkt, Scheduler::instance().clock()) == true){
        setupPacket();
        send(pkt, 0);
    }
}


//this function is called by ns2 when the app attached to this agent wants
//to send a message over the link
void XyzzyAgent::sendmsg(int nbytes, AppData* data, const char* flags) {

    if (state_ == STATE_NO_CONN){
        // there is no connection active -- initiate a connection
        if (buddies == NULL){
            evaluateStatus();
        }

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

    // if the heartbeat timer isnt running, start it up...
    if (state_ == STATE_ASSOCIATED){
        for (DestNode* curr = destList; curr != NULL; curr = curr->next){
            if (curr->hb_){
                if (curr->hb_->status() == TIMER_IDLE)
                    curr->hb_->sched(HB_INTERVAL);
                else if (curr->hb_->status() == TIMER_PENDING){
                } else {
                    curr->hb_->resched(HB_INTERVAL);
                }
            } else {
                curr->hb_ = new HeartbeatTimer(this, curr);
                curr->hb_->sched(HB_INTERVAL);
            }
        }
    }

    //create a packet to put data in
    Packet* p;

    //if the maximum packet size is zero...somthing has gone terribly wrong
    if (size_ == 0)
        printf(C_RED "[%d] Xyzzy size_ is 0!\n" C_NORMAL, id_);

    //this will eventually handle fragmentation
    if (data && nbytes > size_) {
        printf("[%d] Xyzzy data does not fit in a single packet. Abort\n", id_);
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
        setupPacket();
        send(p, 0);
    }

    // record the packet in the send buffer
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
    printf("[%d] receiving...\n", id_); 
    //get the header out of the packet we just got
    hdr_Xyzzy* oldHdr = hdr_Xyzzy::access(pkt);
    printf("[%d] seqno: %d is type: %d (state: %d) \n", id_, oldHdr->seqno(), oldHdr->type(), state_);

    // always process buddy state messages
    // if we don't do that here buddies won't 
    // change state, so they wont' process any messages
    if (oldHdr->type() == T_state){
        state_ = hdr_Xyzzy::access(pkt)->seqno();
        if(state_ == STATE_ASSOCIATED){
                rcvHighestReceived = 0; 
                rcvNextExpected = rcvHighestReceived + 1;
        }

    // always process buddy heartbeats
    // because if we don't we can't who is supposed to lead on the 
    // connection before the connection is established...
    } else if(hdr_Xyzzy::access(pkt)->type() == T_beat) {
        printf("[%d] received type T_beat, saddr = %d, seqno = %d\n",id_, hdr_ip::access(pkt)->saddr(), hdr_Xyzzy::access(pkt)->seqno());
        fprintf(buddyLog, "[%d] received type T_beat, saddr = %d, seqno = %d\n",id_, hdr_ip::access(pkt)->saddr(), hdr_Xyzzy::access(pkt)->seqno());

        // this is a request, generate and send the response
        // heartbeat requests are positive, replies are negative
        if(hdr_Xyzzy::access(pkt)->seqno() > 0)
        {
            //find the buddy in question
            DestNode* dest;
            for (buddyNode* current = buddies; current != NULL; current = current->next){
                if (current->dests && current->dests->iNsAddr == hdr_ip::access(pkt)->saddr()){
                    // this is the destination we're looking for
                    // get his destination mark him as reacable if he has been marked
                    // otherwise and set his status to the one contained in the heartbeat
                    dest = current->dests;
                    dest->reachable = true;
                    current->status = hdr_Xyzzy::access(pkt)->status();
                    break;
                }
            }

            //need to check are status to see if the
            //status of our buddy has affected us in anyway
            evaluateStatus();
       
            //send our heartbeat reply
            //multiply the sequence number by -1
            setupPacket(pkt);
            Packet *hbReply = allocpkt();
            hdr_Xyzzy::access(hbReply)->seqno() = hdr_Xyzzy::access(pkt)->seqno() * -1;
            hdr_Xyzzy::access(hbReply)->type() = T_beat;

            //set up the packet to be sent to the appropriate destination
            buddySend(hbReply, dest);

            printf("[%d] sending T_beat reply daddr = %d, seqno = %d\n",id_, hdr_ip::access(hbReply)->daddr(), hdr_Xyzzy::access(hbReply)->seqno());
            fprintf(buddyLog, "[%d] sending T_beat reply daddr = %d, seqno = %d\n",id_, hdr_ip::access(hbReply)->daddr(), hdr_Xyzzy::access(hbReply)->seqno());

            //send heartbeat reply
            send(hbReply, 0);
            
        }
        // this is a response.  Cancel the timeout timer
        // and set the number of missed beats to zero
        else
        {   
            printf("[%d] its a reply...canceling timer...\n", id_);
            fprintf(buddyLog, "[%d] its a reply...canceling timer...\n", id_);
            buddyNode *currentBuddy;
            for(currentBuddy = buddies; currentBuddy != NULL; currentBuddy = currentBuddy->next)
            {
                if(currentBuddy->getDest() && currentBuddy->getDest()->iNsAddr == hdr_ip::access(pkt)->saddr())
                {
                    currentBuddy->missedHBS = 0;
                    if(currentBuddy->hb_->status() == TIMER_PENDING)
                    {
                        currentBuddy->hb_->cancel();
                    }
                    break;
                }
            }
        }
    }

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
                for(buddyNode* current = buddies; current != NULL; current = current->next){
                    addrs[idx] = current->dests->iNsAddr;
                    idx++;
                }
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

                printf(C_WHITE "[%d] Got an init (%d), generating initack (%d)\n" C_NORMAL, id_, initHdr->seqno(), initHdr->cumAck());

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
                isReceiving = true;

                // let our buddies know...
                Packet *sp = allocpkt();
                hdr_Xyzzy::access(sp)->seqno() = state_;
                sendToBuddies(sp, T_state);

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

                    printf(C_WHITE "[%d] Got an initack (%d,%d), generating initack (%d,%d)\n" C_NORMAL, id_, oldHdr->seqno(), oldHdr->cumAck(), initHdr->seqno(), initHdr->cumAck());

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

                    // let our buddies know...
                    Packet *sp = allocpkt();
                    hdr_Xyzzy::access(sp)->seqno() = state_;
                    sendToBuddies(sp, T_state);

                    printf(C_WHITE "[%d] Associated.\n" C_NORMAL, id_);

                // if cumAck is correct, we have established the connection
                } else if (oldHdr->cumAck() == init_){
                    state_ = STATE_ASSOCIATED;

                    // let our buddies know...
                    Packet *sp = allocpkt();
                    hdr_Xyzzy::access(sp)->seqno() = state_;
                    sendToBuddies(sp, T_state);

                    printf(C_WHITE "[%d] Associated.\n" C_NORMAL, id_);
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
         printf("[%d] seqno: %d NE: %d HR %d\n", id_, oldHdr->seqno(), rcvNextExpected, rcvHighestReceived);
        
        // check whether this has been acked already
        bool acked = false;
        if ((ackList && oldHdr->seqno() <= ackList->seqno) || oldHdr->seqno() < rcvNextExpected)
            acked = true;
        else {
            for (ackListNode* curr = ackList; curr != NULL; curr = curr->next){
                if (curr->seqno == oldHdr->seqno()){
                    acked = true;
                    break;
                }
            }
        }
        if (acked == false){
            bool ack = updateRcvWindow(pkt);
            forwardToBuddies(pkt, B_RCVD_MSG);
            isReceiving = true;
            evaluateStatus(T_normal);
            // Send back an ack, so allocate a packet
            if (ack){
                setupPacket(pkt);
                Packet* ap = allocpkt();

                //set up its common header values ie protocol type and size of data
                hdr_cmn::access(ap)->ptype() = PT_XYZZY;
                hdr_cmn::access(ap)->size() = 0;

                //update the list of acked packets
                //and prune any consecutive ones from the front
                //of the list...we don't need them any more
                updateCumAck(oldHdr->seqno());
                if(oldHdr->seqno() > seqno_){
                    seqno_ = oldHdr->seqno();
                }
                ackListPrune();

                //create a new protocol specific header for the packet
                hdr_Xyzzy* newHdr = hdr_Xyzzy::access(ap);

                //set the fields in our new header to the ones that were
                //in the packet we just received
                newHdr->type() = T_ack;
                newHdr->seqno() = oldHdr->seqno();
                newHdr->cumAck() = ackList->seqno;

                if (pkt->userdata())
                    printf(C_CYAN "[%d] Generating ack for seqno %d (cumAck: %d).  Received payload of size %d\n" C_NORMAL, id_, newHdr->seqno(), newHdr->cumAck(), pkt->userdata()->size());
                else
                    printf(C_CYAN "[%d] Generating ack for seqno %d (cumAck: %d)\n" C_NORMAL, id_, newHdr->seqno(), newHdr->cumAck());

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
            }
            //pump a packet from the buffer
            sndPktToApp();
        }

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

            printf(C_PURPLE "[%d] I'm not dead yet! (hb:%d, dest:%d)\n" C_NORMAL, id_, newHdr->heartbeat(), hdr_ip::access(ap)->daddr());

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
        } else {
            DestNode* dest = findDest(hdr_ip::access(pkt)->saddr());
            if (dest){
                printf(C_PURPLE "[%d] 'Ere, he says he's not dead. (hb:%d, dest:%d)\n" C_NORMAL, id_, hdr_Xyzzy::access(pkt)->heartbeat(), dest->iNsAddr);
                if (dest->hbTimeout_ != NULL){
                    if (dest->hbTimeout_->status() == TIMER_PENDING)
                        dest->hbTimeout_->cancel();
                    delete dest->hbTimeout_;
                    dest->hbTimeout_ = NULL;
                }
                dest->reachable = true;
                dest->rcount = 0;
                if (primaryDest->reachable == false){
                    primaryDest = dest;
                    evaluateStatus();
                }
            } else {
                printf(C_PURPLE "[%d] Got a heartbeat from someone I don't know... (hb:%d, dest:%d)\n" C_NORMAL, id_, hdr_Xyzzy::access(pkt)->heartbeat(), hdr_ip::access(pkt)->saddr());
            }
        }
    // process forwarded messages from buddies
    } else if(oldHdr->type() == T_buddy) {
        
        printf("[%d] received type T_buddy, saddr = %d, seqno = %d\n",id_, hdr_ip::access(pkt)->saddr(), hdr_Xyzzy::access(pkt)->seqno());
        fprintf(buddyLog, "[%d] received type T_buddy, saddr = %d, seqno = %d\n",id_, hdr_ip::access(pkt)->saddr(), hdr_Xyzzy::access(pkt)->seqno());
        printf("[%d] seqno: %d is a buddy packet\n", id_, oldHdr->seqno());

        //switch on whether our buddy sent or received the message
        //and whether it was an ack or a message
        switch(oldHdr->sndRcv())
        {
            case B_SENT_MSG:
                //change the message to the appropriate tye and recored it as sent
                printf("[%d] seqno: %d is a sent message\n", id_, oldHdr->seqno());
                oldHdr->type() = T_normal;
                buddyRecordPacket(pkt->copy());
                break;
            case B_SENT_ACK:
                //update our ack list, and make sure we know we 
                //are a receiver
                updateCumAck(hdr_Xyzzy::access(pkt)->seqno());
                if(hdr_Xyzzy::access(pkt)->seqno() > seqno_){
                    seqno_ = hdr_Xyzzy::access(pkt)->seqno();
                }
                isReceiving = true;
                ackListPrune();
                break;
            case B_RCVD_MSG:
                //evaluate our status to make sure we 
                //are not the leading buddy
                evaluateStatus(T_buddy);
                if(oldHdr->seqno() >= rcvNextExpected)
                    updateRcvWindow(pkt);
                break;
            case B_RCVD_ACK:
                updateSndWindow(pkt);
                break;
            default:
                printf(C_RED "[%d] Unknown buddy packet %c\n", id_, hdr_Xyzzy::access(pkt)->sndRcv());
                break;
        }
    }
    //this packet is was sent by the leading buddy to tell us
    //how many messages it just passed to the application
    else if(hdr_Xyzzy::access(pkt)->type() == T_rne) {
        printf("[%d] received type T_rne, saddr = %d, seqno = %d\n",id_, hdr_ip::access(pkt)->saddr(), hdr_Xyzzy::access(pkt)->seqno());
        fprintf(buddyLog, "[%d] received type T_rne, saddr = %d, seqno = %d\n",id_, hdr_ip::access(pkt)->saddr(), hdr_Xyzzy::access(pkt)->seqno());
        buddySndPktToApp( hdr_Xyzzy::access(pkt)->seqno());
        rcvNextExpected = hdr_Xyzzy::access(pkt)->seqno();
    }

    //free the packet we just received
    //BEWARE THAT THIS FUCTION DOES THIS
    Packet::free(pkt);
}

//this function tries to add a received packet to our 
//buffer that accounts for reording of packets, it succeds
//it returns true if the buffer is full and the packet needs to be
//dropped it returns false.
bool XyzzyAgent::updateRcvWindow(Packet *pkt)
{
    // Determine if we have room for this in the buffer
    //
    //Packet == NE > HR ... or anything else;
    //we have room in the buffer;
    printf("[%d] seqno: %d NE: %d HR %d\n", id_, hdr_Xyzzy::access(pkt)->seqno(), rcvNextExpected, rcvHighestReceived);

    //if the packet is the next one we are looking for we know exaclty where it goes
    if(hdr_Xyzzy::access(pkt)->seqno() == rcvNextExpected) {
        int tmp = hdr_Xyzzy::access(pkt)->seqno() % WINDOW_SIZE;
        rcvWindow[tmp] = pkt->copy();

        //if this is the new highes packet put it in there and update
        if(hdr_Xyzzy::access(pkt)->seqno() >  rcvHighestReceived)
            ++rcvHighestReceived ;

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
                     id_, hdr_Xyzzy::access(pkt)->seqno(), hdr_Xyzzy::access(rcvWindow[hdr_Xyzzy::access(pkt)->seqno() % WINDOW_SIZE])->seqno());
            return false;
        }

        //else we already have it and should just ack it again...

        
    //packet > HR > NE
    } else if(rcvHighestReceived > rcvNextExpected && hdr_Xyzzy::access(pkt)->seqno() >  rcvHighestReceived) {
        
        //This should wraparounds right
        if(hdr_Xyzzy::access(pkt)->seqno() >=  rcvNextExpected + WINDOW_SIZE){
            printf(C_RED "[%d] Receive window full (wrap around), dropping packet %d\n" C_NORMAL, id_, hdr_Xyzzy::access(pkt)->seqno());
            return false;
        }

        //make sure we don't clobber any thing in the buffer
        if(rcvWindow[hdr_Xyzzy::access(pkt)->seqno() % WINDOW_SIZE] == NULL){
            rcvWindow[hdr_Xyzzy::access(pkt)->seqno() % WINDOW_SIZE] = pkt->copy();
            rcvHighestReceived = hdr_Xyzzy::access(pkt)->seqno();
        }


        //this one catches a strange corner case 
    } else if(rcvHighestReceived+1 == rcvNextExpected) {
        //catch wrap arounds 
        if(hdr_Xyzzy::access(pkt)->seqno() >=  rcvNextExpected + WINDOW_SIZE){
            
            printf(C_RED "[%d] Receive window full (wrap around), dropping packet %d\n" C_NORMAL, id_, hdr_Xyzzy::access(pkt)->seqno());
            return false;
        }


        if(rcvWindow[hdr_Xyzzy::access(pkt)->seqno() % WINDOW_SIZE] == NULL){
            rcvWindow[hdr_Xyzzy::access(pkt)->seqno() % WINDOW_SIZE] = pkt->copy();
            rcvHighestReceived = hdr_Xyzzy::access(pkt)->seqno();
        }else{

            printf(C_RED "[%d] THE DEMON IS HERE, dropping packet %d\n" C_NORMAL, id_, hdr_Xyzzy::access(pkt)->seqno());
            return false;
        }
    } else {
        printf(C_RED "[%d] Receive window full, dropping packet %d\n" C_NORMAL, id_, hdr_Xyzzy::access(pkt)->seqno());
        return false;
    }
    return true;
}

//updates the send buffer when we get an ack the the packet in the argument
void XyzzyAgent::updateSndWindow(Packet* pkt)
{
    //need to find the packet that was acked in our sndWindow
    for (int i = 0; i < WINDOW_SIZE; ++i){

        //if the packet is a packet in this element...
        if (sndWindow[i] != NULL) {
            hdr_Xyzzy* hdr = hdr_Xyzzy::access(sndWindow[i]);

            //and its sequence number is == to the one packet we just got
            if (hdr->seqno() == hdr_Xyzzy::access(pkt)->seqno()){
                printf(C_CYAN "[%d] Got an ack for %d and marking the packet in the buffer.\n" C_NORMAL, id_, hdr_Xyzzy::access(pkt)->seqno());
                // found it, delete the packet
                Packet::free(sndWindow[i]);
                sndWindow[i] = NULL;

                break;
            //if the packet is less than the cumulative ack that came in
            //on the newest packet we can dump it too while were at it.
            } else if(hdr->seqno() < hdr_Xyzzy::access(pkt)->cumAck()) {
                printf(C_CYAN "[%d] Got a cumAck for %d and marking the packet %d in the buffer.\n" C_NORMAL, id_, hdr_Xyzzy::access(pkt)->cumAck(), hdr->seqno());
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
        printf(C_YELLOW "[%d] No more destination IPs!\n" C_NORMAL, id_);
}

// find a dest in the list by ip address
DestNode* XyzzyAgent::findDest(int addr) {
    for(DestNode* current = destList; current != NULL; current = current->next){
        if (current->iNsAddr == addr)
            return current;
    }
    return NULL;
}

// Send a heartbeat message to the given destination
void XyzzyAgent::heartbeat(DestNode* dest){
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

    printf(C_PURPLE "[%d] Bring out yer dead! (hb:%d, dest:%d)\n" C_NORMAL, id_, seqno_, dest->iNsAddr);
    send(p, 0);
}

// Called if the heartbeat times out
void XyzzyAgent::heartbeatTimeout(void* arg) {
    DestNode* dn = (DestNode*) arg;

    if (dn){
        dn->rcount++;
        printf(C_PURPLE "[%d] No you're not, you'll be stone dead in a moment. (hb:%d, rcount:%d, dest: %d)\n" C_NORMAL, id_, seqno_, dn->rcount, dn->iNsAddr);
        if (dn->rcount > MAX_HB_MISS){
            dn->reachable = false;
            nextDest();
            evaluateStatus();
            printf(C_YELLOW "[%d] New destination: %d\n" C_NORMAL, id_, primaryDest->iNsAddr);

        }
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
    delete dn->hbTimeout_;
    dn->hbTimeout_ = NULL;
}


//This function processes commands to the agent from the TCL script
int XyzzyAgent::command(int argc, const char*const* argv) {

    if (argc == 2 && strcmp(argv[1], "clear") == 0){
        for (int i = 0; i < WINDOW_SIZE; ++i){
            sndWindow[i] = NULL;
            return (TCL_OK);
        }
    } else if (argc == 3 && strcmp(argv[1], "set-multihome-core") == 0) {
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
            printf(C_YELLOW "[%d] Trying to set primary dest when there are no destinations\n" C_NORMAL, id_);
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

        // if we have no buddies yet, add this one
        if (buddies == NULL){
            buddies = new buddyNode;
            buddies->id = opAgent->id_;
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

    printf("[%d] sending buddy heart beats...with seqno: %d\n", id_, seqno_+1);
    fprintf(buddyLog, "[%d] sending buddy heart beats...with seqno: %d\n", id_, seqno_);

    //build heartbeat packet
    Packet* pkt = allocpkt();

    //we give it a sequence number to avoid confusion, mark it as a heart beat, 
    //and then wrap our state up in it so we can let our buddies know if it changed
    hdr_Xyzzy::access(pkt)->seqno() = seqno_+1;
    hdr_Xyzzy::access(pkt)->type() = T_beat;   
    hdr_Xyzzy::access(pkt)->status() = myBuddyStatus;
    
    //loop over buddies
    buddyNode* currentBuddy = buddies;
    while(currentBuddy){

        //get the first active destinatino for the buddy
        DestNode* d = currentBuddy->getDest();

        //if there is now valid destination for the buddy get the first one
        if(d == NULL)
        {
            printf("[%d] no dest for buddy %d, using first one...\n", id_, currentBuddy->id);
            d = currentBuddy->dests;
        }

        //if there is no heartbeat time out time for this buddy create one
        if(currentBuddy->hb_ == NULL)
        {
            currentBuddy->hb_ = new BuddyHeartbeatTimer(this, currentBuddy);
        }

        //if we are not still waiting for a response from this buddy 
        //for a previous heartbeat, we go ahead and schedule the time out timer 
        //and send the packet
        if(currentBuddy->hb_->status() != TIMER_PENDING){
            currentBuddy->hb_->resched(0.2);

            buddySend(pkt, d);

            send(pkt->copy(), 0);
        }

        currentBuddy = currentBuddy->next;
    }

    // schedule another heartbeat 
    if (buddyTimer_.status() == TIMER_IDLE)
        buddyTimer_.sched(0.5);
    else if (buddyTimer_.status() == TIMER_PENDING){
    }else if (buddyTimer_.status() == 2){
        buddyTimer_.resched(0.5);
    } else {
        buddyTimer_.resched(0.5);
    }
}

void XyzzyAgent::forwardToBuddies(Packet* p, char sndRcv){
    //this will loop over the buddies and send the packet I
    //just sent/received to them.  Not sure how the multiple interface
    //thing works so not sure how buddy will know where 
    //it came from /shrug

    printf("[%d] Forwarding To Buddies...\n", id_);
    fprintf(buddyLog, "[%d] Forwarding To Buddies...\n", id_);

    //get a copy of the packet to work on so we don't mess up the original
    Packet *pkt = p->copy();

    //set the packet so the buddy knows if it is a sent_msg, sent_ack,
    //received_msg or received ack, and then mark it as a forwarded 
    //packet from the leading buddy
    hdr_Xyzzy::access(pkt)->sndRcv() = sndRcv;
    hdr_Xyzzy::access(pkt)->type() = T_buddy;

    buddyNode* currentBuddy = buddies;

    while(currentBuddy){

        //needs to grab the active interface for a buddy
        //setup to send the packet and then copy it to the buddy
        
        //skip the buddy if it is dead
        if(currentBuddy->status == B_DEAD){
            printf(C_BLUE "[%d] Skipping buddy %d since it is dead\n" C_NORMAL, id_, currentBuddy->id);
            printf("[%d] Skipping buddy %d since it is dead\n", id_, currentBuddy->id);
            fprintf(buddyLog, "[%d] Skipping buddy %d since it is dead\n", id_, currentBuddy->id);
            currentBuddy = currentBuddy->next;
            continue;
        }

        DestNode* d = currentBuddy->getDest();

        //skip the buddy if it has no valid destinations
        if(!d){
            printf(C_BLUE "[%d] Skipping buddy %d since it has no valid dests\n" C_NORMAL, id_, currentBuddy->id);
            printf("[%d] Skipping buddy %d since it has no valid dests\n", id_, currentBuddy->id);
            fprintf(buddyLog, "[%d] Skipping buddy %d since it has no valid dests\n", id_, currentBuddy->id);
            currentBuddy = currentBuddy->next;
            continue;
        }

        printf(C_BLUE "[%d] Forwarding packet %d to buddy %d\n" C_NORMAL, id_, hdr_Xyzzy::access(p)->seqno(), currentBuddy->id);
        printf("[%d] Forwarding packet %d to buddy %d\n", id_, hdr_Xyzzy::access(p)->seqno(), currentBuddy->id);
        fprintf(buddyLog, "[%d] Forwarding packet %d to buddy %d\n", id_, hdr_Xyzzy::access(p)->seqno(), currentBuddy->id);

        //call buddy send to set up the pkt to go to the specifed
        //destination
        buddySend(pkt, d);

        //send the packet
        send(pkt->copy(), 0);
        
        currentBuddy = currentBuddy->next;
    }

    //free our the fucntion's copy of the packet
    Packet::free(pkt);

}

void XyzzyAgent::sendToBuddies(Packet* p, int type){
    //this will loop over the buddies and send the packet I
    //just sent/received to them.  Not sure how the multiple interface
    //thing works so not sure how buddy will know where 
    //it came from /shrug

    //if were are not the leader we don't need to be sending any messages
    //that are not heartbeats to our buddies
    if(myBuddyStatus != B_LEADING && type != T_beat)
        return;

    printf("[%d] Send To Buddies\n", id_);
    fprintf(buddyLog, "[%d] Send To Buddies\n", id_);
    
    //create a packet to send
    Packet *pkt = p->copy();

    hdr_Xyzzy::access(pkt)->type() = type;

    buddyNode* currentBuddy = buddies;

    while(currentBuddy){

        //needs to grab the active interface for a buddy
        //setup to send the packet and then copy it to the buddy
        
        //if the buddy is dead skip him
        if(currentBuddy->status == B_DEAD){
            printf(C_BLUE "[%d] Skipping buddy %d since it is dead\n" C_NORMAL, id_, currentBuddy->id);
            printf("[%d] Skipping buddy %d since it is dead\n", id_, currentBuddy->id);
            fprintf(buddyLog, "[%d] Skipping buddy %d since it is dead\n", id_, currentBuddy->id);
            currentBuddy = currentBuddy->next;
            continue;
        }

        //get the first reachable destination from the buddy
        DestNode* d = currentBuddy->getDest();

        //if d is null then we should skip it because its dead and just hasn't been caught yet
        if(!d){
            printf(C_BLUE "[%d] Skipping buddy %d since it has no valid dests\n" C_NORMAL, id_, currentBuddy->id);
            printf("[%d] Skipping buddy %d since it has no valid dests\n", id_, currentBuddy->id);
            fprintf(buddyLog, "[%d] Skipping buddy %d since it has no valid dests\n", id_, currentBuddy->id);
            currentBuddy = currentBuddy->next;
            continue;
        }

        printf(C_BLUE "[%d] Sending packet %d to buddy %d\n" C_NORMAL, id_, hdr_Xyzzy::access(p)->seqno(), currentBuddy->id);
        printf("[%d] Sending packet %d to buddy %d\n", id_, hdr_Xyzzy::access(p)->seqno(), currentBuddy->id);
        fprintf(buddyLog, "[%d] Sending packet %d to buddy %d\n", id_, hdr_Xyzzy::access(p)->seqno(), currentBuddy->id);

        //call buddy send to set up this packt to be sent to the specified destination
        buddySend(pkt, d);

        //send a copy of the packt to this buddy
        send(pkt->copy(), 0);
        
        currentBuddy = currentBuddy->next;
    }

    //free our copied packet
    Packet::free(pkt);

}

// Prepare packet pkt to be sent to dest
bool XyzzyAgent::buddySend(Packet* p, DestNode* dest)
{
    printf("[%d]buddySend()\n", id_);
    fprintf(buddyLog, "[%d]buddySend()\n", id_);
    if(p == NULL || dest == NULL)
    {
        printf(C_BLUE "[%d] Buddy packet not sent. p or dest was NULL\n", id_);
        printf("[%d] Buddy packet not sent. p or dest was NULL\n", id_);
        fprintf(buddyLog, "[%d] Buddy packet not sent. p or dest was NULL\n", id_);
        return false;
    }
    
    setupPacket(NULL, dest);
    printf(C_BLUE "[%d] Sending packet %d to buddy at %d\n" C_NORMAL, id_, hdr_Xyzzy::access(p)->seqno(), dest->iNsAddr);
    printf("[%d] Sending packet %d to buddy at %d\n", id_, hdr_Xyzzy::access(p)->seqno(), dest->iNsAddr);
    fprintf(buddyLog, "[%d] Sending packet %d to buddy at %d\n", id_, hdr_Xyzzy::access(p)->seqno(), dest->iNsAddr);
    hdr_ip::access(p)->daddr() = daddr();
    hdr_ip::access(p)->dport() = dport();
    hdr_ip::access(p)->saddr() = addr();
    hdr_ip::access(p)->sport() = port();

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
void XyzzyAgent::buddyMissedBeats(buddyNode* buddy)
{

    //increment the number of heartbeats this buddy has missed
    buddy->missedHBS++;

    printf(C_PURPLE "[%d] buddyMissedBeats(), incrementing for %d, now at %d\n" C_NORMAL, id_, buddy->id, buddy->missedHBS);
    fprintf(buddyLog, "[%d] buddyMissedBeats(), incrementing for %d, now at %d\n", id_, buddy->id, buddy->missedHBS);

    //if it has missed more beats then are allowed we need 
    //to change the interface we are conneceted to it on
    if(buddy->missedHBS > B_FAILED_BEATS){
        //if there are no more reachable destinations
        //we need to set the buddy's status to dead
        //if there is one we need to mark it as not reachable.
        if(buddy->getDest() == NULL)
        {
            buddy->status = B_DEAD;
            evaluateStatus();
        } else {
            buddy->getDest()->reachable = false;
        }
    }
}

// Record a packet in the send buffer that a buddy sent and change the type from T_buddy to T_normal.
// we need a seperate function to do this for buddies because we have to make sure we know where things are
// in the buffer to keep there state (time stamp and retry count) up-to-date.
bool XyzzyAgent::buddyRecordPacket(Packet* pkt)
{
    if(seqno_ < hdr_Xyzzy::access(pkt)->seqno()){
       seqno_ = hdr_Xyzzy::access(pkt)->seqno();
    }
    printf("[%d] buddyRecordPacket()\n", id_);
    fprintf(buddyLog, "[%d] buddyRecordPacket()\n", id_);

    //make sure the send buffer isn't full
    if (sndWindow[sndBufLoc_] != NULL)
    {
        // We can schedule a timeout timer to retry this
        printf(C_BLUE "[%d] No room for pkt: %d, not scheduling rerecord\n", id_, hdr_Xyzzy::access(pkt)->seqno());
        printf("[%d] No room for pkt: %d, not scheduling rerecord\n", id_, hdr_Xyzzy::access(pkt)->seqno());
        fprintf(buddyLog, "[%d] No room for pkt: %d, not scheduling rerecord\n", id_, hdr_Xyzzy::access(pkt)->seqno());
        
        return false;
    } else {
        int i;

        //check to see if a packet with this sequence nubmer already exists in the buffer
        //meaning this is probably a retry
        for(i = 0; i < WINDOW_SIZE; i++)
        {
            if(sndWindow[i] == NULL)
                continue;

            //if it is already in the buffer update the state varibles and return
            if(hdr_Xyzzy::access(sndWindow[i])->seqno() == hdr_Xyzzy::access(pkt)->seqno())
            {        
                sndWindow[i] = pkt;
                numTries[i] = hdr_Xyzzy::access(pkt)->numTries();
                timeSent[i] = hdr_Xyzzy::access(pkt)->timeSent();
                printf(C_GREEN "[%d] packet %d already in buffer\n" C_NORMAL, id_, hdr_Xyzzy::access(pkt)->seqno());
                printf("[%d] packet %d already in buffer\n", id_, hdr_Xyzzy::access(pkt)->seqno());
                fprintf(buddyLog, "[%d] packet %d already in buffer\n", id_, hdr_Xyzzy::access(pkt)->seqno());
                return true;
            }
        }
        
        //this is a new packet that has been sent so store it in
        //sndWindow and set all the relevant varibles
        sndWindow[sndBufLoc_] = pkt;
        numTries[sndBufLoc_] = hdr_Xyzzy::access(pkt)->numTries();
        timeSent[sndBufLoc_] = hdr_Xyzzy::access(pkt)->timeSent();

        //move the buffer location forward one.
        //and move it back around if it has moved
        //past the bounds of the array
        sndBufLoc_++;
        sndBufLoc_ %= WINDOW_SIZE;
        printf(C_GREEN "[%d]  New buffer location: %d for pkt %d\n" C_NORMAL, id_, sndBufLoc_, hdr_Xyzzy::access(pkt)->seqno());
        printf("[%d]  New buffer location: %d for pkt %d\n", id_, sndBufLoc_, hdr_Xyzzy::access(pkt)->seqno());
        fprintf(buddyLog, "[%d]  New buffer location: %d for pkt %d\n", id_, sndBufLoc_, hdr_Xyzzy::access(pkt)->seqno());

        return true;
    }
}

//depricated and not use
void XyzzyAgent::Elect(int electNum)
{
    printf("[%d] Elect(%d)\n",id_, electNum);
    fprintf(buddyLog, "[%d] Elect(%d)\n",id_, electNum);
    Packet *ep = allocpkt();
    hdr_Xyzzy::access(ep)->seqno() = electNum;
    sendToBuddies(ep, T_elect);
    Packet::free(ep);
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
        printf(C_YELLOW "[%d] Adding dest %d to buddy \n" C_NORMAL, id_, iNsAddr);

    } else {
        // set the primary to the last destination added just in case the user does not set a primary
        primaryDest = newDest;

        DestNode* head = destList;

        newDest->next = head;
        destList = newDest;
        printf(C_YELLOW "[%d] Adding dest %d to node\n" C_NORMAL, id_, iNsAddr);
    }
}

//sends a packet in the rcv buffer to the listening app
void XyzzyAgent::sndPktToApp(){

   //first we check to see if the next packet the application needs is currently in the receive buffer
   //if it isn't we return and wait to be called again
    printf(C_GREEN "[%d]Forwarding packet to app...\n" C_NORMAL, id_);
    if(rcvWindow[rcvNextExpected % WINDOW_SIZE] == NULL){
        printf(C_RED "[%d]Still waiting on packet %d\n" C_NORMAL, id_, rcvNextExpected);
        return;
    }
    else{
        //if the next expected packet is in the buffer, we are going to loop around and push 
        //packets up to the app until we find the next hole in the buffer.
        while(rcvWindow[rcvNextExpected % WINDOW_SIZE] != NULL){

            //copy the packet out of the buffer and null out its location
              Packet* pkt = rcvWindow[rcvNextExpected % WINDOW_SIZE];
              rcvWindow[rcvNextExpected % WINDOW_SIZE] = NULL;
                
              //open a log file and print out the packet data for debugging etc
            printf(C_GREEN "[%d] Packet %d forwarded\n" C_NORMAL, id_, rcvNextExpected);
              char fname[13];  
              snprintf(fname, 12, "%d-pssd.log", id_);
              FILE* lf = fopen(fname, "a");
                
              if (pkt->userdata()){  
                  PacketData* data = (PacketData*)pkt->userdata();
                  char packetid[255];
                  snprintf(packetid, 254, "Packet %d\n", hdr_Xyzzy::access(pkt)->seqno());
                  fwrite(packetid, strlen(packetid), 1, lf);
                  fwrite(data->data(), data->size(), 1, lf);
              } else
                  fwrite("NULL Data ", 9, 1, lf);
              fwrite("\n\n\n", 2, 1, lf);
              fclose(lf);

              //pass it to an app if there is one
              
            if (app_) {
                printf(C_GREEN "[%d] Packet %d sent to app\n" C_NORMAL, id_, rcvNextExpected);
                app_->process_data(hdr_cmn::access(pkt)->size(), pkt->userdata());
            }

            //free the packet we copied
            Packet::free(pkt);

            //iterate the next expected packet
            ++rcvNextExpected;
        }
        
        //after we have sent all the contigous packets in the buffer
        //we let are buddies know how many we sent to the app by sending 
        //it the new value of our NextExpected varible.
        Packet *sp = allocpkt();
        hdr_Xyzzy::access(sp)->seqno() = rcvNextExpected;
        sendToBuddies(sp, T_rne);
    }
}

//this function is pretty much the same as the other
//send packet to app funcition except some of the conditionals
//are different
void XyzzyAgent::buddySndPktToApp(int next){

    //we want to loop and send to our instance of the appllication all the packets 
    //our buddy sent to its applciction, next represents our buddies new rne varible
    for(int i = rcvNextExpected; rcvNextExpected <  next; ++rcvNextExpected){

        //If any of the values in this stretch of the buffer are missing, it means that 
        //we missed a packet that our buddy sent to its application and the state is now inconsistant.
        //If this happens somthing is really wrong but it pays to be careful
        if(rcvWindow[rcvNextExpected % WINDOW_SIZE] == NULL){
            //state is no longer consistentant... don't know what to do...
        printf(C_RED "[%d] CHOOSE THE FORM OF THE DESTRUCTOR!!! Packet %d never made it to buddy...we are inconsistant\n" C_NORMAL, id_, rcvNextExpected);
            continue;
        }

        //copy the packet from the buffer and null out the space it was in
          Packet* pkt = rcvWindow[rcvNextExpected % WINDOW_SIZE];
          rcvWindow[rcvNextExpected % WINDOW_SIZE] = NULL;
            
          //open a file to log the order in which we send packets to our application.
        printf(C_GREEN "[%d] Packet %d forwarded\n" C_NORMAL, id_, rcvNextExpected);
          char fname[13];  
          snprintf(fname, 12, "%d-pssd.log", id_);
          FILE* lf = fopen(fname, "a");
            
          if (pkt->userdata()){  
              PacketData* data = (PacketData*)pkt->userdata();
              char packetid[255];
              snprintf(packetid, 254, "Packet %d\n", hdr_Xyzzy::access(pkt)->seqno());
              fwrite(packetid, strlen(packetid), 1, lf);
              fwrite(data->data(), data->size(), 1, lf);
          } else
              fwrite("NULL Data ", 9, 1, lf);
          fwrite("\n\n\n", 2, 1, lf);
          fclose(lf);

          //pass it to an app if there is one
          
        if (app_) {
            app_->process_data(hdr_cmn::access(pkt)->size(), pkt->userdata());
        }

        //free our copied packet
        Packet::free(pkt);

    }
}

//this function is called anytime our agent instance receives information 
//that could mean that its status amoungst the buddies may have changed ie
//it lost a link, or the leading buddy failed to receive a heartbeat.
//
//if our status remains the same, we do nothing, if it has changed we send 
//out heartbeats to let our buddies know what our status is now
void XyzzyAgent::evaluateStatus(int msg){

    //we first need to check and see if this buddy is a sender or receive
    //becuase their state changes are cued by different events
    if(isReceiving){

        //if we have received normal message and are not the leading host
        //in our buddy group, then that means we now our because somthing 
        //has changed on the receiving side.
        if(msg == T_normal  && myBuddyStatus != B_LEADING){
            myBuddyStatus = B_LEADING;
            sendBuddyHeartBeats();
            printf("[%d] my state is now: %d\n", id_, myBuddyStatus);
            fprintf(buddyLog, "[%d] my state is now %d\n", id_, myBuddyStatus);

        //if the message is a buddy message and we are the leader, it means our 
        //buddy received an actual msg from the send meaning my state should change
        //as the packets are now going there.
        }else if (msg == T_buddy && myBuddyStatus == B_LEADING){

            myBuddyStatus = B_ACTIVE;
            sendBuddyHeartBeats();
            printf("[%d] my state is now: %d\n", id_, myBuddyStatus);
            fprintf(buddyLog, "[%d] my state is now %d\n", id_, myBuddyStatus);
        }

    //this is the state changes for senders.
    } else{
        bool sendBeats = false;
        
        //check and see if I have any links to the receiving 
        //side, if not then my state needs to change to active, 
        //no connectioin if its not there already.
        if(!primaryDest->reachable){
            if(myBuddyStatus != B_ACTIVE_NO_CONN){
                myBuddyStatus = B_ACTIVE_NO_CONN;
                sendBeats = true;
                printf("[%d] my state is now: %d\n", id_, myBuddyStatus);
                fprintf(buddyLog, "[%d] my state is now %d\n", id_, myBuddyStatus);
                
                printf("[%d] Telling app to stop sending me data.\n", id_);

                //since I am no longer an active buddy the app needs to stop sending me things.
                if (app_)
                    ((XyzzyApp*)app_)->stop();
            }
            return;

        //if my state has been active, no connection, and now one of my links has 
        //come up, I need to change to the active state so my buddies know I am available 
        //to take control of the stream
        }else if(primaryDest->reachable && myBuddyStatus == B_ACTIVE_NO_CONN){
            myBuddyStatus = B_ACTIVE;
            sendBeats = true;
            printf("[%d] my state is now: %d\n", id_, myBuddyStatus);
            fprintf(buddyLog, "[%d] my state is now %d\n", id_, myBuddyStatus);
        }

        bool another = false;

        buddyNode* currentBuddy = buddies;

        //search through my list of buddies to see if there are any live buddies, with connections
        //to the other side, if its true then they should be the one leading the stream.
        while(currentBuddy){

            if(currentBuddy->id < id_ && !(currentBuddy->status == B_DEAD || currentBuddy->status == B_ACTIVE_NO_CONN))
                    another = true;

            currentBuddy = currentBuddy->next;
        }

        //if there is another who should be in charge I am going to set 
        //my own status just to active if I am not there already
        if(another){
            if(myBuddyStatus != B_ACTIVE){    
                myBuddyStatus = B_ACTIVE;
                sendBeats = true;
                printf("[%d] my state is now: %d\n", id_, myBuddyStatus);
                fprintf(buddyLog, "[%d] my state is now %d\n", id_, myBuddyStatus);
                printf("[%d] Telling app to stop sending me data.\n", id_);
                if (app_)
                    ((XyzzyApp*)app_)->stop();
            }

        //if there is nobody more eleagible to lead the transmission then this node
        //should set its status to leading and take over the connection
        }else if (myBuddyStatus != B_LEADING && myBuddyStatus != B_ACTIVE_NO_CONN ){
            if(myBuddyStatus != B_LEADING){
                myBuddyStatus = B_LEADING;
                printf("[%d] my state is now: %d\n", id_, myBuddyStatus);
                fprintf(buddyLog, "[%d] my state is now %d\n", id_, myBuddyStatus);

                sendBuddyHeartBeats();
                sendBeats = false;

                printf("[%d] Telling app to start sending me data (from: %d).\n", id_, seqno_);
                if (app_ && seqno_ > 0){
                    ((XyzzyApp*)app_)->setOffset(seqno_);
                    ((XyzzyApp*)app_)->start();
                }
            }
        }
        //if we entered a new state we want to send
        //our heartbeats which contain our satus so 
        //all of our buddies will know what state we're 
        //in now.
        if (sendBeats)
            sendBuddyHeartBeats();
    }
}
/* vi: set tabstop=4 softtabstop=4 shiftwidth=4 expandtab : */
