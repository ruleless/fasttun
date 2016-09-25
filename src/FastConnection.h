#ifndef __FASTCONNECTION_H__
#define __FASTCONNECTION_H__

#include "FasttunBase.h"
#include "EventPoller.h"
#include "KcpTunnel.h"
#include "Connection.h"
#include "Cache.h"
#include "MessageReceiver.h"

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
			,mMsgRcv(NULL)
			,mHeartBeatRecord()
	{
		mCache = new MyCache(this, &FastConnection::flush);
		mMsgRcv = new MsgRcv(this, &FastConnection::onRecvMsg, &FastConnection::onRecvMsgErr);
	}
	
    virtual ~FastConnection();

	bool acceptConnection(int connfd);	
	bool connect(const char *ip, int port);
	bool connect(const SA *sa, socklen_t salen);

	void shutdown();

	int send(const void *data, size_t datalen);
	void _flushAll();
	bool flush(const void *data, size_t datalen);

	void triggerHeartBeatPacket();
	const HeartBeatRecord& getHeartBeatRecord() const;
	
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
	void onRecvMsg(const void *data, uint8 datalen, void *user);
	void onRecvMsgErr(void *user);
	
	void sendMessage(int msgid, const void *data, size_t datalen);	
	
  private:	
	enum
	{
		MsgId_CreateKcpTunnel = 0,
		MsgId_ConfirmCreateKcpTunnel,
		MsgId_HeartBeat_Request,
		MsgId_HeartBeat_Response,
	};
	
	typedef Cache<FastConnection> MyCache;
	typedef msg::MessageReceiver<FastConnection, 64, uint8> MsgRcv;
	
	EventPoller *mEventPoller;
	ITunnelGroup *mpTunnelGroup;

	Connection *mpConnection;
	ITunnel *mpKcpTunnel;
	bool mbTunnelConnected;
	
	Handler *mpHandler;

	MyCache *mCache;

	MsgRcv *mMsgRcv;

	HeartBeatRecord mHeartBeatRecord;
};

NAMESPACE_END // namespace tun

#endif // __FASTCONNECTION_H__
