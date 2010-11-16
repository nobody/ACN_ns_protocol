#include "testData.h"

static class testDataClass : public TclClass {
	public:
   	testDataClass() : TclClass("Application/testData") {}
	TclObject* create(int, const char*const*) {
	    return (new testData);
	}
} class_app_testData;

void SendTimer::expire(Event*){
	t_->send_data();
}

//constructor go here
testData::testData(): snd_timer_(this), interval_(0.05){
	//will declaring this array work here?
	//or does new need to be used? i don't want to do that 
	//if its not nesscary
//	test_ = "this is only a test";
	msgSize_ = 28;

	//not sure how the bind works but, interval_ should at least be 
	//bound here.
    bind("interval_", &interval_);
}

//otcl command
int testData::command(int argc, const char*const* argv)
{
	Tcl& tcl = Tcl::instance();
	if (argc == 3) {
		if (strcmp(argv[1], "attach-agent") == 0) {
		    agent_ = (Agent*) TclObject::lookup(argv[2]);
		    if (agent_ == 0) {
			tcl.resultf("no such agent %s", argv[2]);
			return(TCL_ERROR);
	      	    }
		    agent_->attachApp(this);
		    return(TCL_OK);
		}
	}

	//if not bonding an agent let the super deal with it
	return (Application::command(argc, argv));
}

void testData::start(){
	running_ = 1;
	send_data();
}

void testData::stop(){
	running_ = 0;
}

void testData::send_data(){

	if(running_){
        counter_++;

        char* str = new char[msgSize_];

        PacketData* data = new PacketData(msgSize_);
        snprintf(str, msgSize_, "this is only a test: %d", counter_);
        memcpy(data->data(), str, msgSize_);
		agent_->sendmsg(msgSize_, data);	
	
		//reschedule the event
		snd_timer_.resched(interval_);
	}
}

