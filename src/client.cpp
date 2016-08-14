#include "FasttunBase.h"
#include "Listener.h"
#include "Connection.h"

using namespace tun;

//--------------------------------------------------------------------------
class Proxy : public Listener::Handler, public Connection::Handler
{
  public:
    Proxy(EventPoller *poller)
			:Connection::Handler()
			,mEventPoller(poller)
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

		pConn->setEventHandler(this);
		mConns.insert(std::pair<int, Connection *>(connfd, pConn));
	}

	virtual void onDisconnected(Connection *pConn)
	{
		char strSrc[1024] = {0};
		if (getPeerAddrInfo(pConn, strSrc, sizeof(strSrc)))
		{
			logWarningLn("from "<<strSrc<<"!  connection disconnected!"
						 <<"  we now have "<<mConns.size()-1<<" connections!");
		}		
		
		onConnShut(pConn);
	}

	virtual void onRecv(Connection *pConn, const void *data, size_t datelen)
	{
	}
	
	virtual void onError(Connection *pConn)
	{
		char strSrc[1024] = {0};
		if (getPeerAddrInfo(pConn, strSrc, sizeof(strSrc)))
		{
			logWarningLn("from "<<strSrc<<"!  got error on connection!"
						 <<"  we now have "<<mConns.size()-1<<" connections!");
		}
		
		onConnShut(pConn);
	}
	
  private:
	void onConnShut(Connection *pConn)
	{		
		ConnMap::iterator it = mConns.find(pConn->getSockFd());
		if (it != mConns.end())
		{			
			mConns.erase(it);
		}

		pConn->shutdown();
		delete pConn;
	}

	char* getPeerAddrInfo(Connection *pConn, char *info, int len)
	{
		char peerIp[MAX_BUF] = {0};
		if (pConn->getPeerIp(peerIp, MAX_BUF))
		{
			snprintf(info, len, "%s:%d", peerIp, pConn->getPeerPort());
			return info;
		}
		return NULL;
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
