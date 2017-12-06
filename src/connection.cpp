#include "connection.h"

NAMESPACE_BEG(tun)

Connection::~Connection()
{
    shutdown();
    free(mBuffer);
}

bool Connection::acceptConnection(int connfd)
{
    if (mFd >= 0)
    {
        ErrorPrint("[acceptConnection] accept connetion error! the conn is in use!");
        return false;
    }

    mFd = connfd;

    // set nonblocking
    if (!core::setNonblocking(mFd))
    {
        ErrorPrint("[acceptConnection] set nonblocking error! %s", coreStrError());
        goto err_1;
    }

    tryRegReadEvent();

    mConnStatus = ConnStatus_Connected;
    return true;

err_1:
    close(mFd);
    mFd = -1;

    return false;
}

bool Connection::connect(const char *ip, int port)
{
    struct sockaddr_in remoteAddr;
    remoteAddr.sin_family = AF_INET;
    remoteAddr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &remoteAddr.sin_addr) < 0)
    {
        ErrorPrint("[connect] illegal ip(%s)", ip);
        return false;
    }

    return connect((const SA *)&remoteAddr, sizeof(remoteAddr));
}

bool Connection::connect(const SA *sa, socklen_t salen)
{
    if (mFd >= 0)
        shutdown();

    if (mFd >= 0)
    {
        ErrorPrint("[connect] accept connetion error! the conn is in use!");
        return false;
    }

    mFd = socket(AF_INET, SOCK_STREAM, 0);
    if (mFd < 0)
    {
        ErrorPrint("[connect] create socket error! %s", coreStrError());
        return false;
    }

    // set nonblocking
    if (!core::setNonblocking(mFd))
    {
        ErrorPrint("[connect] set nonblocking error! %s", coreStrError());
        goto err_1;
    }

    if (::connect(mFd, sa, salen) == 0) // 连接成功
    {
        tryRegReadEvent();
        mConnStatus = ConnStatus_Connected;
        if (mHandler)
            mHandler->onConnected(this);
    }
    else
    {
        if (errno == EINPROGRESS)
        {
            mConnStatus = ConnStatus_Connecting;
            tryRegWriteEvent();
        }
        else
        {
            goto err_1;
        }
    }

    return true;

err_1:
    close(mFd);
    mFd = -1;

    return false;
}

void Connection::shutdown()
{
    if (mFd < 0)
        return;

    tryUnregWriteEvent();
    tryUnregReadEvent();
    close(mFd);
    mFd = -1;
    mConnStatus = ConnStatus_Closed;

    TcpPacketList::iterator it = mTcpPacketList.begin();
    for (; it != mTcpPacketList.end(); ++it)
        delete *it;
    mTcpPacketList.clear();
}

void Connection::send(const void *data, size_t datalen)
{
    if (mFd < 0)
    {
        ErrorPrint("[send] send error! socket uninited or shuted!");
        return;
    }
    if (mConnStatus != ConnStatus_Connected)
    {
        ErrorPrint("[send] can't send data in such status(%d)", mConnStatus);
        return;
    }

    const char *ptr = (const char *)data;
    if (tryFlushRemainPacket())
    {
        int sentlen = ::send(mFd, data, datalen, 0);
        // int sentlen = -1; errno = EAGAIN;
        if ((size_t)sentlen == datalen)
            return;

        if (sentlen > 0)
        {
            ptr += sentlen;
            datalen -= sentlen;
        }
    }

    if (checkSocketErrors())
        return;

    cachePacket(ptr, datalen);
    tryRegWriteEvent(); // 注册发送缓冲区可写事件
}

bool Connection::getpeername(SA *sa, socklen_t *salen) const
{
    if (mFd < 0)
        return false;

    return ::getpeername(mFd, sa, salen) == 0;
}

bool Connection::gethostname(SA *sa, socklen_t *salen) const
{
    if (mFd < 0)
        return false;

    return ::getsockname(mFd, sa, salen) == 0;
}

int Connection::handleInputNotification(int fd)
{
    if (mConnStatus != ConnStatus_Connected)
    {
        ErrorPrint("[handleInputNotification] Connection is not connected! fd=%d", fd);
        return 0;
    }

    char *buf = mBuffer;
    int curlen = 0;
    for (;;)
    {
        int recvlen = recv(mFd, buf+curlen, MAXLEN, 0);
        if (recvlen > 0)
            curlen += recvlen;

        if (recvlen >= MAXLEN)
        {
            if (curlen >= LIMIT_LEN)
                break;

            if (buf == mBuffer)
            {
                buf = (char *)malloc(curlen+MAXLEN);
                memcpy(buf, mBuffer, curlen);
            }
            else
            {
                buf = (char *)realloc(buf, curlen+MAXLEN);
            }
        }
        else
        {
            if (recvlen < 0)
            {
                if (errno != EAGAIN && errno != EWOULDBLOCK)
                    mConnStatus = ConnStatus_Error;
            }
            else if (recvlen == 0)
            {
                mConnStatus = ConnStatus_Closed;
            }
            break;
        }
    }

    if (curlen >= LIMIT_LEN)
    {
        WarningPrint("get big data! recvlen=%d", curlen);
    }

    if (curlen > 0 && mHandler)
        mHandler->onRecv(this, buf, curlen);
    if (buf != mBuffer)
        free(buf);

    if (mHandler)
    {
        if (ConnStatus_Error == mConnStatus)
        {
            tryUnregReadEvent();
            tryUnregWriteEvent();
            mHandler->onError(this);
        }
        else if (ConnStatus_Closed == mConnStatus)
        {
            tryUnregReadEvent();
            tryUnregWriteEvent();
            mHandler->onDisconnected(this);
        }
    }

    return 0;
}

