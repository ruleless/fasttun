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
	mbRegForRead = true;

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
		
	if (mFd >= 0)
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
	mPeerAddr.sin_family = AF_INET;
	mPeerAddr.sin_port = htons(port);
	if (inet_pton(AF_INET, ip, &mPeerAddr.sin_addr) < 0)
	{
		logErrorLn("Connection::connect()  illegal ip("<<ip<<")");
		goto err_1;
	}

	if (::connect(mFd, (SA *)&mPeerAddr, sizeof(mPeerAddr)) < 0)
	{
		if (errno != EINPROGRESS)
			goto err_1;
	}

	if (!mEventPoller->registerForWrite(mFd, this))
	{
		logErrorLn("Connection::connect()  registerForWrite failed! "<<coreStrError());
		goto err_1;
	}
	mbRegForWrite = true;

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

	if (mbRegForWrite)
	{
		mEventPoller->deregisterForWrite(mFd);
		mbRegForWrite = false;
	}
	if (mbRegForRead)
	{
		mEventPoller->deregisterForRead(mFd);
		mbRegForRead = false;
	}
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
	if (mConnStatus != ConnStatus_Connected)
	{
		return 0;
	}
	
	static const int oncelen = 1024;
	char *buf = (char *)malloc(oncelen);
	int curlen = 0;
	for (;;)
	{
		int recvlen = recv(mFd, buf+curlen, oncelen, 0);
		if (recvlen > 0)
			curlen += recvlen;

		if (recvlen >= oncelen)
		{
			buf = (char *)realloc(buf, curlen+oncelen);
		}
		else
		{
			if (recvlen < 0)
			{
				if (errno != EAGAIN && errno != EWOULDBLOCK)
					mConnStatus = ConnStatus_Error;
			}
			else if (recvlen == 0)
			{
				mConnStatus = ConnStatus_Closed;
			}
			break;
		}
	}

	if (curlen > 0 && mHandler)
		mHandler->onRecv(this, buf, curlen);	

	if (mHandler)
	{
		if (ConnStatus_Error == mConnStatus)
			mHandler->onError(this);
		else if (ConnStatus_Closed == mConnStatus)
			mHandler->onDisconnected(this);
	}
	free(buf);

	return 0;
}

int Connection::handleOutputNotification(int fd)
{
	mbRegForWrite = false;
	mEventPoller->deregisterForWrite(fd);
	if (ConnStatus_Connecting == mConnStatus)
	{
		int err = 0;
		socklen_t errlen = sizeof(int);
		if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen) < 0 || err != 0)
		{
			if (err != 0)
				errno = err;
			
			mConnStatus = ConnStatus_Error;
			if (mHandler)
				mHandler->onError(this);
			
			return 0;
		}
		
		mEventPoller->registerForRead(fd, this);
		mbRegForRead = true;
		mConnStatus = ConnStatus_Connected;
		if (mHandler)
			mHandler->onConnected(this);
	}

	return 0;
}

NAMESPACE_END // namespace tun
