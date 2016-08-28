#include "FasttunBase.h"
#include "Listener.h"
#include "Connection.h"
#include "KcpTunnel.h"
#include "FastConnection.h"
#include "Cache.h"

using namespace tun;

static sockaddr_in ListenAddr;
static sockaddr_in RemoteAddr;

//--------------------------------------------------------------------------
class ClientBridge : public Connection::Handler
{
  public:
	struct Handler
	{
		virtual void onIntConnDisconnected(ClientBridge *pBridge) = 0;
		virtual void onIntConnError(ClientBridge *pBridge) = 0;
	};
	
    ClientBridge(EventPoller *poller, Handler *l)
			:mEventPoller(poller)
			,mpHandler(l)
			,mpIntConn(NULL)
			,mpExtConn(NULL)
			,mCache(NULL)
			,mLastExtConnTime(0)
	{
		mCache = new MyCache(this);
	}
	
    virtual ~ClientBridge()
	{		
		shutdown();
		delete mCache;
	}

	bool acceptConnection(int connfd)
	{
		assert(NULL == mpIntConn && NULL == mpExtConn &&
			   "NULL == mpIntConn && NULL == mpExtConn");

		mpIntConn = new Connection(mEventPoller);
		if (!mpIntConn->acceptConnection(connfd))
		{
			delete mpIntConn;
			mpIntConn = NULL;
			return false;
		}
		mpIntConn->setEventHandler(this);

		mpExtConn = new Connection(mEventPoller);
		mLastExtConnTime = core::coreClock();
		if (!mpExtConn->connect((const SA *)&RemoteAddr, sizeof(RemoteAddr)))
		{
			mpIntConn->shutdown();
			delete mpIntConn;
			mpIntConn = NULL;
			delete mpExtConn;
			mpExtConn = NULL;
			return false;
		}
		mpExtConn->setEventHandler(this);
		
		return true;
	}

	void shutdown()
	{
		if (mpIntConn)
		{
			mpIntConn->shutdown();
			delete mpIntConn;
			mpIntConn = NULL;
		}
		if (mpExtConn)
		{
			mpExtConn->shutdown();
			delete mpExtConn;
			mpExtConn = NULL;
		}
	}	

	Connection* getIntConn() const
	{
		return mpIntConn;
	}

	// Connection::Handler
	virtual void onConnected(Connection *pConn)
	{
		if (pConn == mpExtConn)
		{
			logTraceLn("ss connected!");
			_flushAll();
		}
	}
	
	virtual void onDisconnected(Connection *pConn)
	{
		if (pConn == mpIntConn)
		{
			if (mpHandler)
				mpHandler->onIntConnDisconnected(this);
		}
	}

	virtual void onRecv(Connection *pConn, const void *data, size_t datalen)
	{
		if (pConn == mpIntConn)
		{
			if (!mpExtConn->isConnected())
			{
				_reconnectExternal();
				mCache->cache(data, datalen);
			}
			else
			{
				_flushAll();
				mpExtConn->send(data, datalen);
			}
			logInfoLn("internal recvlen="<<datalen);
		}
		else
		{
			mpIntConn->send(data, datalen);
			logInfoLn("external recvlen="<<datalen);
		}
	}
	
	virtual void onError(Connection *pConn)
	{
		if (pConn == mpIntConn)
		{
			if (mpHandler)
				mpHandler->onIntConnError(this);
		}
	}		

	void _reconnectExternal()
	{
		ulong curtick = core::coreClock();
		if (curtick > mLastExtConnTime+1000)
		{
			mLastExtConnTime = curtick;
			mpExtConn->connect((const SA *)&RemoteAddr, sizeof(RemoteAddr));
		}		
	}

	void _flushAll()
	{
		if (mpExtConn->isConnected())
		{
			mCache->flushAll();
		}
	}
	void flush(const char *data, size_t datalen)
	{
		mpExtConn->send(data, datalen);
	}
	
  private:	
	typedef Cache<ClientBridge> MyCache;
	
	EventPoller *mEventPoller;
	Handler *mpHandler;
	
	Connection *mpIntConn;
	Connection *mpExtConn;

	MyCache *mCache;

