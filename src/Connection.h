#ifndef __CONNECTION_H__
#define __CONNECTION_H__

#include "FasttunBase.h"
#include "EventPoller.h"

NAMESPACE_BEG(tun)

class Connection : public InputNotificationHandler
{
  public:
	struct Handler
	{
		virtual void onConnected(Connection *pConn) {}
		virtual void onDisconnected(Connection *pConn) {}

		virtual void onRecv(Connection *pConn, const void *data, size_t datelen) = 0;
		virtual void onError(Connection *pConn);
		
	};
	
    Connection(EventPoller *poller)
			:mFd(-1)
			,mHandler(NULL)
			,mEventPoller(poller)
	{
		assert(mEventPoller && "Connection::mEventPoller != NULL");
	}
	
    virtual ~Connection();

	bool acceptConnection(int connfd);
	bool connect(const char *ip, int port);

	void shutdown();

	int send(const void *data, size_t datalen);

	inline int getSockFd() const
	{
		return mFd;
	}

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

#endif // __CONNECTION_H__
