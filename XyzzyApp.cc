#include "XYZZY/XyzzyApp.h"

// XyzzyApp
static class XyzzyAppClass : public TclClass {
public:
	XyzzyAppClass() : TclClass("Application/XyzzyApp") {}
	TclObject* create(int argc, const char*const* argv) {
		if (argc != 5)
			return NULL;
		Agent *xyzzy = (Agent *)TclObject::lookup(argv[4]);
		if (xyzzy == NULL) 
			return NULL;
		return (new XyzzyApp(xyzzy));
	}
} class_xyzzy_app;


XyzzyApp::XyzzyApp(Agent* xyzzy) {
    agent_ = xyzzy;
    agent_->attachApp(this);
}

XyzzyApp::~XyzzyApp(){
    agent_->attachApp(NULL);
}

void XyzzyApp::recv(int nbytes){}
void XyzzyApp::send(int nbytes, AppData* data){}
void XyzzyApp::process_data(int size, AppData* data) {}
void XyzzyApp::resume() {}
void XyzzyApp::start() {}
void XyzzyApp::stop() {}
void XyzzyApp::setOffset(int num) {}
int XyzzyApp::command(int argc, const char*const* argv) {
     return Application::command(argc, argv);
}
