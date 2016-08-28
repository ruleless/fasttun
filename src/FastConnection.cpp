#include "FastConnection.h"

NAMESPACE_BEG(tun)

//--------------------------------------------------------------------------
template <int MaxNum>
class ConvGen : public IDGenerator<uint32, MaxNum>
{
	typedef IDGenerator<uint32, MaxNum> IDGen;
  public:
    ConvGen() : IDGen()
	{
		static const uint32 BEGID = 100;
		for (uint32 id = BEGID; id < BEGID+MaxNum; ++id)
		{
			this->restorId(id);
		}
	}
	
    virtual ~ConvGen()
	{
		this->mAvailableIds.clear();
	}
};

static ConvGen<10000> s_convGen;
//--------------------------------------------------------------------------

FastConnection::~FastConnection()
{
	shutdown();
	delete mCache;
}

bool FastConnection::acceptConnection(int connfd)
{
	shutdown();

	uint32 conv = 0;
	if (!s_convGen.genNewId(conv))
	{
		logErrorLn("FastConnection::acceptConnection() no available convids!");
		return false;
	}

	// create a connection object on an exists socket 
	mpConnection = new Connection(mEventPoller);
	if (!mpConnection->acceptConnection(connfd))
	{
		delete mpConnection;
		mpConnection = NULL;
		s_convGen.restorId(conv);
		return false;
	}
	mpConnection->setEventHandler(this);

	// create kcp tunnel
	mbTunnelConnected = false;
	mpKcpTunnel = mpTunnelGroup->createTunnel(conv);
	if (NULL == mpKcpTunnel)
	{
		s_convGen.restorId(conv);
		return false;
	}

	mpKcpTunnel->setEventHandler(this);
	MemoryStream stream;
	stream<<conv;
	sendMessage(MsgId_CreateKcpTunnel, stream.data(), stream.length());
	
	return true;
}

bool FastConnection::connect(const char *ip, int port)
{
	struct sockaddr_in remoteAddr;
	remoteAddr.sin_family = AF_INET;
	remoteAddr.sin_port = htons(port);
	if (inet_pton(AF_INET, ip, &remoteAddr.sin_addr) < 0)
	{
		logErrorLn("FastConnection::connect()  illegal ip("<<ip<<")");
		return false;
	}

	return connect((const SA *)&remoteAddr, sizeof(remoteAddr));
}

bool FastConnection::connect(const SA *sa, socklen_t salen)
{
	shutdown();
	
	mpConnection = new Connection(mEventPoller);
	if (!mpConnection->connect(sa, salen))
	{
		delete mpConnection;
		mpConnection = NULL;
		return false;
	}
	mpConnection->setEventHandler(this);
		
	return true;
}

void FastConnection::shutdown()
{
	clearCurMsg();
	if (mpKcpTunnel)
	{
		s_convGen.restorId(mpKcpTunnel->getConv());
		mpTunnelGroup->destroyTunnel(mpKcpTunnel);
		mbTunnelConnected = false;
		mpKcpTunnel = NULL;
	}
	if (mpConnection)
	{
		mpConnection->shutdown();
		delete mpConnection;
		mpConnection = NULL;
	}
}

int FastConnection::send(const void *data, size_t datalen)
{
	if (mpKcpTunnel && mbTunnelConnected)
	{
		_flushAll();
		return mpKcpTunnel->send(data, datalen);
	}

	mCache->cache(data, datalen);
	return datalen;
}

void FastConnection::_flushAll()
{
	if (mpKcpTunnel && mbTunnelConnected && !mCache->empty())
	{
		mCache->flushAll();
	}
}

void FastConnection::flush(const void *data, size_t datalen)
{
	mpKcpTunnel->send(data, datalen);
}

void FastConnection::onConnected(Connection *pConn)
{
	if (mpHandler)
		mpHandler->onConnected(this);
}
		
void FastConnection::onDisconnected(Connection *pConn)
{
	shutdown();
	if (mpHandler)
		mpHandler->onDisconnected(this);
}

void FastConnection::onRecv(Connection *pConn, const void *data, size_t datalen)
{
	const char *pMsg = (const char *)data;
	for (;;)		
	{
		size_t leftlen = parseMessage(pMsg, datalen);
		if (MsgRcvState_Error == mMsgRcvstate)
		{
			onError(pConn);
			break;
		}
		else if (MsgRcvState_RcvComplete == mMsgRcvstate)
		{
			handleMessage(mCurMsg.data, mCurMsg.len);
			clearCurMsg();
		}

		if (leftlen > 0)
		{
			pMsg += (datalen-leftlen);
			datalen = leftlen;
		}
		else
		{
			break;
		}
	}
}

void FastConnection::onError(Connection *pConn)
{	
	sockaddr_in sa;
	socklen_t salen = sizeof(sa);
	if (pConn->getpeername((SA *)&sa, &salen))
	{
		char ip[MAX_BUF] = {0};
		if (inet_ntop(AF_INET, &sa.sin_addr, ip, sizeof(ip)))
		{
			logErrorLn("FastConnection::onError() with "<<ip<<":"<<ntohs(sa.sin_port)<<
					   " reason:"<<coreStrError());
		}		
	}

	shutdown();
	if (mpHandler)
		mpHandler->onError(this);
}

