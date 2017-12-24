#include "fasttun_base.h"
#include "select_poller.h"
#include "epoll_poller.h"
#include "listener.h"
#include "connection.h"
#include "kcp_tunnel.h"
#include "fast_connection.h"
#include "cache.h"

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
            ,mIntConn(poller)
            ,mExtConn(poller, gTunnelManager)
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
        if (!mExtConn.acceptConnection(connfd))
        {
            return false;
        }
        mExtConn.setEventHandler(this);

        mLastExtConnTime = core::getClock();
        mIntConn.setEventHandler(this);
        if (!mIntConn.connect((const SA *)&ConnectAddr, sizeof(ConnectAddr)))
        {
            mExtConn.shutdown();
            return false;
        }

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

        mExtConn.shutdown();
        mIntConn.shutdown();
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
        mExtConn.send(data, datalen);
    }

    virtual void onError(Connection *pConn)
    {
        WarningPrint("occur an error at an internal connection! reason:%s", coreStrError());
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
        if (!mIntConn.isConnected())
        {
            _reconnectInternal();
            mCache->cache(data, datalen);
        }
        else
        {
            _flushAll();
            mIntConn.send(data, datalen);
        }
    }

    // TimerHandler
    virtual void onTimeout(TimerHandle handle, void *pUser)
    {
        if (handle == mHeartBeatTimer)
        {
            mExtConn.triggerHeartBeatPacket();
        }
        else if (handle == mConnCheckTimer)
        {
            const HeartBeatRecord &rec = mExtConn.getHeartBeatRecord();

            if (rec.isTimeout())
            {
                InfoPrint("External Connection Timeout!");
                if (mpHandler)
                    mpHandler->onExtConnError(this);
            }
        }
    }

    void _reconnectInternal()
    {
        ulong curtick = core::getClock();
        if (curtick > mLastExtConnTime+1000)
        {
            mLastExtConnTime = curtick;
            mIntConn.connect((const SA *)&ConnectAddr, sizeof(ConnectAddr));
        }
    }

    void _flushAll()
    {
        if (mIntConn.isConnected())
        {
            mCache->flushAll();
        }
    }
    bool flush(const void *data, size_t len)
    {
        mIntConn.send(data, len);
        return true;
    }

  private:
    typedef Cache<ServerBridge> MyCache;

    EventPoller *mEventPoller;
    Handler *mpHandler;

    Connection mIntConn;
    FastConnection mExtConn;

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
        if (!mListener.initialise(sa, salen))
        {
            ErrorPrint("create listener failed.");
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
        DebugPrint("a fast connection createted! cursize:%u", mBridges.size());
    }

    virtual void onExtConnDisconnected(ServerBridge *pBridge)
    {
        onBridgeShut(pBridge);
        DebugPrint("a fast connection closed! cursize:%u", mBridges.size());
    }

    virtual void onExtConnError(ServerBridge *pBridge)
    {
        onBridgeShut(pBridge);
        InfoPrint("a fast connection occur error! cursize:%u, reason:%s", mBridges.size(), coreStrError());
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
            WarningPrint("broken pipe!");
        }
        break;
    case SIGINT:
        {
            InfoPrint("catch SIGINT!");
            s_continueMainLoop = false;
        }
        break;
    case SIGQUIT:
        {
            InfoPrint("catch SIGQUIT!");
            s_continueMainLoop = false;
        }
        break;
    case SIGKILL:
        {
            InfoPrint("catch SIGKILL!");
            s_continueMainLoop = false;
        }
        break;
    case SIGTERM:
        {
            InfoPrint("catch SIGTERM!");
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
    int logLevel = InfoLog | WarningLog | ErrorLog | EmphasisLog;
    if (bVerbose)
        logLevel |= DebugLog;

    if (pidFlags)
    {
        daemonize(pidPath);

        if (log_initialise(logLevel) != 0)
        {
            fprintf(stderr, "init log failed!");
            exit(1);
        }

        log_reg_filelog("log", "tunsvr-", "/var/log", "tunsvr-old-", "/var/log");
    }
    else
    {
        if (log_initialise(logLevel) != 0)
        {
            fprintf(stderr, "init log failed!");
            exit(1);
        }

        log_reg_filelog("log", "tunsvr-", "/tmp", "tunsvr-old-", "/tmp");
        log_reg_console();
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
        log_finalise();
        exit(EXIT_FAILURE);
    }
    if (!core::str2Ipv4(listenAddr, ListenAddr) ||
        !core::str2Ipv4(connectAddr, ConnectAddr) ||
        !core::str2Ipv4(kcpListenAddr, KcpListenAddr))
    {
        ErrorPrint("invalid socket address!");
        log_finalise();
        exit(EXIT_FAILURE);
    }

    // create event poller
#ifdef HAS_EPOLL
    EventPoller *netPoller = new EpollPoller();
#else
    EventPoller *netPoller = new SelectPoller();
#endif

    // kcp tunnel manager
    gTunnelManager = new MyTunnelGroup(netPoller);
    if (!gTunnelManager->create((const SA *)&KcpListenAddr, sizeof(KcpListenAddr)))
    {
        ErrorPrint("initialise Tunnel Manager error!");
        delete netPoller;
        log_finalise();
        exit(EXIT_FAILURE);
    }

    // create server
    Server svr(netPoller);
    if (!svr.create((const SA *)&ListenAddr, sizeof(ListenAddr)))
    {
        ErrorPrint("create server error!");
        gTunnelManager->shutdown();
        delete gTunnelManager;
        delete netPoller;
        log_finalise();
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
    DebugPrint("Enter Main Loop...");
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
    DebugPrint("Leave Main Loop...");

    // finalise
    svr.finalise();

    gTunnelManager->shutdown();
    delete gTunnelManager;

    delete netPoller;

    // uninit log
    DebugPrint("Exit Fasttun!");
    log_finalise();
    exit(0);
}
