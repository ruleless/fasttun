#include "KcpTunnel.h"

NAMESPACE_BEG(tun)

//--------------------------------------------------------------------------
static int kcpOutput(const char *buf, int len, ikcpcb *kcp, void *user);

KcpTunnel::~KcpTunnel()
{
	shutdown();
	delete mCache;
}

bool KcpTunnel::create(uint32 conv)
{
	if (mKcpCb)
		shutdown();

	mConv = conv;
	mKcpCb = ikcp_create(mConv, this);
	if (NULL == mKcpCb)
		return false;

	ikcp_setmtu(mKcpCb, 4096);
	mKcpCb->output = kcpOutput;
	return true;
}

void KcpTunnel::shutdown()
{
	if (mKcpCb)
	{
		ikcp_release(mKcpCb);
		mKcpCb = NULL;
	}
}

int KcpTunnel::send(const void *data, size_t datalen)
{
	return ikcp_send(mKcpCb, (const char *)data, datalen);
}

bool KcpTunnel::input(const void *data, size_t datalen)
{
	int ret = ikcp_input(mKcpCb, (const char *)data, datalen);
	return 0 == ret;
}

void KcpTunnel::update(uint32 current)
{
	ikcp_update(mKcpCb, current);

	int datalen = ikcp_peeksize(mKcpCb);
	char *buf = NULL;
	if (datalen > 0)
	{
		buf = (char *)malloc(datalen);
		assert(ikcp_recv(mKcpCb, buf, datalen) == datalen);
	}	

	if (datalen > 0)
	{
		if (mHandler)
			mHandler->onRecv(this, buf, datalen);
		free(buf);
	}	
}

int KcpTunnel::_output(const void *data, size_t datalen)
{
	if (mGroup->assignedRemoteAddr())
	{
		return mGroup->_output(data, datalen);
	}
	else
	{
		if (assignedRemoteAddr())
		{
			_flushAll();
			return sendto(mGroup->getSockFd(), data, datalen, 0, (SA *)&mRemoteAddr, sizeof(mRemoteAddr));
		}
		else
		{
			mCache->cache(data, datalen);
		}
	}
}

void KcpTunnel::_flushAll()
{
	if (assignedRemoteAddr())
	{
		mCache->flushAll();
	}
}

void KcpTunnel::flush(const void *data, size_t datalen)
{
	sendto(mGroup->getSockFd(), data, datalen, 0, (SA *)&mRemoteAddr, sizeof(mRemoteAddr));
}

static int kcpOutput(const char *buf, int len, ikcpcb *kcp, void *user)
{
	KcpTunnel *pTunnel = (KcpTunnel *)user;
	if (pTunnel)
	{
		assert(pTunnel->_output(buf, len) == len && "kcp outputed len illegal");		
	}
	return 0;
}
//--------------------------------------------------------------------------

//--------------------------------------------------------------------------
KcpTunnelGroup::~KcpTunnelGroup()
{
}

bool KcpTunnelGroup::create(const char *localaddr, const char *remoteaddr)
{
	// assign the local address
	if (!core::getSocketAddress(localaddr, mLocalAddr))
	{
		logErrorLn("KcpTunnelGroup::create() localaddr format error! "<<localaddr);
		return false;
	}

	// assign the remote address
	if (remoteaddr)
	{
		mbAssignedRemoteAddr = true;
		if (!core::getSocketAddress(remoteaddr, mRemoteAddr))
		{
			logErrorLn("KcpTunnelGroup::create() remoteaddr format error! "<<remoteaddr);
			return false;
		}
	}
	else
	{
		mbAssignedRemoteAddr = false;
	}	

	// create socket
	mFd = socket(AF_INET, SOCK_DGRAM, 0);
	if (mFd < 0)
		return false;

	// set nonblocking
	if (!core::setNonblocking(mFd))
		return false;

	// bind local address
	if (bind(mFd, (SA *)&mLocalAddr, sizeof(mLocalAddr)) < 0)
	{
		logErrorLn("KcpTunnelGroup::create() bind local address err! "<<coreStrError());
		return false;
	}

	// register for event
	if (!mEventPoller->registerForRead(mFd, this))
	{
		logErrorLn("KcpTunnelGroup::create() register error!");
		return false;
	}

	return true;
}

void KcpTunnelGroup::shutdown()
{
	Tunnels::iterator it = mTunnels.begin();
	for (; it != mTunnels.end(); ++it)
	{
		KcpTunnel *pTunnel = it->second;
		if (pTunnel)
		{
			pTunnel->shutdown();
			delete pTunnel;
		}
	}
	mTunnels.clear();
	
	if (mFd >= 0)
	{
		mEventPoller->deregisterForRead(mFd);
		close(mFd);
		mFd = -1;
	}
}

int KcpTunnelGroup::_output(const void *data, size_t datalen)
{
	assert(mbAssignedRemoteAddr && "KcpTunnelGroup::_output no remote address");
	return sendto(mFd, data, datalen, 0, (SA *)&mRemoteAddr, sizeof(mRemoteAddr));
}

KcpTunnel* KcpTunnelGroup::createTunnel(uint32 conv)
{
	Tunnels::iterator it = mTunnels.find(conv);
	if (it != mTunnels.end())
	{
		logErrorLn("KcpTunnelGroup::createTunnel() tunnul already exist! conv="<<conv);
		return NULL;
	}
	
	KcpTunnel *pTunnel = new KcpTunnel(this);
	if (!pTunnel->create(conv))
	{
		return NULL;
	}

	mTunnels.insert(std::pair<uint32, KcpTunnel *>(conv, pTunnel));
	return pTunnel;
}

void KcpTunnelGroup::destroyTunnel(KcpTunnel *pTunnel)
{	
	uint32 conv = pTunnel->getConv();

	pTunnel->shutdown();
	Tunnels::iterator it = mTunnels.find(conv);
	if (it != mTunnels.end())
	{
		mTunnels.erase(it);
	}
}

void KcpTunnelGroup::update()
{   
	// update all tunnels
	uint32 current = core::getTickCount();
	Tunnels::iterator it = mTunnels.begin();
	for (; it != mTunnels.end(); ++it)
	{
		KcpTunnel *pTunnel = it->second;
		if (pTunnel)
			pTunnel->update(current);
	}
}

int KcpTunnelGroup::handleInputNotification(int fd)
{
	// recv data from internet
	int maxlen = mKcpArg.mtu;
	char *buf = (char *)malloc(maxlen);
	assert(buf != NULL && "udp recv! malloc failed!");

	sockaddr_in addr;
	socklen_t addrlen = sizeof(addr);
	int recvlen = recvfrom(fd, buf, maxlen, 0, (SA *)&addr, &addrlen);
	if (recvlen > 0)
	{
		bool bAccepted = false;
		Tunnels::iterator it = mTunnels.begin();
		for (; it != mTunnels.end(); ++it)
		{
			KcpTunnel *pTunnel = it->second;
			if (pTunnel && pTunnel->input(buf, recvlen))
			{
				pTunnel->setRemoteAddr((SA *)&addr, addrlen);
				bAccepted = true;
				break;
			}
		}

		if (!bAccepted)
		{
			logErrorLn("KcpTunnel() got a stream that has no acceptor! datalen="<<recvlen);
			// buf.hexlike();
			// buf.textlike();
		}
	}
	free(buf);   	
}
//--------------------------------------------------------------------------

NAMESPACE_END // namespace tun
