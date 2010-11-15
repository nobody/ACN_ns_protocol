#include "timer-handler.h"
#include "packet.h"
#include "app.h"

class testData;

class SendTimer : public TimerHandler {
	public:
	SendTimer(MmApp* t) : TimerHandler(), t_(t) {}
	inline virtual void expire(Event*);
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

	private:
	double interval_;      // Application data packet transmission interval
	int random_;           // If 1 add randomness to the interval
	int running_;          // If 1 application is running
	int rate_;
	char[] test_;
	int msgSize_;
        SendTimer snd_timer_;  // SendTimer

};

