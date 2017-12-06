#ifndef __LISTENER_H__
#define __LISTENER_H__

#include "fasttun_base.h"
#include "event_poller.h"

NAMESPACE_BEG(tun)

class Listener : InputNotificationHandler
{
  public:
    struct Handler
    {
        virtual void onAccept(int connfd) = 0;
    };
    
    Listener(EventPoller *poller)
            :mFd(-1)
            ,mHandler(NULL)
            ,mEventPoller(poller)
    {
        assert(mEventPoller && "Listener::mEventPoller != NULL");
    }
    
    virtual ~Listener();

    bool create(const char *ip, int port);
    bool create(const SA *sa, socklen_t salen);
    void finalise();

    inline void setEventHandler(Handler *h)
    {
        mHandler = h;
    }

    // InputNotificationHandler
    virtual int handleInputNotification(int fd);
  private:
    int mFd;
    Handler *mHandler;
    
    EventPoller *mEventPoller;  
};

NAMESPACE_END // namespace tun

#endif // __LISTENER_H__
