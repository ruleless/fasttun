#include "FasttunBase.h"
#include "Listener.h"
#include "Connection.h"
#include "KcpTunnel.h"
#include "FastConnection.h"
#include "Cache.h"

using namespace tun;

static KcpTunnelGroup *gTunnelManager = NULL;

//--------------------------------------------------------------------------
class ServerBridge : public Connection::Handler, public FastConnection::Handler
{
  public:
	struct Handler
	{
		virtual void onExtConnDisconnected(ServerBridge *pBridge) = 0;
		virtual void onExtConnError(ServerBridge *pBridge) = 0;
	};
	
    ServerBridge(EventPoller *poller, Handler *h)
			:mEventPoller(poller)
			,mpHandler(h)
			,mpIntConn(NULL)
			,mpExtConn(NULL)
			,mCache(NULL)
			,mLastExtConnTime(0)
	{
		mCache = new MyCache(this);
	}
	
    virtual ~ServerBridge()
	{
		delete mCache;
	}

	bool acceptConnection(int connfd)
	{
		assert(NULL == mpIntConn && NULL == mpExtConn &&
			   "NULL == mpIntConn && NULL == mpExtConn");

		mpExtConn = new FastConnection(mEventPoller, gTunnelManager);
		if (!mpExtConn->acceptConnection(connfd))
		{
			delete mpExtConn;
			mpExtConn = NULL;
			return false;
		}
		mpExtConn->setEventHandler(this);

		mLastExtConnTime = getTickCount();
		mpIntConn = new Connection(mEventPoller);
		if (!mpIntConn->connect("127.0.0.1", 5082))
		{
			mpExtConn->shutdown();
			delete mpExtConn;
			mpExtConn = NULL;
			delete mpIntConn;
			mpIntConn = NULL;
			return false;
		}
		mpIntConn->setEventHandler(this);

		return true;
	}

	void shutdown()
	{
		if (mpExtConn)
		{
			mpExtConn->shutdown();
			delete mpExtConn;
			mpExtConn = NULL;
		}
		if (mpIntConn)
		{
			mpIntConn->shutdown();
			delete mpIntConn;
			mpIntConn = NULL;
		}
	}

	int getExtSockFd() const
	{
		if (mpExtConn)
			return mpExtConn->getSockFd();
		return -1;
	}

	FastConnection* getExtConn() const
	{
		return mpExtConn;
	}

	// Connection::Handler
	virtual void onConnected(Connection *pConn)
	{
		logTraceLn("connected with ss!");
		_flushAll();
	}
	virtual void onDisconnected(Connection *pConn)
	{
		logTraceLn("disconnected with ss!");
		_reconnectInternal();
	}

	virtual void onRecv(Connection *pConn, const void *data, size_t datalen)
	{
		logTraceLn("serverint onRecv len="<<datalen);
		mpExtConn->send(data, datalen);
	}
	
	virtual void onError(Connection *pConn)
	{
		_reconnectInternal();
	}

	// FastConnection::Handler
	virtual void onDisconnected(FastConnection *pConn)
	{
		if (mpHandler)
			mpHandler->onExtConnDisconnected(this);
	}
	virtual void onError(FastConnection *pConn)
	{
		if (mpHandler)
			mpHandler->onExtConnError(this);
	}
		
	virtual void onCreateKcpTunnelFailed(FastConnection *pConn)
	{
		if (mpHandler)
			mpHandler->onExtConnError(this);
	}

	virtual void onRecv(FastConnection *pConn, const void *data, size_t datalen)
	{
		logTraceLn("severext onRecv len="<<datalen);		
		if (!mpIntConn->isConnected())
		{
			logErrorLn("recv from client! but we have no connection with ss!");
			_reconnectInternal();
			mCache->cache(data, datalen);
		}
		else
		{
			_flushAll();
			mpIntConn->send(data, datalen);
		}
	}

	void _reconnectInternal()
	{
		ulong curtick = getTickCount();
		if (curtick > mLastExtConnTime+1000)
		{
			mLastExtConnTime = curtick;			
			mpIntConn->connect("127.0.0.1", 5082);
			logTraceLn("reconnect ss!");
		}		
	}

