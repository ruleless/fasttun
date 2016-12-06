#include "FasttunBase.h"
#include "Listener.h"
#include "Connection.h"
#include "KcpTunnel.h"
#include "FastConnection.h"
#include "Cache.h"

using namespace tun;

core::Timers tun::gTimer;

typedef KcpTunnelGroup<true> MyTunnelGroup;
static MyTunnelGroup *gTunnelManager = NULL;

static sockaddr_in ListenAddr, KcpListenAddr;
static sockaddr_in ConnectAddr;

//--------------------------------------------------------------------------
class ServerBridge : public Connection::Handler
                   , public FastConnection::Handler
                   , public TimerHandler
{
    static const uint32 CONNCHECK_INTERVAL = 30000;
    
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
            ,mHeartBeatTimer()
            ,mConnCheckTimer()
    {
        mCache = new MyCache(this, &ServerBridge::flush);
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

        mLastExtConnTime = core::getClock();
        mpIntConn = new Connection(mEventPoller);
        if (!mpIntConn->connect((const SA *)&ConnectAddr, sizeof(ConnectAddr)))
        {
            mpExtConn->shutdown();
            delete mpExtConn;
            mpExtConn = NULL;
            delete mpIntConn;
            mpIntConn = NULL;
            return false;
        }
        mpIntConn->setEventHandler(this);

        uint32 curClock = core::getClock();
        mHeartBeatTimer = gTimer.add(curClock+HeartBeatRecord::HEARTBEAT_INTERVAL,
                                     HeartBeatRecord::HEARTBEAT_INTERVAL,
                                     this, NULL);
        mConnCheckTimer = gTimer.add(curClock+CONNCHECK_INTERVAL,
                                     CONNCHECK_INTERVAL,
                                     this, NULL);

        return true;
    }

    void shutdown()
    {
        mHeartBeatTimer.cancel();
        mConnCheckTimer.cancel();
        
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

    FastConnection* getExtConn() const
    {
        return mpExtConn;
    }

    // Connection::Handler
    virtual void onConnected(Connection *pConn)
    {
        _flushAll();
    }
    virtual void onDisconnected(Connection *pConn)
    {
        _reconnectInternal();
    }

    virtual void onRecv(Connection *pConn, const void *data, size_t datalen)
    {
        mpExtConn->send(data, datalen);
    }
    
    virtual void onError(Connection *pConn)
    {
        _reconnectInternal();
        logWarningLn("occur an error at an internal connection! reason:"<<coreStrError());
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
        if (!mpIntConn->isConnected())
        {
            _reconnectInternal();
            mCache->cache(data, datalen);
        }
        else
        {
            _flushAll();
            mpIntConn->send(data, datalen);
        }
    }

    // TimerHandler
    virtual void onTimeout(TimerHandle handle, void *pUser)
    {
        if (handle == mHeartBeatTimer)
        {
            if (mpExtConn)
                mpExtConn->triggerHeartBeatPacket();
        }
        else if (handle == mConnCheckTimer)
        {
            if (mpExtConn)
            {
                const HeartBeatRecord& rec = mpExtConn->getHeartBeatRecord();

                if (rec.isTimeout())
                {
                    logInfoLn("External Connection Timeout!");
                    if (mpHandler)
                        mpHandler->onExtConnError(this);
                }
            }
        }
    }

    void _reconnectInternal()
    {
        ulong curtick = core::getClock();
        if (curtick > mLastExtConnTime+1000)
        {
            mLastExtConnTime = curtick;         
            mpIntConn->connect((const SA *)&ConnectAddr, sizeof(ConnectAddr));
        }       
    }

    void _flushAll()
    {
        if (mpIntConn->isConnected())
        {
            mCache->flushAll();
        }
    }
    bool flush(const void *data, size_t len)
    {
        mpIntConn->send(data, len);
        return true;
    }
    
  private:  
    typedef Cache<ServerBridge> MyCache;
    
    EventPoller *mEventPoller;
    Handler *mpHandler;

    Connection *mpIntConn;
    FastConnection *mpExtConn;

    MyCache *mCache;

    ulong mLastExtConnTime;

    TimerHandle mHeartBeatTimer;
    TimerHandle mConnCheckTimer;
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
            ,mBridges()
            ,mShutedBridges()
    {
    }
    
    virtual ~Server()
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

        update();
        BridgeList::iterator it = mBridges.begin();
        for (; it != mBridges.end(); ++it)
        {
            ServerBridge *bridge = *it;
            if (bridge)
            {
                bridge->shutdown();
                delete bridge;
            }
        }
        mBridges.clear();
    }

    // call it ervery frame
    void update()
    {
        BridgeList::iterator it = mShutedBridges.begin();
        for (; it != mShutedBridges.end(); ++it)
        {
            (*it)->shutdown();
            delete *it;     
        }
        mShutedBridges.clear();
    }

    virtual void onAccept(int connfd)
    {
        ServerBridge *bridge = new ServerBridge(mEventPoller, this);
        if (!bridge->acceptConnection(connfd))
        {
            delete bridge;
            return;
        }

        mBridges.insert(bridge);
        logInfoLn("a fast connection createted! cursize:"<<mBridges.size());
    }

    virtual void onExtConnDisconnected(ServerBridge *pBridge)
    {       
        onBridgeShut(pBridge);
        logInfoLn("a fast connection closed! cursize:"<<mBridges.size());
    }
    
    virtual void onExtConnError(ServerBridge *pBridge)
    {       
        onBridgeShut(pBridge);
        logInfoLn("a fast connection occur error! cursize:"<<mBridges.size()<<" reason:"<<coreStrError());
    }
    
  private:
    void onBridgeShut(ServerBridge *pBridge)
    {       
        BridgeList::iterator it = mBridges.find(pBridge);
        if (it != mBridges.end())
        {           
            mBridges.erase(it);
        }

        mShutedBridges.insert(pBridge);
    }
    
  private:
    typedef std::set<ServerBridge *> BridgeList;
    
    EventPoller *mEventPoller;
    Listener mListener;

    BridgeList mBridges;
    BridgeList mShutedBridges;
};
//--------------------------------------------------------------------------

