#include "EventPoller.h"
#include "SelectPoller.h"
#include "EpollPoller.h"

EventPoller::EventPoller()
		:mSpareTime(0)
		,mFdReadHandlers()
		,mFdWriteHandlers()		
{
}

EventPoller::~EventPoller()
{
}

bool EventPoller::registerForRead(int fd, InputNotificationHandler *handler)
{
	if (!this->doRegisterForRead(fd))
	{
		return false;
	}

	mFdReadHandlers[fd] = handler;

	return true;
}

bool EventPoller::registerForWrite(int fd, OutputNotificationHandler *handler)
{
	if (!this->doRegisterForWrite(fd))
	{
		return false;
	}

	mFdWriteHandlers[fd] = handler;

	return true;
}

bool EventPoller::deregisterForRead(int fd)
{
	mFdReadHandlers.erase(fd);

	return this->doDeregisterForRead(fd);
}

bool EventPoller::deregisterForWrite(int fd)
{
	mFdWriteHandlers.erase(fd);

	return this->doDeregisterForWrite(fd);
}

bool EventPoller::triggerRead(int fd)	
{
	FDReadHandlers::iterator iter = mFdReadHandlers.find(fd);

	if (iter == mFdReadHandlers.end())
	{
		return false;
	}

	iter->second->handleInputNotification(fd);

	return true;
}

bool EventPoller::triggerWrite(int fd)	
{
	FDWriteHandlers::iterator iter = mFdWriteHandlers.find(fd);

	if (iter == mFdWriteHandlers.end())
	{
		return false;
	}

	iter->second->handleOutputNotification(fd);

	return true;
}

bool EventPoller::triggerError(int fd)
{
	if (!this->triggerRead(fd))
	{
		return this->triggerWrite(fd);
	}

	return true;
}

bool EventPoller::isRegistered(int fd, bool isForRead) const
{
	return isForRead ? (mFdReadHandlers.find(fd) != mFdReadHandlers.end()) : 
			(mFdWriteHandlers.find(fd) != mFdWriteHandlers.end());
}

int EventPoller::getFileDescriptor() const
{
	return -1;
}

InputNotificationHandler* EventPoller::findForRead(int fd)
{
	FDReadHandlers::iterator iter = mFdReadHandlers.find(fd);
	
	if(iter == mFdReadHandlers.end())
		return NULL;

	return iter->second;
}

OutputNotificationHandler* EventPoller::findForWrite(int fd)
{
	FDWriteHandlers::iterator iter = mFdWriteHandlers.find(fd);
	
	if(iter == mFdWriteHandlers.end())
		return NULL;

	return iter->second;
}

int EventPoller::recalcMaxFD() const
{
	int readMaxFD = -1;

	FDReadHandlers::const_iterator iFDReadHandler = mFdReadHandlers.begin();
	while (iFDReadHandler != mFdReadHandlers.end())
	{
		if (iFDReadHandler->first > readMaxFD)
		{
			readMaxFD = iFDReadHandler->first;
		}

		++iFDReadHandler;
	}

	int writeMaxFD = -1;

	FDWriteHandlers::const_iterator iFDWriteHandler = mFdWriteHandlers.begin();
	while (iFDWriteHandler != mFdWriteHandlers.end())
	{
		if (iFDWriteHandler->first > writeMaxFD)
		{
			writeMaxFD = iFDWriteHandler->first;
		}

		++iFDWriteHandler;
	}

	return max(readMaxFD, writeMaxFD);
}

EventPoller *EventPoller::create()
{
#ifdef HAS_EPOLL
	return new EpollPoller();
#else
	return new SelectPoller();
#endif
}
