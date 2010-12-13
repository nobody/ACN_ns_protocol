#ifndef ns2_testFile_
#define ns2_testFile_

// Add "Application/testFile set interval_ 0.05" to tcl/lib/ns-default.tcl
#include "timer-handler.h"
#include "packet.h"
//#include "app.h"
#include "XYZZY/XyzzyApp.h"

class testFile;
class XyzzyAgent;

class SendFileTimer : public TimerHandler {
	public:
	SendFileTimer(testFile* t) : TimerHandler(), t_(t) {}
	virtual void expire(Event*);
	protected:
	testFile* t_;
};
class testFile: public XyzzyApp {
	public:
	testFile(Agent* xyzzy);
	void send_data();  // called by SendTimer:expire (Sender)
	virtual void setOffset(int num);	// to set the offset in the file
	protected:
	virtual int command(int argc, const char*const* argv);
	void start();       // Start sending data packets (Sender)
	void stop();        // Stop sending data packets (Sender)
	virtual void process_data(int size, AppData* data); //this is what is called when the application is recieving data
	double interval_;      // Application data packet transmission interval
	int numReads;	// variable used to store the number of reads done from the file to find current location
	char* fileOutputName;		//variable used to store the name of the file for the output
	char* fileInputName;		//variable used to store the name of the file for the input

	private:
	int random_;           // If 1 add randomness to the interval
    unsigned short counter_;
	int running_;          // If 1 application is running
	int rate_;
	char* test_;
	int msgSize_;
        SendFileTimer snd_timer_;  // SendTimer

};

#endif
