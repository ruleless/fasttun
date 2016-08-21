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
		for (uint32 id = 100; id < id+MaxNum; ++id)
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
{}

bool FastConnection::acceptConnection(int connfd)
{
	shutdown();

	mpConnection = new Connection(mEventPoller);
	if (!mpConnection->acceptConnection(connfd))
	{
		delete mpConnection;
		mpConnection = NULL;
		return false;
	}
	mpConnection->setEventHandler(this);
	
	return true;
}

bool FastConnection::connect(const char *ip, int port)
{
	shutdown();
	
	mpConnection = new Connection(mEventPoller);
	if (!mpConnection->connect(ip, port))
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
		mpKcpTunnel->shutdown();
		delete mpKcpTunnel;
		mpKcpTunnel = NULL;
	}
	if (mpConnection)
	{
		mpConnection->shutdown();
		delete mpConnection;
		mpConnection = NULL;
	}
}

void FastConnection::onConnected(Connection *pConn)
{}
		
void FastConnection::onDisconnected(Connection *pConn)
{
	char ip[MAX_BUF] = {0};
	if (pConn->getPeerIp(ip, sizeof(ip)))
	{
		logErrorLn("FastConnection::onDisconnected() peer ip:"<<ip);
	}
	
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
			shutdown();
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
	char ip[MAX_BUF] = {0};
	if (pConn->getPeerIp(ip, sizeof(ip)))
	{
		logErrorLn("FastConnection::onError() peer ip"<<ip);
	}
	
	shutdown();
	if (mpHandler)
		mpHandler->onError(this);
}

void FastConnection::onRecv(KcpTunnel *pTunnel, const void *data, size_t datelen)
{	
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
			memcpy(mCurMsg.data, pMsg, copylen);
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
	assert(stream.length() > sizeof(int) && "handleMessage() stream.length() > sizeof(int)");

	int msgid = 0;
	stream>>msgid;
	switch (msgid)
	{
		
	}
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
	int msglen = sizeof(msgid)+sizeof(datalen);
	stream<<msglen;
	stream<<msgid;
	stream.append(data, datalen);
	if (mpConnection->send(stream.data(), stream.length()) != stream.length())
	{
		logErrorLn("FastConnection::sendMessage() sentlen is not supposed!");
	}
}

NAMESPACE_END // namespace tun
