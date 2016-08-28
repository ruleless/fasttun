#include "FasttunBase.h"
#include "Listener.h"
#include "Connection.h"
#include "KcpTunnel.h"
#include "FastConnection.h"

using namespace tun;

typedef KcpTunnelGroup<false> MyTunnelGroup;
static MyTunnelGroup *gTunnelManager = NULL;

static sockaddr_in ListenAddr;
static sockaddr_in RemoteAddr;

int kcpOutput(const char *buf, int len, ikcpcb *kcp, void *user)
{
	ITunnel *pTunnel = (ITunnel *)user;
	if (pTunnel)
	{
		assert(pTunnel->_output(buf, len) == len && "kcp outputed len illegal");
	}
	return 0;
}

//--------------------------------------------------------------------------
class ClientBridge : public Connection::Handler, public FastConnection::Handler
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
			,mLastExtConnTime(0)
	{}
	
    virtual ~ClientBridge()
	{
		shutdown();
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

		mpExtConn = new FastConnection(mEventPoller, gTunnelManager);
		mLastExtConnTime = getTickCount();
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

	// Connection::Handler
	virtual void onConnected(FastConnection *pConn)
	{
		logInfoLn("a fast connection connected!");
	}
	
	virtual void onDisconnected(Connection *pConn)
	{
		if (mpHandler)
			mpHandler->onIntConnDisconnected(this);
	}	
	
	virtual void onError(Connection *pConn)
	{
		if (mpHandler)
			mpHandler->onIntConnError(this);
	}

	virtual void onRecv(Connection *pConn, const void *data, size_t datalen)
	{
		mpExtConn->send(data, datalen);
		if (!mpExtConn->isConnected())
			_reconnectExternal();
	}

	// FastConnection::Handler
	virtual void onDisconnected(FastConnection *pConn)
	{
		_reconnectExternal();
		logInfoLn("a fast connection closed!");
	}
	virtual void onError(FastConnection *pConn)
	{
		_reconnectExternal();
		logInfoLn("a fast connection ocur an error! reason:"<<coreStrError());
	}
	virtual void onCreateKcpTunnelFailed(FastConnection *pConn)
	{
		_reconnectExternal();
		logInfoLn("create fast connection faild!");
	}	

	virtual void onRecv(FastConnection *pConn, const void *data, size_t datalen)
	{
		mpIntConn->send(data, datalen);
	}

	void _reconnectExternal()
	{
		ulong curtick = getTickCount();
		if (curtick > mLastExtConnTime+10000)
		{
			mLastExtConnTime = curtick;
			mpExtConn->connect((const SA *)&RemoteAddr, sizeof(RemoteAddr));
			logInfoLn("reconnect remote fast server!");
		}		
	}
	
  private:
	EventPoller *mEventPoller;
	Handler *mpHandler;
	
	Connection *mpIntConn;
	FastConnection *mpExtConn;

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

	bool create(const SA *sa, socklen_t salen)
	{
		if (!mListener.create(sa, salen))
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

void sigHandler(int signo)
{
	switch (signo)
	{
	case SIGPIPE:
		{
			logWarningLn("broken pipe!");
		}
		break;
	default:
		break;
	}	
}

int main(int argc, char *argv[])
{
	// initialise tracer
	core::createTrace();
	core::output2Console();
	core::output2Html("fasttun_client.html");

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
		static std::string s_listenAddr = ini.getString("local", "listen", "");
		static std::string s_remoteAddr = ini.getString("local", "remote", "");
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
	if (!cli.create((const SA *)&ListenAddr, sizeof(ListenAddr)))
	{
		logErrorLn("create client error!");
		delete netPoller;
		core::closeTrace();
		exit(EXIT_FAILURE);
	}

	gTunnelManager = new MyTunnelGroup(netPoller);
	if (!gTunnelManager->create(remoteAddr))
	{
		logErrorLn("initialise Tunnel Manager error!");
		delete netPoller;
		core::closeTrace();
		exit(EXIT_FAILURE);
	}
	
	signal(SIGPIPE, sigHandler);

	double maxWait = 0;
	for (;;)
	{
		netPoller->processPendingEvents(maxWait);
		maxWait = gTunnelManager->update();
		maxWait *= 0.001f;
	}

	gTunnelManager->shutdown();
	delete gTunnelManager;
	cli.finalise();	
	delete netPoller;

	// close tracer
	core::closeTrace();
	exit(0);
}
