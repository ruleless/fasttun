NAMESPACE_BEG(tun)

//--------------------------------------------------------------------------
static int kcpOutput(const char *buf, int len, ikcpcb *kcp, void *user)
{
	ITunnel *pTunnel = (ITunnel *)user;
	if (pTunnel)
	{
		assert(pTunnel->_output(buf, len) == len && "kcp outputed len illegal");
	}
	return 0;
}

template <bool IsServer>
KcpTunnel<IsServer>::~KcpTunnel()
{
	shutdown();
}

template <bool IsServer>
bool KcpTunnel<IsServer>::create(uint32 conv, const KcpArg &arg)
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

template <bool IsServer>
void KcpTunnel<IsServer>::shutdown()
{
	if (mKcpCb)
	{
		ikcp_release(mKcpCb);
		mKcpCb = NULL;
	}
}

template <bool IsServer>
int KcpTunnel<IsServer>::send(const void *data, size_t datalen)
{
	return ikcp_send(mKcpCb, (const char *)data, datalen);
}

template <bool IsServer>
bool KcpTunnel<IsServer>::input(const void *data, size_t datalen)
{
	int ret = ikcp_input(mKcpCb, (const char *)data, datalen);
	return 0 == ret;
}

template <bool IsServer>
uint32 KcpTunnel<IsServer>::update(uint32 current)
{
	ikcp_update(mKcpCb, current);	

	int datalen = ikcp_peeksize(mKcpCb);
	if (datalen > 0)
	{
		char *buf = (char *)malloc(datalen);
		assert(buf != NULL && "ikcp_recv() malloc failed!");
		assert(ikcp_recv(mKcpCb, buf, datalen) == datalen);

		if (mHandler)
			mHandler->onRecv(buf, datalen);
		free(buf);
	}

	uint32 nextCallTime = ikcp_check(mKcpCb, current);
	return nextCallTime > current ? nextCallTime - current : 0;
}
//--------------------------------------------------------------------------

//--------------------------------------------------------------------------
template <bool IsServer>
KcpTunnelGroup<IsServer>::~KcpTunnelGroup()
{
}

template <bool IsServer>
bool KcpTunnelGroup<IsServer>::create(const char *addr)
{
	if (!Supper::create(addr))
	{
		return false;
	}   	

	return this->_create();
}

template <bool IsServer>
bool KcpTunnelGroup<IsServer>::create(const SA* sa, socklen_t salen)
{
	if (!Supper::create(sa, salen))
	{
		return false;
	}

	return this->_create();
}

template <bool IsServer>
bool KcpTunnelGroup<IsServer>::_create()
{
	// register for event
	if (!mEventPoller->registerForRead(this->mFd, this))
	{
		logErrorLn("KcpTunnelGroup::create() register error!");
		return false;
	}
	return true;
}

template <bool IsServer>
void KcpTunnelGroup<IsServer>::shutdown()
{
	typename Tunnels::iterator it = this->mTunnels.begin();
	for (; it != this->mTunnels.end(); ++it)
	{
		Tun *pTunnel = it->second;
		if (pTunnel)
		{
			pTunnel->shutdown();
			delete pTunnel;
		}
	}
	this->mTunnels.clear();
	
	if (this->mFd >= 0)
	{
		mEventPoller->deregisterForRead(this->mFd);
		close(this->mFd);
		this->mFd = -1;
	}
}

template <bool IsServer>
ITunnel* KcpTunnelGroup<IsServer>::createTunnel(uint32 conv)
{
	typename Tunnels::iterator it = this->mTunnels.find(conv);
	if (it != this->mTunnels.end())
	{
		logErrorLn("KcpTunnelGroup::createTunnel() tunnul already exist! conv="<<conv);
		return NULL;
	}
	
	Tun *pTunnel = new Tun(this);
	if (!pTunnel->create(conv, mKcpArg))
	{
		return NULL;
	}

	this->mTunnels.insert(std::pair<uint32, Tun *>(conv, pTunnel));
	return pTunnel;
}

template <bool IsServer>
void KcpTunnelGroup<IsServer>::destroyTunnel(ITunnel *pTunnel)
{	
	uint32 conv = pTunnel->getConv();
	
	typename Tunnels::iterator it = this->mTunnels.find(conv);
	if (it != this->mTunnels.end())
	{
		this->mTunnels.erase(it);
	}

	static_cast<Tun *>(pTunnel)->shutdown();
	delete pTunnel;
}

template <bool IsServer>
uint32 KcpTunnelGroup<IsServer>::update()
{
	// update all tunnels
	uint32 current = core::getTickCount();
	uint32 maxWait = 0xFFFFFFFF;
	typename Tunnels::iterator it = this->mTunnels.begin();
	for (; it != this->mTunnels.end(); ++it)
	{
		Tun *pTunnel = it->second;
		if (pTunnel)
		{			
			maxWait = min(maxWait, pTunnel->update(current));
		}
	}
	return maxWait;
}

template <bool IsServer>
int KcpTunnelGroup<IsServer>::handleInputNotification(int fd)
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
		typename Tunnels::iterator it = mTunnels.begin();
		for (; it != mTunnels.end(); ++it)
		{
			Tun *pTunnel = it->second;
			if (pTunnel && pTunnel->input(buf, recvlen))
			{
				pTunnel->onRecvPeerAddr((const SA *)&addr, addrlen);
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
	return 0;
}
//--------------------------------------------------------------------------

NAMESPACE_END // namespace tun
