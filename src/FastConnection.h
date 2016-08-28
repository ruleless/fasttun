#ifndef __FASTCONNECTION_H__
#define __FASTCONNECTION_H__

#include "FasttunBase.h"
#include "EventPoller.h"
#include "KcpTunnel.h"
#include "Connection.h"
#include "Cache.h"

NAMESPACE_BEG(tun)

class FastConnection : public Connection::Handler, public KcpTunnelHandler
{
  public:
	class Handler
	{
	  public:
		Handler() {}

		virtual void onConnected(FastConnection *pConn) {}
		virtual void onDisconnected(FastConnection *pConn) {}
		virtual void onError(FastConnection *pConn) {}
		
		virtual void onCreateKcpTunnelFailed(FastConnection *pConn) {}

		virtual void onRecv(FastConnection *pConn, const void *data, size_t datalen) {}
	};
	
    FastConnection(EventPoller *poller, ITunnelGroup *pGroup)
			:mEventPoller(poller)
			,mpTunnelGroup(pGroup)
			,mpConnection(NULL)
			,mpKcpTunnel(NULL)
			,mbTunnelConnected(false)
			,mpHandler(NULL)
			,mCache(NULL)
			,mMsgRcvstate(MsgRcvState_NoData)
			,mRcvdMsgLen(0)
	{
		memset(&mMsgLenRcvBuf, 0, sizeof(mMsgLenRcvBuf));
		memset(&mCurMsg, 0, sizeof(mCurMsg));
		mCache = new MyCache(this);
	}
	
    virtual ~FastConnection();

	bool acceptConnection(int connfd);	
	bool connect(const char *ip, int port);
	bool connect(const SA *sa, socklen_t salen);

	void shutdown();

	int send(const void *data, size_t datalen);
	void _flushAll();
	void flush(const void *data, size_t datalen);
	
	// Connection::Handler
	virtual void onConnected(Connection *pConn);
	virtual void onDisconnected(Connection *pConn);

	virtual void onRecv(Connection *pConn, const void *data, size_t datalen);
	virtual void onError(Connection *pConn);

	// KcpTunnel::Handler
	virtual void onRecv(const void *data, size_t datalen);

	inline void setEventHandler(Handler *h)
	{
		mpHandler = h;
	}	

	inline Connection* getConnection() const
	{
		return mpConnection;
	}

	inline ITunnel* getKcpTunnel() const
	{
		return mpKcpTunnel;
	}

	inline bool isConnected() const
	{
		if (mpConnection)
			return mpConnection->isConnected();
		return false;
	}

  private:
	// return left data size
	size_t parseMessage(const void *data, size_t datalen);
	void clearCurMsg();

	void handleMessage(const void *data, size_t datalen);
	void sendMessage(int msgid, const void *data, size_t datalen);
	
  private:
	enum EMsgRcvState
	{
		MsgRcvState_NoData,
		MsgRcvState_Error,
		MsgRcvState_RcvdHead,
		MsgRcvState_RcvComplete,
	};

	struct MessageLenBuf
	{
		int curlen;
		char buf[sizeof(int)];
	};
	struct Message
	{
		int len;
		char *data;		
	};	

	enum
	{
		MsgId_CreateKcpTunnel = 0,
		MsgId_ConfirmCreateKcpTunnel,
	};
	
	static const int MAX_MSG_LEN = 65535;
	typedef Cache<FastConnection> MyCache;
	
	EventPoller *mEventPoller;
	ITunnelGroup *mpTunnelGroup;

	Connection *mpConnection;
	ITunnel *mpKcpTunnel;
	bool mbTunnelConnected;
	
	Handler *mpHandler;

	MyCache *mCache;
	
	EMsgRcvState mMsgRcvstate;
	MessageLenBuf mMsgLenRcvBuf;
	int mRcvdMsgLen;
	Message mCurMsg;
};

NAMESPACE_END // namespace tun

#endif // __FASTCONNECTION_H__