static bool s_continueMainLoop = true;
void sigHandler(int signo)
{
    switch (signo)
    {
    case SIGPIPE:
        {
            logWarningLn("broken pipe!");
        }
        break;
    case SIGINT:
        {
            logTraceLn("catch SIGINT!");
            s_continueMainLoop = false;
        }
        break;
    case SIGQUIT:
        {
            logTraceLn("catch SIGQUIT!");
            s_continueMainLoop = false;
        }
        break;
    case SIGKILL:
        {
            logTraceLn("catch SIGKILL!");
            s_continueMainLoop = false;
        }
        break;
    case SIGTERM:
        {
            logTraceLn("catch SIGTERM!");
            s_continueMainLoop = false;
        }
        break;
    default:
        break;
    }   
}

int main(int argc, char *argv[])
{   
    // parse parameter
    int pidFlags = 0;
    bool bVerbose = false;
    const char *confPath = NULL;
    const char *listenAddr = NULL;
    const char *kcpListenAddr = NULL;
    const char *connectAddr = NULL;
    const char *pidPath = NULL;
    
    int opt = 0;
    while ((opt = getopt(argc, argv, "f:c:l:r:v")) != -1)
    {
        switch (opt)
        {
        case 'c':
            confPath = optarg;
            break;
        case 'l':
            listenAddr = optarg;
            break;
        case 'u':
            kcpListenAddr = optarg;
            break;
        case 'r':
            connectAddr = optarg;
            break;
        case 'f':
            pidFlags = 1;
            pidPath = optarg;
            break;
        case 'v':
            bVerbose = true;
            break;
        default:
            break;
        }
    }

    // daemoniize
    int traceLevel = core::levelTrace|core::levelWarning|
                     core::levelError|core::levelEmphasis;
    if (bVerbose)
        traceLevel |= core::levelInfo;
    if (pidFlags)
    {
        daemonize(pidPath);
        
        core::createTrace(traceLevel);
        core::output2File("/var/log/tun-svr.log");
    }
    else
    {
        core::createTrace(traceLevel);
        core::output2Console();
        core::output2File("tun-svr.log");
    }
    
    if (argc == 1)
    {
        confPath = DEFAULT_CONF_PATH;
    }
    if (confPath)
    {
        Ini ini(confPath);
        static std::string s_listenAddr = ini.getString("server", "listen", "");
        static std::string s_kcpListenAddr = ini.getString("server", "kcplisten", s_listenAddr.c_str());
        static std::string s_connectAddr = ini.getString("server", "connect", "");
        if (s_listenAddr != "")
            listenAddr = s_listenAddr.c_str();
        if (s_connectAddr != "")
            connectAddr = s_connectAddr.c_str();
        if (s_kcpListenAddr != "")
            kcpListenAddr = s_kcpListenAddr.c_str();
    }

    if (NULL == listenAddr || NULL == connectAddr || NULL == kcpListenAddr)
    {
        fprintf(stderr, "no argument assigned or parse argument failed!\n");
        core::closeTrace();
        exit(EXIT_FAILURE);
    }
    if (!core::str2Ipv4(listenAddr, ListenAddr) ||
        !core::str2Ipv4(connectAddr, ConnectAddr) ||
        !core::str2Ipv4(kcpListenAddr, KcpListenAddr))
    {
        logErrorLn("invalid socket address!");
        core::closeTrace();
        exit(EXIT_FAILURE);
    }

    // create event poller
    EventPoller *netPoller = EventPoller::create();

    // kcp tunnel manager
    gTunnelManager = new MyTunnelGroup(netPoller);
    if (!gTunnelManager->create((const SA *)&KcpListenAddr, sizeof(KcpListenAddr)))
    {
        logErrorLn("initialise Tunnel Manager error!");
        delete netPoller;
        core::closeTrace();
        exit(EXIT_FAILURE);
    }

    // create server
    Server svr(netPoller);
    if (!svr.create((const SA *)&ListenAddr, sizeof(ListenAddr)))
    {
        logErrorLn("create server error!");
        gTunnelManager->shutdown();
        delete gTunnelManager;
        delete netPoller;
        core::closeTrace();
        exit(EXIT_FAILURE);
    }   

    struct sigaction newAct;
    newAct.sa_handler = sigHandler;
    sigemptyset(&newAct.sa_mask);
    newAct.sa_flags = 0;
    
    sigaction(SIGPIPE, &newAct, NULL);
    
    sigaction(SIGINT, &newAct, NULL);
    sigaction(SIGQUIT, &newAct, NULL);
    
    // sigaction(SIGKILL, &newAct, NULL);   
    sigaction(SIGTERM, &newAct, NULL);

    static const uint32 MAX_WAIT = 60000;
    double maxWait = 0;
    uint32 curClock = 0, nextKcpUpdateInterval = 0, nextTimerCheckInterval = 0;
    logTraceLn("Enter Main Loop...");
    while (s_continueMainLoop)
    {
        curClock = core::getClock();

        netPoller->processPendingEvents(maxWait);

        nextKcpUpdateInterval = gTunnelManager->update();

        gTimer.process(curClock);
        nextTimerCheckInterval = gTimer.nextExp(curClock);
        if (0 == nextTimerCheckInterval)
            nextTimerCheckInterval = MAX_WAIT;

        svr.update();

        maxWait  = min(nextKcpUpdateInterval, nextTimerCheckInterval);
        maxWait *= 0.001f;      
    }
    logTraceLn("Leave Main Loop...");

    // finalise
    svr.finalise();
    
    gTunnelManager->shutdown();
    delete gTunnelManager;
    
    delete netPoller;

    // close tracer
    logTraceLn("Exit Fasttun!");
    core::closeTrace();
    exit(0);
}