	ulong mLastExtConnTime;
};
//--------------------------------------------------------------------------

//--------------------------------------------------------------------------
class Client : public Listener::Handler, public ClientBridge::Handler
{
  public:
    Client(EventPoller *poller)
			:Listener::Handler()
			,mEventPoller(poller)
			,mListener(poller)
			,mBridges()
	{
	}
	
    virtual ~Client()
	{
	}

	bool create()
	{
		if (!mListener.create((const SA *)&ListenAddr, sizeof(ListenAddr)))
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
		BridgeList::iterator it = mBridges.begin();
		for (; it != mBridges.end(); ++it)
		{
			ClientBridge *bridge = *it;
			if (bridge)
			{
				bridge->shutdown();
				delete bridge;
			}
		}
		mBridges.clear();
	}

	virtual void onAccept(int connfd)
	{
		ClientBridge *bridge = new ClientBridge(mEventPoller, this);
		if (!bridge->acceptConnection(connfd))
		{
			delete bridge;
			return;
		}

		mBridges.insert(bridge);
		logInfoLn("a connection createted! cursize:"<<mBridges.size());
	}

	virtual void onIntConnDisconnected(ClientBridge *pBridge)
	{				
		onBridgeShut(pBridge);
		logInfoLn("a connection closed! cursize:"<<mBridges.size());
	}
	
	virtual void onIntConnError(ClientBridge *pBridge)
	{		
		onBridgeShut(pBridge);
		logInfoLn("a connection occur error! cursize:"<<mBridges.size()<<" reason:"<<coreStrError());
	}	   	
	
  private:
	void onBridgeShut(ClientBridge *pBridge)
	{		
		BridgeList::iterator it = mBridges.find(pBridge);
		if (it != mBridges.end())
		{			
			mBridges.erase(it);
		}

		pBridge->shutdown();
		delete pBridge;
	}
	
  private:
	typedef std::set<ClientBridge *> BridgeList;
	
	EventPoller *mEventPoller;
	Listener mListener;

	BridgeList mBridges;
};
//--------------------------------------------------------------------------

int main(int argc, char *argv[])
{
	// initialise tracer
	core::createTrace();
	core::output2Console();
	core::output2Html("fasttun_test.html");

	// parse parameter
	const char *confPath = NULL;
	const char *listenAddr = NULL;
	const char *remoteAddr = NULL;
	
	int opt = 0;
	while ((opt = getopt(argc, argv, "c:l:r:")) != -1)
	{
		switch (opt)
		{
		case 'c':
			confPath = optarg;
			break;
		case 'l':
			listenAddr = optarg;
			break;
		case 'r':
			remoteAddr = optarg;
			break;
		default:
			break;
		}
	}	
	
	if (argc == 1)
	{
		confPath = DEFAULT_CONF_PATH;
	}
	if (confPath)
	{
		Ini ini(confPath);
		static std::string s_listenAddr = ini.getString("test", "listen", "");
		static std::string s_remoteAddr = ini.getString("test", "remote", "");
		if (s_listenAddr != "")
			listenAddr = s_listenAddr.c_str();
		if (s_remoteAddr != "")
			remoteAddr = s_remoteAddr.c_str();
	}
	
	if (NULL == listenAddr || NULL == remoteAddr)
	{
		fprintf(stderr, "no argument assigned or parse argument failed!\n");
		core::closeTrace();
		exit(EXIT_FAILURE);
	}
	if (!core::str2Ipv4(listenAddr, ListenAddr) || !core::str2Ipv4(remoteAddr, RemoteAddr))
	{
		logErrorLn("invalid socket address!");
		core::closeTrace();
		exit(EXIT_FAILURE);
	}

	// create event poller
	EventPoller *netPoller = EventPoller::create();

	// create client
	Client cli(netPoller);
	if (!cli.create())
	{
		logErrorLn("create client error!");
		delete netPoller;
		core::closeTrace();
		exit(EXIT_FAILURE);
	}	

	for (;;)
	{
		netPoller->processPendingEvents(-1);
	}	

	cli.finalise();	
	delete netPoller;

	// close tracer
	core::closeTrace();
	exit(0);
}
