#ifndef __EVENTPOLLOER_H__
#define __EVENTPOLLOER_H__

#include "common.h"
#include "timestamp.h"
#include "Network.h"
#include "NetworkDef.h"
#include "concurrency.h"
#include <map>

#ifndef _WIN32
#define HAS_EPOLL
#endif

class InputNotificationHandler;

class EventPoller
{
  public:
	EventPoller();
	virtual ~EventPoller();

	static EventPoller* create();

	bool registerForRead(int fd, InputNotificationHandler *handler);
	bool registerForWrite(int fd, OutputNotificationHandler *handler);

	bool deregisterForRead(int fd);
	bool deregisterForWrite(int fd);

	virtual int processPendingEvents(double maxWait) = 0;
	virtual int getFileDescriptor() const;
	
	void clearSpareTime()
	{
		mSpareTime = 0;
	}
	uint64 spareTime() const
	{
		return mSpareTime;
	}

	InputNotificationHandler* findForRead(int fd);
	OutputNotificationHandler* findForWrite(int fd);
  protected:
	virtual bool doRegisterForRead(int fd) = 0;
	virtual bool doRegisterForWrite(int fd) = 0;

	virtual bool doDeregisterForRead(int fd) = 0;
	virtual bool doDeregisterForWrite(int fd) = 0;

	bool triggerRead(int fd);
	bool triggerWrite(int fd);
	bool triggerError(int fd);
	
	bool isRegistered(int fd, bool isForRead) const;

	int recalcMaxFD() const;
  protected:
	uint64 mSpareTime;
  private:
	typedef std::map<int, InputNotificationHandler *> FDReadHandlers;
	typedef std::map<int, OutputNotificationHandler *> FDWriteHandlers;
	
	FDReadHandlers mFdReadHandlers;
	FDWriteHandlers mFdWriteHandlers;
};

#endif
