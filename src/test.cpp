#include "FasttunBase.h"
#include "Listener.h"
#include "Connection.h"
#include "KcpTunnel.h"
#include "FastConnection.h"

using namespace tun;

static KcpTunnelGroup gTunnelManager;

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
			,mRemainData()
			,mLastExtConnTime(0)
	{}
	
    virtual ~ClientBridge()
	{
		DataList::iterator it = mRemainData.begin();
		for (; it != mRemainData.end(); ++it)
		{
			free((*it).data);		
		}
		mRemainData.clear();
		
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

		mpExtConn = new Connection(mEventPoller);
		mLastExtConnTime = getTickCount();
		if (!mpExtConn->connect("127.0.0.1", 5082))
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

	int getIntSockFd() const
	{
		if (mpIntConn)
			return mpIntConn->getSockFd();
		return -1;
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
			logTraceLn("clientint onRecv len="<<datalen);
			if (!mpExtConn->isConnected())
			{
				_reconnectExternal();
				
				Data d;
				d.datalen = datalen;
				d.data = (char *)malloc(datalen);
				memcpy(d.data, data, datalen);
				mRemainData.push_back(d);
			}
			else
			{
				_flushAll();
				mpExtConn->send(data, datalen);
				logTraceLn("mpExtConn->send len="<<datalen);
			}
		}
		else
		{
			logTraceLn("clientext onRecv len="<<datalen);
			mpIntConn->send(data, datalen);
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
		ulong curtick = getTickCount();
		if (curtick > mLastExtConnTime+1000)
		{
			mLastExtConnTime = curtick;
			mpExtConn->connect("127.0.0.1", 5082);
		}		
	}

	void _flushAll()
	{
		if (mpExtConn->isConnected())
		{
			DataList::iterator it = mRemainData.begin();
			for (; it != mRemainData.end(); ++it)
			{
				const Data &d = *it;
				if (d.data && d.datalen > 0)
				{
					mpExtConn->send(d.data, d.datalen);
					logTraceLn("mpExtConn->send len="<<d.datalen);
				}
				free(d.data);
			}
			mRemainData.clear();
		}
	}
	
  private:
	struct Data
	{
		size_t datalen;
		char *data;
	};

	typedef std::list<Data> DataList;
	
	EventPoller *mEventPoller;
	Handler *mpHandler;
	
	Connection *mpIntConn;
	Connection *mpExtConn;

	DataList mRemainData;

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
			,mConns()
	{
	}
	
    virtual ~Client()
	{
	}

	bool create()
	{
		if (!mListener.create("127.0.0.1", 5081))
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
			ClientBridge *bridge = it->second;
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
		ClientBridge *bridge = new ClientBridge(mEventPoller, this);
		if (!bridge->acceptConnection(connfd))
		{
			delete bridge;
			return;
		}

		mConns.insert(std::pair<int, ClientBridge *>(connfd, bridge));
	}

	virtual void onIntConnDisconnected(ClientBridge *pBridge)
	{
		char strSrc[1024] = {0};
		if (getPeerAddrInfo(pBridge, strSrc, sizeof(strSrc)))
		{
			logWarningLn("from "<<strSrc<<"! connection disconnected. reason:"<<coreStrError()
						 <<" we now have "<<mConns.size()-1<<" connections!");
		}		
		
		onBridgeShut(pBridge);
	}
	
	virtual void onIntConnError(ClientBridge *pBridge)
	{
		char strSrc[1024] = {0};
		if (getPeerAddrInfo(pBridge, strSrc, sizeof(strSrc)))
		{
			logWarningLn("from "<<strSrc<<"! got error on connection! reason:"<<coreStrError()
						 <<" we now have "<<mConns.size()-1<<" connections!");
		}

		onBridgeShut(pBridge);
	}	   	
	
  private:
	void onBridgeShut(ClientBridge *pBridge)
	{		
		ConnMap::iterator it = mConns.find(pBridge->getIntSockFd());
		if (it != mConns.end())
		{			
			mConns.erase(it);
		}

		pBridge->shutdown();
		delete pBridge;
	}

	char* getPeerAddrInfo(ClientBridge *pBridge, char *info, int len)
	{
		char peerIp[MAX_BUF] = {0};
		if (pBridge->getIntConn() && pBridge->getIntConn()->getPeerIp(peerIp, MAX_BUF))
		{
			snprintf(info, len, "%s:%d", peerIp, pBridge->getIntConn()->getPeerPort());
			return info;
		}
		return NULL;
	}	
  private:
	typedef std::map<int, ClientBridge *> ConnMap;
	
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
	core::output2Html("fasttun_client.html");

	// create event poller
	EventPoller *netPoller = EventPoller::create();

	// create client
	Client cli(netPoller);
	if (!cli.create())
	{
		logErrorLn("create client error!");
		delete netPoller;
		core::closeTrace();
		exit(1);
	}

	if (!gTunnelManager.initialise("0.0.0.0:29900", "127.0.0.1:29901"))
	{
		logErrorLn("initialise Tunnel Manager error!");
		delete netPoller;
		core::closeTrace();
		exit(1);
	}

	for (;;)
	{
		netPoller->processPendingEvents(0.016);
		gTunnelManager.update();
	}	

	gTunnelManager.finalise();
	cli.finalise();	
	delete netPoller;

	// close tracer
	core::closeTrace();
	exit(0);
}
