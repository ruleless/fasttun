#ifndef __CONNECTION_H__
#define __CONNECTION_H__

#include "FasttunBase.h"
#include "EventPoller.h"

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
	{
		memset(&mPeerAddr, 0, sizeof(mPeerAddr));
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

	inline bool isConnected() const
	{
		return mConnStatus == ConnStatus_Connected;
	}

	const char* getPeerIp(char *ip, int iplen) const;
	int getPeerPort() const;	

	// InputNotificationHandler
	virtual int handleInputNotification(int fd);

	// OutputNotificationHandler
	virtual int handleOutputNotification(int fd);
	
  private:
	int mFd;
	EConnStatus mConnStatus;
	sockaddr_in mPeerAddr;
	Handler *mHandler; 

	EventPoller *mEventPoller;
	bool mbRegForRead;
	bool mbRegForWrite;
};

NAMESPACE_END // namespace tun 

#endif // __CONNECTION_H__
