#include "FasttunBase.h"
#include "Listener.h"
#include "Connection.h"

using namespace tun;

//--------------------------------------------------------------------------
class Proxy : public Listener::Handler
{
  public:
    Proxy(EventPoller *poller)
			:mEventPoller(poller)
			,mListener(poller)
			,mConns()
	{
	}
	
    virtual ~Proxy()
	{
	}

	bool create()
	{
		if (!mListener.create("127.0.0.1", 5080))
		{
			logErrorLn("create listener failed.");
			return false;
		}
		mListener.setEventHandler(this);

		return true;
	}

	void finalise()
	{		
		mListener.finalise();
		ConnMap::iterator it = mConns.begin();
		for (; it != mConns.end(); ++it)
		{
			Connection *pConn = it->second;
			if (pConn)
				pConn->shutdown();
		}
		mConns.clear();
	}

	virtual void onAccept(int connfd)
	{
		Connection *pConn = new Connection(mEventPoller);
		if (!pConn->acceptConnection(connfd))
		{
			delete pConn;
			return;
		}

		mConns.insert(std::pair<int, Connection *>(connfd, pConn));
	}
  private:
	typedef std::map<int, Connection *> ConnMap;
	
	EventPoller *mEventPoller;
	Listener mListener;

	ConnMap mConns;
};
//--------------------------------------------------------------------------

int main(int argc, char *argv[])
{
	// initilise tracer
	core::createTrace();
	core::output2Console();
	core::output2Html("fasttun_client.html");

	// create event poller
	EventPoller *netPoller = EventPoller::create();

	// create proxy
	Proxy proxy(netPoller);
	if (!proxy.create())
	{
		logErrorLn("create proxy error!");
		delete netPoller;
		core::closeTrace();
		exit(1);
	}

	for (;;)
	{
		netPoller->processPendingEvents(0.016);
	}

	proxy.finalise();	
	delete netPoller;

	// close tracer
	core::closeTrace();
	exit(0);
}
