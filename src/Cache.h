#ifndef __CACHE_H__
#define __CACHE_H__

#include "FasttunBase.h"

NAMESPACE_BEG(tun)

template <class T>
class Cache
{
	typedef bool (T::*FuncType)(const void *, size_t);
  public:
    Cache(T *host, FuncType func)
			:mHost(host)
			,mFunc(func)
			,mCachedList()
	{}
	
    virtual ~Cache()
	{
		typename DataList::iterator it = this->mCachedList.begin();
		for (; it != this->mCachedList.end(); ++it)
		{
			free((*it).data);
		}
		this->mCachedList.clear();
	}

	bool empty() const
	{
		return this->mCachedList.empty();
	}

	void cache(const void *data, size_t len)
	{
		assert(len > 0 && "cache() && len>0");
		
		Data d;
		d.len = len;
		d.data = (char *)malloc(len);
		assert(d.data != NULL && "cache() malloc failed");
		memcpy(d.data, data, len);
		this->mCachedList.push_back(d);
	}

	bool flushAll()
	{
		typename DataList::iterator it = this->mCachedList.begin();
		for (; it != this->mCachedList.end(); )
		{
			if ((mHost->*mFunc)((*it).data, (*it).len))
			{
				free((*it).data);
				mCachedList.erase(it++);
			}
			else
			{
				return false;
			}
		}
		return true;
	}
	
  private:
	struct Data
	{
		size_t len;
		char *data;
	};

	typedef std::list<Data> DataList;

	T *mHost;
	FuncType mFunc;
	DataList mCachedList;	
};

NAMESPACE_END // namespace tun

#endif // __CACHE_H__



