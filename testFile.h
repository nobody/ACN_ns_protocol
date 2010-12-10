#ifndef ns2_testFile_
#define ns2_testFile_

// Add "Application/testFile set interval_ 0.05" to tcl/lib/ns-default.tcl
#include "timer-handler.h"
#include "packet.h"
#include "app.h"

class testFile;

class SendFileTimer : public TimerHandler {
	public:
	SendFileTimer(testFile* t) : TimerHandler(), t_(t) {}
	virtual void expire(Event*);
	protected:
	testFile* t_;
};
class testFile: public Application {
	public:
	testFile();
	void send_data();  // called by SendTimer:expire (Sender)
	protected:
	int command(int argc, const char*const* argv);
	void start();       // Start sending data packets (Sender)
	void stop();        // Stop sending data packets (Sender)
	virtual void processData(int size, AppData* data); //this is what is called when the application is recieving data
	double interval_;      // Application data packet transmission interval
	int numReads;	// variable used to store the number of reads done from the file to find current location

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
