#include "SelectPoller.h"

NAMESPACE_BEG(tun)

SelectPoller::SelectPoller()
        :EventPoller()
        ,mMaxFd(-1)
        ,mFdWriteCount(0)
{
    FD_ZERO(&mFdReadSet);
    FD_ZERO(&mFdWriteSet);
}

int SelectPoller::processPendingEvents(double maxWait)
{
    fd_set readFDs;
    fd_set writeFDs;
    struct timeval nextTimeout;

    FD_ZERO(&readFDs);
    FD_ZERO(&writeFDs);

    readFDs = mFdReadSet;
    writeFDs = mFdWriteSet;

    nextTimeout.tv_sec = (int)maxWait;
    nextTimeout.tv_usec = (int)((maxWait - (double)nextTimeout.tv_sec) * 1000000.0);

    uint64 startTime = getTimeStamp();
    int countReady = 0;

#ifdef _WIN32
    if (mMaxFd == -1)
    {
        Sleep(int(maxWait * 1000.0));
    }
    else
#endif
    {
        countReady = select(mMaxFd+1, &readFDs, mFdWriteCount ? &writeFDs : NULL, NULL, &nextTimeout);
    }

    mSpareTime += getTimeStamp() - startTime;
    
    if (countReady > 0)
    {
        this->handleNotifications(countReady, readFDs, writeFDs);
    }
    else if (countReady == -1)
    {
        WarningPrint("EventDispatcher::processPendingEvents() error in select() err:%s", coreStrError());
    }

    return countReady;
}

void SelectPoller::handleNotifications(int &countReady, fd_set &readFDs, fd_set &writeFDs)
{
#ifdef _WIN32
    for (unsigned i = 0; i < readFDs.fd_count; ++i)
    {
        int fd = readFDs.fd_array[i];
        --countReady;
        this->triggerRead(fd);
    }

    for (unsigned i = 0; i < writeFDs.fd_count; ++i)
    {
        int fd = writeFDs.fd_array[i];
        --countReady;
        this->triggerWrite(fd);
    }
#else
    for (int fd = 0; fd <= mMaxFd && countReady > 0; ++fd)
    {
        if (FD_ISSET(fd, &readFDs))
        {
            --countReady;
            this->triggerRead(fd);
        }

        if (FD_ISSET(fd, &writeFDs))
        {
            --countReady;
            this->triggerWrite(fd);
        }
    }
#endif
}

bool SelectPoller::doRegisterForRead(int fd)
{
#ifndef _WIN32
    if ((fd < 0) || (FD_SETSIZE <= fd))
    {
        ErrorPrint("SelectPoller::doRegisterForRead()  Tried to register invalid fd(%d) FD_SETSIZE(%d)", fd, FD_SETSIZE);

        return false;
    }
#else
    if (mFdReadSet.fd_count >= FD_SETSIZE)
    {
        ErrorLn("SelectPoller::doRegisterForRead()  Tried to register invalid fd("<<fd<<")  FD_SETSIZE("<<FD_SETSIZE<<")");     

        return false;
    }
#endif

    if (FD_ISSET(fd, &mFdReadSet))
        return false;

    FD_SET(fd, &mFdReadSet);
    mMaxFd = max(fd, mMaxFd);

    return true;
}

bool SelectPoller::doRegisterForWrite(int fd)
{
#ifndef _WIN32
    if ((fd < 0) || (FD_SETSIZE <= fd))
    {
        ErrorPrint("SelectPoller::doRegisterForWrite()  Tried to register invalid fd(%d) FD_SETSIZE(%d)", fd, FD_SETSIZE);

        return false;
    }
#else
    if (mFdWriteSet.fd_count >= FD_SETSIZE)
    {
        ErrorLn("SelectPoller::doRegisterForWrite()  Tried to register invalid fd("<<fd<<")  FD_SETSIZE("<<FD_SETSIZE<<")");        

        return false;
    }
#endif

    if (FD_ISSET(fd, &mFdWriteSet))
    {
        return false;
    }

    FD_SET(fd, &mFdWriteSet);
    mMaxFd = max(fd, mMaxFd);   
    ++mFdWriteCount;
    
    return true;
}

bool SelectPoller::doDeregisterForRead(int fd)
{
#ifndef _WIN32
    if ((fd < 0) || (FD_SETSIZE <= fd))
    {
        return false;
    }
#endif

    if (!FD_ISSET(fd, &mFdReadSet))
    {
        return false;
    }

    FD_CLR(fd, &mFdReadSet);

    if (fd == mMaxFd)
    {
        mMaxFd = this->recalcMaxFD();
    }

    return true;
}

bool SelectPoller::doDeregisterForWrite(int fd)
{
#ifndef _WIN32
    if ((fd < 0) || (FD_SETSIZE <= fd))
    {
        return false;
    }
#endif

    if (!FD_ISSET(fd, &mFdWriteSet))
    {
        return false;
    }

    FD_CLR(fd, &mFdWriteSet);

    if (fd == mMaxFd)
    {
        mMaxFd = this->recalcMaxFD();
    }

    --mFdWriteCount;
    return true;
}

NAMESPACE_END // namespace tun
