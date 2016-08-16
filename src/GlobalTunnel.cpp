#include "GlobalTunnel.h"

NAMESPACE_BEG(tun)

GlobalTunnel::~GlobalTunnel()
{}

bool GlobalTunnel::initialise()
{
	mTun = mpGroupTun->createTunnel(GLOBAL_TUN_ID);	
	if (NULL == mTun)
	{
		logErrorLn("GlobalTunnel::initialise() createTunnel failed!");
		return false;
	}
	mTun->setEventHandler(this);

	return true;
}

void GlobalTunnel::finalise()
{
	if (mTun)
	{
		mpGroupTun->destroyTunnel(mTun);
		mTun = NULL;
	}
}

int GlobalTunnel::send(const void *data, size_t datalen)
{
	if (mTun)
		return mTun->send(data, datalen);

	return -1;
}

void GlobalTunnel::onRecv(KcpTunnel *pTunnel, const void *data, size_t datelen)
{		
}

NAMESPACE_END // namespace tun