	void _flushAll()
	{
		if (mpIntConn->isConnected())
		{
			mCache->flushAll();
		}
	}
	void flush(const void *data, size_t len)
	{
		mpIntConn->send(data, len);
	}
	
  private:	
	typedef Cache<ServerBridge> MyCache;
	
	EventPoller *mEventPoller;
	Handler *mpHandler;

	Connection *mpIntConn;
	FastConnection *mpExtConn;

	MyCache *mCache;

	ulong mLastExtConnTime;
};
//--------------------------------------------------------------------------

//--------------------------------------------------------------------------
class Server : public Listener::Handler, public ServerBridge::Handler
{
  public:
    Server(EventPoller *poller)
			:Listener::Handler()
			,mEventPoller(poller)
			,mListener(poller)
			,mConns()
	{
	}
	
    virtual ~Server()
	{
	}

	bool create()
	{
		if (!mListener.create("127.0.0.1", 29900))
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
			ServerBridge *bridge = it->second;
			if (bridge)
			{
				bridge->shutdown();
				delete bridge;
			}
		}
		mConns.clear();
	}

	virtual void onAccept(int connfd)
	{
		ServerBridge *bridge = new ServerBridge(mEventPoller, this);
		if (!bridge->acceptConnection(connfd))
		{
			delete bridge;
			return;
		}

		mConns.insert(std::pair<int, ServerBridge *>(connfd, bridge));
	}

	virtual void onExtConnDisconnected(ServerBridge *pBridge)
	{
		char strSrc[1024] = {0};
		if (getPeerAddrInfo(pBridge, strSrc, sizeof(strSrc)))
		{
			logWarningLn("from "<<strSrc<<"! connection disconnected!"
						 <<" we now have "<<mConns.size()-1<<" connections!");
		}		
		
		onBridgeShut(pBridge);
	}
	
	virtual void onExtConnError(ServerBridge *pBridge)
	{
		char strSrc[1024] = {0};
		if (getPeerAddrInfo(pBridge, strSrc, sizeof(strSrc)))
		{
			logWarningLn("from "<<strSrc<<"! got error on connection!"
						 <<" we now have "<<mConns.size()-1<<" connections!");
		}

		onBridgeShut(pBridge);
	}	   	
	
  private:
	void onBridgeShut(ServerBridge *pBridge)
	{		
		ConnMap::iterator it = mConns.find(pBridge->getExtSockFd());
		if (it != mConns.end())
		{			
			mConns.erase(it);
		}

		pBridge->shutdown();
		delete pBridge;
	}

	char* getPeerAddrInfo(ServerBridge *pBridge, char *info, int len)
	{
		char peerIp[MAX_BUF] = {0};
		Connection *pExtConn = pBridge->getExtConn() ? pBridge->getExtConn()->getConnection() : NULL;
		if (pExtConn && pExtConn->getPeerIp(peerIp, MAX_BUF))
		{
			snprintf(info, len, "%s:%d", peerIp, pExtConn->getPeerPort());
			return info;
		}
		return NULL;
	}	
  private:
	typedef std::map<int, ServerBridge *> ConnMap;
	
	EventPoller *mEventPoller;
	Listener mListener;

	ConnMap mConns;
};
//--------------------------------------------------------------------------

int main(int argc, char *argv[])
{
	// initialise tracer
	core::createTrace();
	core::output2Console();
	core::output2Html("fasttun_server.html");

	// create event poller
	EventPoller *netPoller = EventPoller::create();

	// create server
	Server svr(netPoller);
	if (!svr.create())
	{
		logErrorLn("create server error!");
		delete netPoller;
		core::closeTrace();
		exit(1);
	}	

	gTunnelManager = new KcpTunnelGroup(netPoller);
	if (!gTunnelManager->create("0.0.0.0:29901"))
	{
		logErrorLn("initialise Tunnel Manager error!");
		delete netPoller;
		core::closeTrace();
		exit(1);
	}	

	for (;;)
	{
		netPoller->processPendingEvents(0.016);
		gTunnelManager->update();
	}
	
	gTunnelManager->shutdown();
	delete gTunnelManager;
	svr.finalise();
	delete netPoller;

	// close tracer
	core::closeTrace();
	exit(0);
}
