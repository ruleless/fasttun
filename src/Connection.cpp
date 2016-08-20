#include "Connection.h"

NAMESPACE_BEG(tun)

Connection::~Connection()
{
}

bool Connection::acceptConnection(int connfd)
{
	if (mFd >= 0)
	{
		logErrorLn("Connection::acceptConnection()  accept connetion error! the conn is in use!");
		return false;
	}

	mFd = connfd;

	socklen_t addrlen = sizeof(mPeerAddr);
	if (getpeername(connfd, (SA *)&mPeerAddr, &addrlen) < 0)
	{
		goto err_1;
	}

	// set nonblocking		
	if (!core::setNonblocking(mFd))
	{
		logErrorLn("Connection::acceptConnection()  set nonblocking error! "<<coreStrError());
		goto err_1;
	}	

	if (!mEventPoller->registerForRead(mFd, this))
	{
		logErrorLn("Connection::acceptConnection()  registerForRead failed! "<<coreStrError());
		goto err_1;
	}

	mConnStatus = ConnStatus_Connected;
	return true;

err_1:
	close(mFd);
	mFd = -1;

	return false;
}

bool Connection::connect(const char *ip, int port)
{
	if (mFd >= 0)
		shutdown();
		
	if (mFd < 0)
	{
		logErrorLn("Connection::connect()  accept connetion error! the conn is in use!");
		return false;
	}

	mFd = socket(AF_INET, SOCK_STREAM, 0);
	if (mFd < 0)
	{
		logErrorLn("Connection::connect()  create socket error!"<<coreStrError());
		return false;
	}

	// set nonblocking	
	int fstatus = fcntl(mFd, F_GETFL);
	if (fstatus < 0)
	{
		logErrorLn("Connection::connect()  get file status error! "<<coreStrError());
		goto err_1;
	}
	if (fcntl(mFd, F_SETFL, fstatus|O_NONBLOCK) < 0)
	{
		logErrorLn("Connection::connect()  set nonblocking error! "<<coreStrError());
		goto err_1;
	}

	// assign the server address
	struct sockaddr_in servaddr;
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(port);
	if (inet_pton(AF_INET, ip, &servaddr.sin_addr) < 0)
	{
		logErrorLn("Connection::connect()  illegal ip("<<ip<<")");
		goto err_1;
	}

	if (::connect(mFd, (SA *)&servaddr, sizeof(servaddr)) < 0)
	{
		if (errno != EAGAIN)
			goto err_1;
	}

	if (!mEventPoller->registerForRead(mFd, this))
	{
		logErrorLn("Connection::connect()  registerForRead failed! "<<coreStrError());
		goto err_1;
	}

	mConnStatus = ConnStatus_Connecting;
	return true;

err_1:
	close(mFd);
	mFd = -1;
	
	return false;
}

void Connection::shutdown()
{
	if (mFd < 0)
		return;

	mEventPoller->deregisterForRead(mFd);
	close(mFd);
	mFd = -1;
	mConnStatus = ConnStatus_Closed;
}

int Connection::send(const void *data, size_t datalen)
{
	if (mFd < 0)
	{
		logErrorLn("Connection::send()  send error! socket uninited!");
		return -1;
	}

	return ::send(mFd, data, datalen, 0);
}

const char* Connection::getPeerIp(char *ip, int iplen) const
{
	return inet_ntop(AF_INET, &mPeerAddr.sin_addr, ip, iplen);
}

int Connection::getPeerPort() const
{
	return ntohs(mPeerAddr.sin_port);
}

int Connection::handleInputNotification(int fd)
{
	if (ConnStatus_Connecting == mConnStatus)
	{
		mConnStatus = ConnStatus_Connected;
		if (mHandler)
			mHandler->onConnected(this);
	}
	if (mConnStatus != ConnStatus_Connected)
	{
		return 0;
	}
	
	core::MemoryStream buf;
	int oncelen = 1024;
	buf.reserve(oncelen);
	for (;;)
	{
		int recvlen = recv(mFd, buf.data()+buf.wpos(), oncelen, 0);
		if (recvlen < 0)
		{
			if (errno != EAGAIN)
				mConnStatus = ConnStatus_Error;
			break;
		}
		else if (recvlen == 0)
		{
			mConnStatus = ConnStatus_Closed;
			break;
		}
		else if (recvlen < oncelen)
		{
			buf.wpos(buf.wpos()+recvlen);
			break;			
		}
		else
		{
			buf.wpos(buf.wpos()+recvlen);
			buf.reserve(buf.length()+oncelen);
		}
	}

	if (buf.length() > 0 && mHandler)
		mHandler->onRecv(this, buf.data(), buf.length());

	if (mHandler)
	{
		if (ConnStatus_Error == mConnStatus)
			mHandler->onError(this);
		else if (ConnStatus_Closed == mConnStatus)
			mHandler->onDisconnected(this);
	}

	return 0;
}

NAMESPACE_END // namespace tun
