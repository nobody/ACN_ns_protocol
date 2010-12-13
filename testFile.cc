#include "testFile.h"
#include <iostream>
#include <fstream>
#include <stdlib.h>
#include <string.h>

static class testFileClass : public TclClass {
public:
	testFileClass() : TclClass("Application/testFile") {}
	TclObject* create(int argc, const char*const* argv) {
		if (argc != 5)
			return NULL;
		Agent *xyzzy = (Agent *)TclObject::lookup(argv[4]);
		if (xyzzy == NULL) 
			return NULL;
		return (new testFile(xyzzy));
	}
} class_app_testFile;

void SendFileTimer::expire(Event*){
	t_->send_data();
}

//constructor go here
testFile::testFile(Agent* xyzzy): XyzzyApp(xyzzy), interval_(0.05), snd_timer_(this), numReads(0){
	//will declaring this array work here?
	//or does new need to be used? i don't want to do that 
	//if its not nesscary
//	test_ = "this is only a test";
	msgSize_ = 28;
    agent_ = xyzzy;
    agent_->attachApp(this);

    fileInputName = "alice.txt";
    fileOutputName = "alice-out.txt";

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

        if (strcmp(argv[1], "set-input-file-name") == 0) {
            fileInputName = (char*) malloc(sizeof(char) * (strlen(argv[2]) + 1));
            strcpy(fileInputName, argv[2]);
            return (TCL_OK);
        }

        if (strcmp(argv[1], "set-output-file-name") == 0) {
            fileOutputName = (char*) malloc(sizeof(char) * (strlen(argv[2]) + 1));
            strcpy(fileOutputName, argv[2]);
            return (TCL_OK);
        }
	}

	//if not bonding an agent let the super deal with it
	return (Application::command(argc, argv));
}

void testFile::start(){
	running_ = 1;
	send_data();
    printf("Application starting to send data. Offset: %d\n", numReads);
}

void testFile::stop(){
	running_ = 0;
}

void testFile::process_data(int size, AppData* data){
    //cast as packet data
    PacketData* pData = (PacketData*)data;

    //open the file
    ofstream myFile;
    //myFile.open( "alice out.txt", ios::out | ios::app );
    myFile.open( fileOutputName, ios::out |  ios::app );

    //write the packet data to the file
    myFile.write((char*) pData->data(), size);

    //close the file
    myFile.close();
}

void testFile::setOffset(int num){
    //to set the offset in the file
    numReads = num * 1000;
}

void testFile::send_data(){

    //create a file and read in the alice in wonderland text file
    ifstream myFile;
    myFile.open( fileInputName, ios::in );
    // myFile.open( "alice.txt", ios::in );    

	if(running_){
        counter_++;


         //char to hold the 1000 bytes of the file
        PacketData* data = new PacketData(1000);

        //seek to the right part of the file
        myFile.seekg( numReads, ios::beg );

        //read 1000 bytes in the file
        myFile.read( (char*)data->data(), 1000);

        //get the read count
        int count = myFile.gcount();
        
        //increment the offset
        numReads += count;

        //close the file
        myFile.close();

        printf("Application sending packet. Offset: %d, size: %d\n", numReads, count);

        //sends the message over the network
		agent_->sendmsg(count, data);	
	
		//reschedule the event
        if(!myFile.eof())
		    snd_timer_.resched(interval_);
	}
}

/* vi: set tabstop=4 softtabstop=4 shiftwidth=4 expandtab : */
