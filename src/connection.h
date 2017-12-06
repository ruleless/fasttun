#ifndef __CONNECTION_H__
#define __CONNECTION_H__

#include "fasttun_base.h"
#include "event_poller.h"

NAMESPACE_BEG(tun)

class Connection : public InputNotificationHandler, public OutputNotificationHandler
{
  public:
    class Handler
    {
      public:
        Handler() {}
        
        virtual void onConnected(Connection *pConn) {}
        virtual void onDisconnected(Connection *pConn) {}

        virtual void onRecv(Connection *pConn, const void *data, size_t datalen) = 0;
        virtual void onError(Connection *pConn) {}      
    };

    enum EConnStatus
    {
        ConnStatus_Closed,      
        ConnStatus_Error,

        ConnStatus_Connecting,
        ConnStatus_Connected,
    };
    
    Connection(EventPoller *poller)
            :mFd(-1)
            ,mConnStatus(ConnStatus_Closed)
            ,mHandler(NULL)
            ,mEventPoller(poller)
            ,mbRegForRead(false)
            ,mbRegForWrite(false)
            ,mTcpPacketList()
            ,mBuffer(NULL)
    {
        assert(mEventPoller && "Connection::mEventPoller != NULL");
        
        mBuffer = (char *)malloc(MAXLEN);
        assert(mBuffer != NULL && "Connection malloc buffer failed");
    }
    
    virtual ~Connection();

    bool acceptConnection(int connfd);
    bool connect(const char *ip, int port);
    bool connect(const SA *sa, socklen_t salen);

    void shutdown();    
    
    void send(const void *data, size_t datalen);    

    inline void setEventHandler(Handler *h)
    {
        mHandler = h;
    }

    inline bool isConnected() const
    {
        return mConnStatus == ConnStatus_Connected;
    }

    bool getpeername(SA *sa, socklen_t *salen) const;
    bool gethostname(SA *sa, socklen_t *salen) const;

    // InputNotificationHandler
    virtual int handleInputNotification(int fd);

    // OutputNotificationHandler
    virtual int handleOutputNotification(int fd);

  private:
    void tryRegReadEvent();
    void tryUnregReadEvent();
    
    void tryRegWriteEvent();
    void tryUnregWriteEvent();  
    
    bool tryFlushRemainPacket();
    void cachePacket(const void *data, size_t datalen);

    bool checkSocketErrors();
    EReason _checkSocketErrors();   
    
  private:
    static const int MAXLEN = 8*1024;
    static const int LIMIT_LEN = 1024*1024; 
    typedef std::list<TcpPacket *> TcpPacketList;
    
    int mFd;
    EConnStatus mConnStatus;
    Handler *mHandler; 

    EventPoller *mEventPoller;
    bool mbRegForRead;
    bool mbRegForWrite;

    TcpPacketList mTcpPacketList;

    char *mBuffer;
};

NAMESPACE_END // namespace tun 

#endif // __CONNECTION_H__