int Connection::handleOutputNotification(int fd)
{
    if (ConnStatus_Connecting == mConnStatus)
    {
        tryUnregWriteEvent();

        int err = 0;
        socklen_t errlen = sizeof(int);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen) < 0 || err != 0)
        {
            if (err != 0)
                errno = err;

            mConnStatus = ConnStatus_Error;
            if (mHandler)
            {
                tryUnregReadEvent();
                tryUnregWriteEvent();
                mHandler->onError(this);
            }

            return 0;
        }

        tryRegReadEvent();
        mConnStatus = ConnStatus_Connected;
        if (mHandler)
            mHandler->onConnected(this);
    }
    else if (ConnStatus_Connected == mConnStatus)
    {
        tryFlushRemainPacket();
        if (checkSocketErrors())
            return 0;
    }

    return 0;
}

void Connection::tryRegReadEvent()
{
    if (!mbRegForRead)
    {
        mbRegForRead = true;
        mEventPoller->registerForRead(mFd, this);
    }
}

void Connection::tryUnregReadEvent()
{
    if (mbRegForRead)
    {
        mbRegForRead = false;
        mEventPoller->deregisterForRead(mFd);
    }
}

void Connection::tryRegWriteEvent()
{
    if (!mbRegForWrite)
    {
        mbRegForWrite = true;
        mEventPoller->registerForWrite(mFd, this);
    }
}

void Connection::tryUnregWriteEvent()
{
    if (mbRegForWrite)
    {
        mbRegForWrite = false;
        mEventPoller->deregisterForWrite(mFd);
    }
}

bool Connection::tryFlushRemainPacket()
{
    if (mTcpPacketList.empty())
    {
        return true;
    }

    TcpPacketList::iterator it = mTcpPacketList.begin();
    for (; it != mTcpPacketList.end();)
    {
        TcpPacket *p = *it;
        assert(p->buflen > p->sentlen && "p->buflen > p->sentlen");
        size_t len = p->buflen - p->sentlen;
        int sentlen = ::send(mFd, p->buf+p->sentlen, len, 0);

        if (len == (size_t)sentlen)
        {
            delete p;
            mTcpPacketList.erase(it++);
            continue;
        }

        if (sentlen > 0)
            p->sentlen += sentlen;
        break;
    }

    if (mTcpPacketList.empty())
    {
        tryUnregWriteEvent();
        return true;
    }

    return false;
}

void Connection::cachePacket(const void *data, size_t datalen)
{
    TcpPacket *p = new TcpPacket(); assert(p && "tcppacket != NULL");
    p->buf = (char *)malloc(datalen); assert(p->buf && "packet->buf != NULL");
    memcpy(p->buf, data, datalen);
    p->buflen = datalen;
    p->sentlen = 0;
    mTcpPacketList.push_back(p);
}

bool Connection::checkSocketErrors()
{
    EReason err = _checkSocketErrors();
    if (err != Reason_ResourceUnavailable)
    {
        if (mHandler)
        {
            tryUnregReadEvent();
            tryUnregWriteEvent();
            mHandler->onError(this);
        }
        return true;
    }
    return false;
}

EReason Connection::_checkSocketErrors()
{
    int err;
    EReason reason;

#ifdef unix
    err = errno;
    if (EAGAIN == err || EWOULDBLOCK == err)
        return Reason_ResourceUnavailable;

    switch (err)
    {
    case ECONNREFUSED:
        reason = Reason_NoSuchPort;
        break;
    case EPIPE:
        reason = Reason_ClientDisconnected;
        break;
    case ECONNRESET:
        reason = Reason_ClientDisconnected;
        break;
    case ENOBUFS:
        reason = Reason_TransmitQueueFull;
        break;
    default:
        reason = Reason_GeneralNetwork;
        break;
    }
#else
    err = WSAGetLastError();

    if (err == WSAEWOULDBLOCK || err == WSAEINTR)
    {
        reason = Reason_ResourceUnavailable;
    }
    else
    {
        switch (err)
        {
        case WSAECONNREFUSED:
            reason = Reason_NoSuchPort;
            break;
        case WSAECONNRESET:
            reason = Reason_ClientDisconnected;
            break;
        case WSAECONNABORTED:
            reason = Reason_ClientDisconnected;
            break;
        default:
            reason = Reason_GeneralNetwork;
            break;
        }
    }
#endif

    return reason;
}

NAMESPACE_END // namespace tun
