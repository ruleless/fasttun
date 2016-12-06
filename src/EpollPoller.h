#ifndef __POLLEREPOLL_H__
#define __POLLEREPOLL_H__

#include "EventPoller.h"

#ifdef HAS_EPOLL

NAMESPACE_BEG(tun)

class EpollPoller : public EventPoller
{
  public:
    EpollPoller(int expectedSize = 20);
    virtual ~EpollPoller();

    virtual int getFileDescriptor() const
    {
        return mEpfd;
    }
  protected:
    virtual bool doRegisterForRead(int fd)
    {
        return this->doRegister(fd, true, true);
    }

    virtual bool doRegisterForWrite(int fd)
    {
        return this->doRegister(fd, false, true);
    }

    virtual bool doDeregisterForRead(int fd)
    {
        return this->doRegister(fd, true, false);
    }

    virtual bool doDeregisterForWrite(int fd)
    {
        return this->doRegister(fd, false, false);
    }

    virtual int processPendingEvents(double maxWait);

    bool doRegister(int fd, bool isRead, bool isRegister);
  private:
    int mEpfd;
};

NAMESPACE_END // namespace tun

#endif // HAS_EPOLL
#endif // __POLLEREPOLL_H__
