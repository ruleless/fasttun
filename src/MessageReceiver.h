#ifndef __MESSAGERECEIVER_H__
#define __MESSAGERECEIVER_H__

#include "FasttunBase.h"

NAMESPACE_BEG(msg)

template <class T, size_t MaxLen>
class MessageReceiver
{
	typedef void (T::*RecvFunc)(const void *, size_t);
	typedef void (T::*RecvErrFunc)();
  public:
    MessageReceiver(T *host, RecvFunc recvFunc, RecvErrFunc recvErrFunc)
			:mRcvdState(MsgRcvState_NoData)
			,mRcvdMsgLen(0)
			,mHost(host)
			,mRecvFunc(recvFunc)
			,mRecvErrFunc(recvErrFunc)
	{
		assert(mHost && mRecvFunc && mRecvErrFunc);
		memset(&mRcvdLenBuf, 0, sizeof(mRcvdLenBuf));
		memset(&mCurMsg, 0, sizeof(mCurMsg));
	}
	
    virtual ~MessageReceiver()
	{
		if (mCurMsg.ptr)
			free(mCurMsg.ptr);	
	}

	void input(const void *data, size_t datalen)
	{
		const char *ptr = (const char *)data;
		for (;;)		
		{
			size_t leftlen = parseMessage(ptr, datalen);
			if (MsgRcvState_Error == mMsgRcvstate)
			{
				(mHost->*mRecvErrFunc)();
				break;
			}
			else if (MsgRcvState_RcvComplete == mMsgRcvstate)
			{
				(mHost->*mRecvFunc)(mCurMsg.ptr, mCurMsg.len);
				clearCurMsg();
			}

			if (leftlen > 0)
			{
				ptr += (datalen-leftlen);
				datalen = leftlen;
			}
			else
			{
				break;
			}
		}
	}

  private:
	size_t parseMessage(const void *data, size_t datalen)
	{
		const char *ptr = (const char *)data;
	
		// 先收取消息长度
		if (MsgRcvState_NoData == mRcvdState)
		{
			assert(sizeof(size_t) >= mRcvdLenBuf.curlen);
			size_t copylen = sizeof(size_t)-mRcvdLenBuf.curlen;
			copylen = min(copylen, datalen);

			if (copylen > 0)
			{
				memcpy(mRcvdLenBuf.buf+mRcvdLenBuf.curlen, ptr, copylen);
				mRcvdLenBuf.curlen += copylen;
				ptr += copylen;
				datalen -= copylen;
			}
			if (mRcvdLenBuf.curlen == sizeof(size_t)) // 消息长度已获取
			{			
				MemoryStream stream;
				stream.append(mRcvdLenBuf.buf, sizeof(size_t));
				stream>>mCurMsg.len;
				if (mCurMsg.len > MaxLen)
				{
					mRcvdLenBuf = MsgRcvState_Error;
					mCurMsg.len = 0;
					return datalen;
				}
			
				mRcvdState = MsgRcvState_RcvdHead;
				mRcvdMsgLen = 0;
				mCurMsg.ptr = malloc(mCurMsg.len);
				assert(mCurMsg.ptr != NULL && "mCurMsg.ptr != NULL");
			}
		}

		// 再收取消息内容
		if (MsgRcvState_RcvdHead == mRcvdState)
		{
			assert(mCurMsg.len >= mRcvdMsgLen);
			size_t copylen = mCurMsg.len-mRcvdMsgLen;		
			copylen = min(copylen, datalen);

			if (copylen > 0)
			{
				memcpy(mCurMsg.ptr+mRcvdMsgLen, ptr, copylen);
				mRcvdMsgLen += copylen;
				ptr += copylen;
				datalen -= copylen;
			}
			if (mRcvdMsgLen == mCurMsg.len) // 消息体已获取
			{
				mRcvdState = MsgRcvState_RcvComplete;
			}
		}

		return datalen;
	}

	void clearCurMsg()
	{
		mRcvdState = MsgRcvState_NoData;
		
		memset(&mRcvdLenBuf, 0, sizeof(mRcvdLenBuf));
		
		if (mCurMsg.ptr)
			free(mCurMsg.ptr);
		mRcvdMsgLen = 0;
		mCurMsg.ptr = NULL;
		mCurMsg.len = 0;
	}

  private:
	enum EMsgRcvState
	{
		MsgRcvState_NoData,
		MsgRcvState_Error,
		MsgRcvState_RcvdHead,
		MsgRcvState_RcvComplete,
	};

	struct MessageLenBuf
	{
		size_t curlen;
		char buf[sizeof(size_t)];
	};
	
	struct Message
	{
		size_t len;
		char *ptr;
	};

	// message parse state
	EMsgRcvState mRcvdState;

	// message header
	MessageLenBuf mRcvdLenBuf;

	// message body
	size_t mRcvdMsgLen;
	Message mCurMsg;

	// message dealer
	T *mHost;
	RecvFunc mRecvFunc;
	RecvErrFunc mRecvErrFunc;
};

NAMESPACE_END // namespace msg

#endif // __MESSAGERECEIVER_H__
