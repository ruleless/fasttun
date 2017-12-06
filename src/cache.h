#ifndef __CACHE_H__
#define __CACHE_H__

#include "fasttun_base.h"
#include "disk_cache.h"

NAMESPACE_BEG(tun)

template <class T, int MAX_LEN_CACHE_IN_MEM = 256*1024>
class Cache
{
    typedef bool (T::*FuncType)(const void *, size_t);
  public:
    Cache(T *host, FuncType func)
            :mHost(host)
            ,mFunc(func)
            ,mCachedList()
            ,mDiskCache()
            ,mLenCacheInMem(0)
            ,mLenCacheInFile(0)
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
        return 0 == mLenCacheInMem && 0 == mLenCacheInFile;
    }

    void cache(const void *data, size_t len)
    {
        assert(len > 0 && "cache() && len>0");

        // cache in file
        if (mLenCacheInMem+len > MAX_LEN_CACHE_IN_MEM || mLenCacheInFile > 0)
        {
            int ret = mDiskCache.write(data, len);
            if (ret == (int)len)
            {
                mLenCacheInFile += len;
                return;
            }

            ErrorPrint("Cache::cache() write to file failed! return code:%d", ret);
        }

        // cache in mem
        Data d;
        mLenCacheInMem += len;
        d.len = len;
        d.data = (char *)malloc(len);
        assert(d.data != NULL && "cache() malloc failed");
        memcpy(d.data, data, len);
        this->mCachedList.push_back(d);
    }

    void clear()
    {
        typename DataList::iterator it = this->mCachedList.begin();
        for (; it != this->mCachedList.end(); ++it)
        {
            free((*it).data);
        }
        this->mCachedList.clear();

        mDiskCache.clear();
    }

    bool flushAll()
    {
        typename DataList::iterator it = this->mCachedList.begin();
        for (; it != this->mCachedList.end(); )
        {
            if ((mHost->*mFunc)((*it).data, (*it).len))
            {
                mLenCacheInMem -= (*it).len;
                free((*it).data);
                mCachedList.erase(it++);
            }
            else
            {
                return false;
            }
        }

        for (ssize_t sz = mDiskCache.peeksize(); sz > 0; sz = mDiskCache.peeksize())
        {
            char *ptr = (char *)malloc(sz);
            if (NULL == ptr)
            {
                ErrorPrint("malloc failed size=%lld", sz);
                assert(false);
            }
            assert(mDiskCache.read(ptr, sz) == sz);

            if ((mHost->*mFunc)(ptr, sz))
            {
                mLenCacheInFile -= sz;
                free(ptr);
            }
            else
            {
                free(ptr);
                mDiskCache.rollback(sz);
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
    DiskCache mDiskCache;
    size_t mLenCacheInMem;
    size_t mLenCacheInFile;
};

NAMESPACE_END // namespace tun

#endif // __CACHE_H__
