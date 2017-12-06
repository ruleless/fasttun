#include "fasttun_base.h"
#include "listener.h"
#include "connection.h"
#include "kcp_tunnel.h"
#include "fast_connection.h"

using namespace tun;

core::Timers tun::gTimer;

typedef KcpTunnelGroup<false> MyTunnelGroup;
static MyTunnelGroup *gTunnelManager = NULL;

static sockaddr_in ListenAddr;
static sockaddr_in RemoteAddr;
static sockaddr_in KcpRemoteAddr;

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
        mLastExtConnTime = core::getClock();
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
    }
    virtual void onError(FastConnection *pConn)
    {
        _reconnectExternal();
        WarningPrint("a fast connection ocur an error! reason:%s", coreStrError());
    }
    virtual void onCreateKcpTunnelFailed(FastConnection *pConn)
    {
        _reconnectExternal();
        WarningPrint("create fast connection faild!");
    }

    virtual void onRecv(FastConnection *pConn, const void *data, size_t datalen)
    {
        mpIntConn->send(data, datalen);
    }

    void _reconnectExternal()
    {
        ulong curtick = core::getClock();
        if (curtick > mLastExtConnTime+10000)
        {
            mLastExtConnTime = curtick;
            mpExtConn->connect((const SA *)&RemoteAddr, sizeof(RemoteAddr));
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
            ,mShutedBridges()
    {
    }
    
    virtual ~Client()
    {
    }

    bool create(const SA *sa, socklen_t salen)
    {
        if (!mListener.create(sa, salen))
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
            ClientBridge *bridge = *it;
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
        ClientBridge *bridge = new ClientBridge(mEventPoller, this);
        if (!bridge->acceptConnection(connfd))
        {
            delete bridge;
            return;
        }

        mBridges.insert(bridge);
        DebugPrint("a connection createted! cursize:%u", mBridges.size());
    }

    virtual void onIntConnDisconnected(ClientBridge *pBridge)
    {       
        onBridgeShut(pBridge);
        DebugPrint("a connection closed! cursize:%u", mBridges.size());
    }
    
    virtual void onIntConnError(ClientBridge *pBridge)
    {
        onBridgeShut(pBridge);
        WarningPrint("a connection occur error! cursize:%u reason:%s", mBridges.size(), coreStrError());
    }
    
  private:
    void onBridgeShut(ClientBridge *pBridge)
    {       
        BridgeList::iterator it = mBridges.find(pBridge);
        if (it != mBridges.end())
        {           
            mBridges.erase(it);
        }

        mShutedBridges.insert(pBridge);
    }   
    
  private:
    typedef std::set<ClientBridge *> BridgeList;
    
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
    const char *remoteAddr = NULL;
    const char *kcpRemoteAddr = NULL;
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
        case 'r':
            remoteAddr = optarg;
            break;
        case 'u':
            kcpRemoteAddr = optarg;
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

        log_reg_filelog("log", "tuncli-", "/var/log", "tuncli-old-", "/var/log");
    }
    else
    {
        if (log_initialise(logLevel) != 0)
        {
            fprintf(stderr, "init log failed!");
            exit(1);
        }

        log_reg_filelog("log", "tuncli-", "/tmp", "tuncli-old-", "/tmp");
        log_reg_console();
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
        static std::string s_kcpRemoteAddr = ini.getString("local", "kcpremote", s_remoteAddr.c_str());
        if (s_listenAddr != "")
            listenAddr = s_listenAddr.c_str();
        if (s_remoteAddr != "")
            remoteAddr = s_remoteAddr.c_str();
        if (s_kcpRemoteAddr != "")
            kcpRemoteAddr = s_kcpRemoteAddr.c_str();
    }
    
    if (NULL == listenAddr || NULL == remoteAddr || NULL == kcpRemoteAddr)
    {
        fprintf(stderr, "no argument assigned or parse argument failed!\n");
        log_finalise();
        exit(EXIT_FAILURE);
    }
    if (!core::str2Ipv4(listenAddr, ListenAddr) ||
        !core::str2Ipv4(remoteAddr, RemoteAddr) ||
        !core::str2Ipv4(kcpRemoteAddr, KcpRemoteAddr))
    {
        ErrorPrint("invalid socket address!");
        log_finalise();
        exit(EXIT_FAILURE);
    }

    // create event poller
    EventPoller *netPoller = EventPoller::create();

    // kcp tunnel manager
    gTunnelManager = new MyTunnelGroup(netPoller);
    if (!gTunnelManager->create(kcpRemoteAddr))
    {
        ErrorPrint("initialise Tunnel Manager error!");
        delete netPoller;
        log_finalise();
        exit(EXIT_FAILURE);
    }

    // create client        
    Client cli(netPoller);
    if (!cli.create((const SA *)&ListenAddr, sizeof(ListenAddr)))
    {
        ErrorPrint("create client error!");
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

        cli.update();

        maxWait  = min(nextKcpUpdateInterval, nextTimerCheckInterval);
        maxWait *= 0.001f;      
    }
    DebugPrint("Leave Main Loop...");

    // finalise
    cli.finalise(); 
    
    gTunnelManager->shutdown();
    delete gTunnelManager;
    
    delete netPoller;

    // uninit log
    DebugPrint("Exit Fasttun!");
    log_finalise();
    exit(0);
}
