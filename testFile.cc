#include "testFile.h"
#include <iostream>
#include <fstream>

static class testFileClass : public TclClass {
	public:
   	testFileClass() : TclClass("Application/testFile") {}
	TclObject* create(int, const char*const*) {
	    return (new testFile);
	}
} class_app_testFile;

void SendFileTimer::expire(Event*){
	t_->send_data();
}

//constructor go here
testFile::testFile(): interval_(0.05), snd_timer_(this){
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
int testFile::command(int argc, const char*const* argv)
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

void testFile::start(){
	running_ = 1;
	send_data();
}

void testFile::stop(){
	running_ = 0;
}

void testFile::processData(int size, AppData* data){

}

void testFile::send_data(){

    //create a file and read in the alice in wonderland text file
    ifstream myFile;
    myFile.open( "alice.txt", ios::in );    

	if(running_){
        counter_++;

        char* str = new char[msgSize_];

        //char to hold the 1000 bytes of the file
        PacketData* data = new PacketData(1000);

        myFile.read( (char*)data->data(), 1000);


        snprintf(str, msgSize_, "this is only a test: %d", counter_);
        memcpy(data->data(), str, msgSize_);
		agent_->sendmsg(msgSize_, data);	
	
		//reschedule the event
		snd_timer_.resched(interval_);
	}
}

/* vi: set tabstop=4 softtabstop=4 shiftwidth=4 expandtab : */
