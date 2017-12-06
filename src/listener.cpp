#include "listener.h"

NAMESPACE_BEG(tun)

Listener::~Listener()
{
    finalise();
}

bool Listener::create(const char *ip, int port)
{
    struct sockaddr_in remoteAddr;
    remoteAddr.sin_family = AF_INET;
    remoteAddr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &remoteAddr.sin_addr) < 0)
    {
        ErrorPrint("Listener::create()  illegal ip(%s)", ip);
        return false;
    }

    return create((const SA *)&remoteAddr, sizeof(remoteAddr));
}

bool Listener::create(const SA *sa, socklen_t salen)
{   
    if (mFd >= 0)
    {
        ErrorPrint("Listener already created!");
        return false;
    }

    mFd = socket(AF_INET, SOCK_STREAM, 0);
    if (mFd < 0)
    {
        ErrorPrint("Listener::create()  create socket error! %s", coreStrError());
        return false;
    }

    int opt = 1;
    setsockopt(mFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

#ifdef SO_REUSEPORT
    opt = 1;
    setsockopt(mFd, SOL_SOCKET, SO_REUSEPORT, (const char*)&opt, sizeof(opt));
#endif

    // set nonblocking  
    if (!core::setNonblocking(mFd))
    {
        ErrorPrint("Listener::create()  set nonblocking error! %s", coreStrError());
        goto err_1;
    }
    
    if (bind(mFd, sa, salen) < 0)
    {
        ErrorPrint("Listener::create()  bind error! %s", coreStrError());
        goto err_1;
    }

    if (listen(mFd, LISTENQ) < 0)
    {
        ErrorPrint("Listener::create()  listen failed! %s", coreStrError());
        goto err_1;
    }

    if (!mEventPoller->registerForRead(mFd, this))
    {
        ErrorPrint("Listener::create()  registerForRead failed! %s", coreStrError());
        goto err_1;
    }

    return true;

err_1:
    close(mFd);
    mFd = -1;
    
    return false;
}

void Listener::finalise()
{
    if (mFd < 0)
        return;
    
    mEventPoller->deregisterForRead(mFd);
    close(mFd);
    mFd = -1;
}

int Listener::handleInputNotification(int fd)
{
    struct sockaddr_in addr;
    socklen_t addrlen;
    int newConns = 0;
    while (newConns++ < 32)
    {
        addrlen = sizeof(addr);
        int connfd = accept(fd, (SA *)&addr, &addrlen);
        if (connfd < 0)
        {            
            // DebugPrint("accept failed! %s", coreStrError());
            break;
        }
        else
        {
#if 0
            struct sockaddr_in localAddr, remoteAddr;
            socklen_t localAddrLen = sizeof(localAddr), remoteAddrLen = sizeof(remoteAddr);
            if (getsockname(connfd, (SA *)&localAddr, &localAddrLen) == 0 &&
                getpeername(connfd, (SA *)&remoteAddr, &remoteAddrLen) == 0)
            {
                char remoteip[MAX_BUF] = {0}, localip[MAX_BUF] = {0};
                if (inet_ntop(AF_INET, &localAddr.sin_addr, localip, sizeof(localip)) &&
                    inet_ntop(AF_INET, &remoteAddr.sin_addr, remoteip, sizeof(remoteip)))
                {
                    char acceptLog[1024] = {0};
                    snprintf(acceptLog, sizeof(acceptLog), "(%s:%d) accept from %s:%d",
                             localip, ntohs(localAddr.sin_port),
                             remoteip, ntohs(remoteAddr.sin_port));
                    DebugPrint(acceptLog);
                }
            }
#endif 
            
            if (mHandler)
            {
                mHandler->onAccept(connfd);
            }
        }
    }
    
    return 0;
}

NAMESPACE_END // namespace tun
