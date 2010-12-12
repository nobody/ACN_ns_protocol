#ifndef ns_XyzzyApp_h
#define ns_XyzzyApp_h

#include "app.h"

class XyzzyApp : public Application {
public:
	XyzzyApp(Agent *xyzzy);
	~XyzzyApp();

	virtual void recv(int nbytes);
	virtual void send(int nbytes, AppData *data);

	virtual void process_data(int size, AppData* data);

	virtual void resume();

	virtual void start(); 
	virtual void stop();

	virtual void setOffset(int num);
protected:
	virtual int command(int argc, const char*const* argv);

};

#endif // ns_xyzzyapp_h
