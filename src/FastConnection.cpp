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
	delete mMsgRcv;
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
	mMsgRcv->clear();
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

void FastConnection::triggerHeartBeatPacket()
{
	mHeartBeatRecord.packetSentTime = core::getClock();
	sendMessage(MsgId_HeartBeat_Request, NULL, 0);
}

const HeartBeatRecord& FastConnection::getHeartBeatRecord() const
{
	return mHeartBeatRecord;
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
	mMsgRcv->input(data, datalen, pConn);
}

void FastConnection::onError(Connection *pConn)
{
	shutdown();
	if (mpHandler)
		mpHandler->onError(this);
}

void FastConnection::onRecv(const void *data, size_t datalen)
{
	if (mpHandler)
		mpHandler->onRecv(this, data, datalen);
}

void FastConnection::onRecvMsg(const void *data, uint8 datalen, void *user)
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
	case MsgId_HeartBeat_Request:
		sendMessage(MsgId_HeartBeat_Response, NULL, 0);
		break;
	case MsgId_HeartBeat_Response:
		mHeartBeatRecord.packetRecvTime = core::getClock();
		break;
	default:
		logErrorLn("FastConnection::handleMessage() undefined message!");
		break;
	}

	if (notifyKcpTunnelCreateFailed && mpHandler)
		mpHandler->onCreateKcpTunnelFailed(this);
}

void FastConnection::onRecvMsgErr(void *user)
{
	onError((Connection *)user);
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
	uint8 msglen = sizeof(msgid)+datalen;
	stream<<msglen;
	stream<<msgid;
	if (data != NULL && datalen > 0)
		stream.append(data, datalen);
	mpConnection->send(stream.data(), stream.length());
}

NAMESPACE_END // namespace tun
