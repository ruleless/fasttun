#ifndef __DISKCACHE_H__
#define __DISKCACHE_H__

#include "FasttunBase.h"

NAMESPACE_BEG(tun)

class DiskCache
{
  public:
    DiskCache()
			:mpFile(NULL)
	{}
	
    virtual ~DiskCache();

	ssize_t write(const void *data, size_t datalen);
	ssize_t read(void *data, size_t datalen);
	size_t peeksize();

  private:
	bool _createFile();

  private:
	FILE *mpFile;
};

NAMESPACE_END // namespace tun

#endif // __DISKCACHE_H__
