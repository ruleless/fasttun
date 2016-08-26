#include "KcpTunnel.h"

NAMESPACE_BEG(tun)

//--------------------------------------------------------------------------
static int kcpOutput(const char *buf, int len, ikcpcb *kcp, void *user);

KcpTunnel::~KcpTunnel()
{
	shutdown();
	delete mCache;
}

bool KcpTunnel::create(uint32 conv, const KcpArg &arg)
{
	if (mKcpCb)
		shutdown();

	mConv = conv;
	mKcpCb = ikcp_create(mConv, this);
	if (NULL == mKcpCb)
		return false;

	mKcpCb->output = kcpOutput;
	ikcp_nodelay(mKcpCb, arg.nodelay, arg.interval, arg.resend, arg.nc);
	ikcp_setmtu(mKcpCb, arg.mtu);	
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

uint32 KcpTunnel::update(uint32 current)
{
	ikcp_update(mKcpCb, current);	

	int datalen = ikcp_peeksize(mKcpCb);
	if (datalen > 0)
	{
		char *buf = (char *)malloc(datalen);
		assert(buf != NULL && "ikcp_recv() malloc failed!");
		assert(ikcp_recv(mKcpCb, buf, datalen) == datalen);

		if (mHandler)
			mHandler->onRecv(this, buf, datalen);
		free(buf);
	}

	uint32 nextCallTime = ikcp_check(mKcpCb, current);
	return nextCallTime > current ? nextCallTime - current : 0;
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
			return sendto(mGroup->_getSockFd(), data, datalen, 0, (SA *)&mRemoteAddr, sizeof(mRemoteAddr));
		}
		else
		{
			mCache->cache(data, datalen);
			return datalen;
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
	sendto(mGroup->_getSockFd(), data, datalen, 0, (SA *)&mRemoteAddr, sizeof(mRemoteAddr));
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

	if (localaddr)
	{
		// assign the local address
		sockaddr_in addr;
		if (!core::getSocketAddress(localaddr, addr))
		{
			logErrorLn("KcpTunnelGroup::create() localaddr format error! "<<localaddr);
			return false;
		}

		// bind local address
		if (bind(mFd, (SA *)&addr, sizeof(addr)) < 0)
		{
			logErrorLn("KcpTunnelGroup::create() bind local address err! "<<coreStrError());
			return false;
		}
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
	if (!pTunnel->create(conv, mKcpArg))
	{
		return NULL;
	}

	mTunnels.insert(std::pair<uint32, KcpTunnel *>(conv, pTunnel));
	return pTunnel;
}

void KcpTunnelGroup::destroyTunnel(KcpTunnel *pTunnel)
{	
	uint32 conv = pTunnel->getConv();
	
	Tunnels::iterator it = mTunnels.find(conv);
	if (it != mTunnels.end())
	{
		mTunnels.erase(it);
	}

	pTunnel->shutdown();
	delete pTunnel;
}

uint32 KcpTunnelGroup::update()
{
	// update all tunnels
	uint32 current = core::getTickCount();
	uint32 waitTime = 0xFFFFFFFF;
	Tunnels::iterator it = mTunnels.begin();
	for (; it != mTunnels.end(); ++it)
	{
		KcpTunnel *pTunnel = it->second;
		if (pTunnel)
		{			
			waitTime = min(waitTime, pTunnel->update(current));
		}
	}
	return waitTime;
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

	// input to kcp
	if (recvlen > 0)
	{
		bool bAccepted = false;
		Tunnels::iterator it = mTunnels.begin();
		for (; it != mTunnels.end(); ++it)
		{
			KcpTunnel *pTunnel = it->second;
			if (pTunnel && pTunnel->input(buf, recvlen))
			{
				if (!assignedRemoteAddr())
					pTunnel->setRemoteAddr((SA *)&addr, addrlen);
				bAccepted = true;
				break;
			}
		}

		if (!bAccepted)
		{
			logErrorLn("KcpTunnel() got a stream that has no acceptor! datalen="<<recvlen);
		}
	}
	free(buf);   	
}
//--------------------------------------------------------------------------

NAMESPACE_END // namespace tun
