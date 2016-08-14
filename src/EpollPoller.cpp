#include "EpollPoller.h"

#ifdef HAS_EPOLL

#include <sys/epoll.h>

NAMESPACE_BEG(tun)

EpollPoller::EpollPoller(int expectedSize)
{
	mEpfd = epoll_create(expectedSize);
	if (mEpfd == -1)
	{
		logErrorLn("EpollPoller::EpollPoller()  epoll_create failed  err:"<<coreStrError());		
	}
}

EpollPoller::~EpollPoller()
{
	if (mEpfd != -1)
	{
		close(mEpfd);
	}
}

int EpollPoller::processPendingEvents(double maxWait)
{
	const int MAX_EVENTS = 10;
	struct epoll_event events[MAX_EVENTS];
	int maxWaitInMilliseconds = int(ceil(maxWait * 1000));
	
	uint64 startTime = getTimeStamp();
	int nfds = epoll_wait(mEpfd, events, MAX_EVENTS, maxWaitInMilliseconds);
	mSpareTime += getTimeStamp() - startTime;

	for (int i = 0; i < nfds; ++i)
	{
		if (events[i].events & (EPOLLERR|EPOLLHUP))
		{
			this->triggerError(events[i].data.fd);
		}
		else
		{
			if (events[i].events & EPOLLIN)
			{
				this->triggerRead(events[i].data.fd);
			}

			if (events[i].events & EPOLLOUT)
			{
				this->triggerWrite(events[i].data.fd);
			}
		}
	}

	return nfds;
}

bool EpollPoller::doRegister(int fd, bool isRead, bool isRegister)
{
	struct epoll_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.data.fd = fd;

	int op;
	if (this->isRegistered(fd, !isRead))
	{
		op = EPOLL_CTL_MOD;
		ev.events = isRegister ? (EPOLLIN | EPOLLOUT) : (isRead ? EPOLLOUT : EPOLLIN);
	}
	else
	{
		op = isRegister ? EPOLL_CTL_ADD : EPOLL_CTL_DEL;
		ev.events = isRead ? EPOLLIN : EPOLLOUT;
	}

	if (epoll_ctl(mEpfd, op, fd, &ev) < 0)
	{
		char errMsg[MAX_BUF] = {0};
		snprintf(errMsg, sizeof(errMsg),
				 "EpollPoller::doRegister()  Failed to %s %s file descriptor %d (%s)",
				 (isRegister ? "add" : "remove"),
				 (isRead ? "read" : "write"),
				 fd,
				 coreStrError());
		logErrorLn(errMsg);

		return false;
	}

	return true;
}

NAMESPACE_END // namespace tun

#endif // HAS_EPOLL
