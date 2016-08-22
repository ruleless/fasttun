#ifndef __FASTCONNECTION_H__
#define __FASTCONNECTION_H__

#include "FasttunBase.h"
#include "EventPoller.h"
#include "KcpTunnel.h"
#include "Connection.h"

NAMESPACE_BEG(tun)

class FastConnection : public Connection::Handler, public KcpTunnel::Handler
{
  public:
	class Handler
	{
	  public:
		Handler() {}

		virtual void onDisconnected(FastConnection *pConn) {}
		virtual void onError(FastConnection *pConn) {}
		
		virtual void onCreateKcpTunnelFailed(FastConnection *pConn) {}

		virtual void onRecv(FastConnection *pConn, const void *data, size_t datalen) {}
	};
	
    FastConnection(EventPoller *poller, KcpTunnelGroup *pGroup)
			:mEventPoller(poller)
			,mpTunnelGroup(pGroup)
			,mpConnection(NULL)
			,mpKcpTunnel(NULL)
			,mbTunnelConnected(false)
			,mpHandler(NULL)
			,mRemainData()
			,mMsgRcvstate(MsgRcvState_NoData)
			,mRcvdMsgLen(0)
	{
		memset(&mMsgLenRcvBuf, 0, sizeof(mMsgLenRcvBuf));
		memset(&mCurMsg, 0, sizeof(mCurMsg));
	}
	
    virtual ~FastConnection();

	bool acceptConnection(int connfd);
	bool connect(const char *ip, int port);

	void shutdown();

	int send(const void *data, size_t datalen);
	void _flushAll();
	
	// Connection::Handler
	virtual void onConnected(Connection *pConn);
	virtual void onDisconnected(Connection *pConn);

	virtual void onRecv(Connection *pConn, const void *data, size_t datalen);
	virtual void onError(Connection *pConn);

	// KcpTunnel::Handler
	virtual void onRecv(KcpTunnel *pTunnel, const void *data, size_t datalen);

	inline void setEventHandler(Handler *h)
	{
		mpHandler = h;
	}

	inline int getSockFd() const
	{
		if (mpConnection)
			return mpConnection->getSockFd();
		return -1;
	}

	inline Connection* getConnection() const
	{
		return mpConnection;
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

	struct Data
	{
		size_t datalen;
		char *data;
	};

	enum
	{
		MsgId_CreateKcpTunnel = 0,
		MsgId_ConfirmCreateKcpTunnel,
	};
	
	static const int MAX_MSG_LEN = 65535;
	typedef std::list<Data> DataList;
	
	EventPoller *mEventPoller;
	KcpTunnelGroup *mpTunnelGroup;

	Connection *mpConnection;
	KcpTunnel *mpKcpTunnel;
	bool mbTunnelConnected;
	
	Handler *mpHandler;

	DataList mRemainData;
	
	EMsgRcvState mMsgRcvstate;
	MessageLenBuf mMsgLenRcvBuf;
	int mRcvdMsgLen;
	Message mCurMsg;
};

NAMESPACE_END // namespace tun

#endif // __FASTCONNECTION_H__
