#ifndef ns2_testData_
#define ns2_testData_

// Add "Application/testData set interval_ 0.05" to tcl/lib/ns-default.tcl
#include "timer-handler.h"
#include "packet.h"
#include "app.h"

class testData;

class SendTimer : public TimerHandler {
	public:
	SendTimer(testData* t) : TimerHandler(), t_(t) {}
	virtual void expire(Event*);
	protected:
	testData* t_;
};
class testData: public Application {
	public:
	testData();
	void send_data();  // called by SendTimer:expire (Sender)
	protected:
	int command(int argc, const char*const* argv);
	void start();       // Start sending data packets (Sender)
	void stop();        // Stop sending data packets (Sender)
	double interval_;      // Application data packet transmission interval

	private:
	int random_;           // If 1 add randomness to the interval
    unsigned short counter_;
	int running_;          // If 1 application is running
	int rate_;
	char* test_;
	int msgSize_;
        SendTimer snd_timer_;  // SendTimer

};

#endif