void FastConnection::onRecv(const void *data, size_t datalen)
{
	if (mpHandler)
		mpHandler->onRecv(this, data, datalen);
}

size_t FastConnection::parseMessage(const void *data, size_t datalen)
{
	const char *pMsg = (const char *)data;
	
	// 先收取消息长度
	if (MsgRcvState_NoData == mMsgRcvstate)
	{
		int copylen = sizeof(int)-mMsgLenRcvBuf.curlen;
		assert(copylen >= 0);
		copylen = min(copylen, datalen);

		if (copylen > 0)
		{
			memcpy(mMsgLenRcvBuf.buf+mMsgLenRcvBuf.curlen, pMsg, copylen);
			mMsgLenRcvBuf.curlen += copylen;
			pMsg += copylen;
			datalen -= copylen;
		}
		if (mMsgLenRcvBuf.curlen == sizeof(int)) // 消息长度已获取
		{			
			MemoryStream stream;
			stream.append(mMsgLenRcvBuf.buf, sizeof(int));
			stream>>mCurMsg.len;
			if (mCurMsg.len <= 0 || mCurMsg.len > MAX_MSG_LEN)
			{
				mMsgRcvstate = MsgRcvState_Error;
				mCurMsg.len = -1;
				return datalen;
			}
			
			mMsgRcvstate = MsgRcvState_RcvdHead;
			mRcvdMsgLen = 0;
			mCurMsg.data = (char *)malloc(mCurMsg.len);
		}
	}

	// 再收取消息内容
	if (MsgRcvState_RcvdHead == mMsgRcvstate)
	{
		int copylen = mCurMsg.len-mRcvdMsgLen;
		assert(copylen >= 0 && "copylen >= 0");
		assert(mCurMsg.data != NULL && "mCurMsg.data != NULL");
		copylen = min(copylen, datalen);

		if (copylen > 0)
		{
			memcpy(mCurMsg.data+mRcvdMsgLen, pMsg, copylen);
			mRcvdMsgLen += copylen;
			pMsg += copylen;
			datalen -= copylen;
		}
		if (mRcvdMsgLen == mCurMsg.len) // 消息体已获取
		{
			mMsgRcvstate = MsgRcvState_RcvComplete;
		}
	}

	return datalen;
}

void FastConnection::clearCurMsg()
{
	mMsgRcvstate = MsgRcvState_NoData;
	memset(&mMsgLenRcvBuf, 0, sizeof(mMsgLenRcvBuf));
	if (mCurMsg.data)
		free(mCurMsg.data);
	mRcvdMsgLen = 0;
	mCurMsg.data = NULL;
	mCurMsg.len = 0;
}

void FastConnection::handleMessage(const void *data, size_t datalen)
{
	MemoryStream stream;
	stream.append(data, datalen);
	assert(stream.length() >= sizeof(int) && "handleMessage() stream.length() > sizeof(int)");

	bool notifyKcpTunnelCreateFailed = false;
	int msgid = 0;
	stream>>msgid;	
	switch (msgid)
	{
	case MsgId_CreateKcpTunnel:
		{
			uint32 conv = 0;
			stream>>conv;
			assert(NULL == mpKcpTunnel && "FastConnection::handleMessage() NULL == mpKcpTunnel");
			mpKcpTunnel = mpTunnelGroup->createTunnel(conv);
			if (NULL == mpKcpTunnel)
			{
				logErrorLn("FastConnection::handleMessage() fail to create kcp tunnel!");
				notifyKcpTunnelCreateFailed = true;
				break;
			}
			
			mpKcpTunnel->setEventHandler(this);
			mbTunnelConnected = true;
			sendMessage(MsgId_ConfirmCreateKcpTunnel, NULL, 0);
			_flushAll();
		}
		break;
	case MsgId_ConfirmCreateKcpTunnel:
		{
			if (NULL == mpKcpTunnel)
			{
				logErrorLn("FastConnection::handleMessage() we have no kcptunnel on server!");
				notifyKcpTunnelCreateFailed = true;				
				break;
			}
			mbTunnelConnected = true;
			_flushAll();
		}
		break;
	default:
		logErrorLn("FastConnection::handleMessage() undefined message!");
		break;
	}

	if (notifyKcpTunnelCreateFailed && mpHandler)
		mpHandler->onCreateKcpTunnelFailed(this);
}

void FastConnection::sendMessage(int msgid, const void *data, size_t datalen)
{
	if (NULL == mpConnection)
	{
		logErrorLn("FastConnection::sendMessage() NULL == mpConnection");
		return;
	}
	if (!mpConnection->isConnected())
	{
		logErrorLn("FastConnection::sendMessage() mpConnection is not connected");
		return;
	}
	
	MemoryStream stream;
	int msglen = sizeof(msgid)+datalen;
	stream<<msglen;
	stream<<msgid;
	if (data != NULL && datalen > 0)
		stream.append(data, datalen);
	if (mpConnection->send(stream.data(), stream.length()) != stream.length())
	{
		logErrorLn("FastConnection::sendMessage() sentlen is not supposed!");
	}
}

NAMESPACE_END // namespace tun
