#include "DiskCache.h"

NAMESPACE_BEG(tun)

DiskCache::~DiskCache()
{
	if (mpFile)
		fclose(mpFile);
}

ssize_t DiskCache::write(const void *data, size_t datalen)
{
	assert(data && datalen > 0);
	if (NULL == mpFile)
		_createFile();
	if (NULL == mpFile)
		return -1;

	if (fwrite(&datalen, 1, sizeof(datalen), mpFile) != sizeof(datalen))
		return -1;
	
	return fwrite(data, 1, datalen, mpFile);
}

ssize_t DiskCache::read(void *data, size_t datalen)
{
	if (NULL == mpFile)
		return 0;

	size_t peeksz = peeksize();
	if (0 == peeksz)
		return 0;
	if (datalen < peeksz)
		return -2;

	fseek(mpFile, SEEK_CUR, -sizeof(peeksz));
	return fread(data, 1, peeksz, mpFile);
}

size_t DiskCache::peeksize()
{
	if (NULL == mpFile)
		return 0;

	size_t peeksz = 0;
	if (fread(&peeksz, 1, sizeof(peeksz), mpFile) != sizeof(peeksz))
		return 0;

	fseek(mpFile, SEEK_CUR, -sizeof(peeksz));
	return peeksz;
}

bool DiskCache::_createFile()
{
	if (mpFile)
		fclose(mpFile);
	
	mpFile = tmpfile();
	return mpFile != NULL;
}

NAMESPACE_END // namespace tun
