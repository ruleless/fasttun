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

	long curpos = ftell(mpFile);
	if (curpos < 0)
		return -10;
	if (fseek(mpFile, 0, SEEK_END) < 0)
		return -11;
	
	if (fwrite(&datalen, 1, sizeof(datalen), mpFile) != sizeof(datalen))
		return -2;
	if (fwrite(data, 1, datalen, mpFile) != datalen)
		return -3;

	fseek(mpFile, curpos, SEEK_SET);
	return datalen;
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

	fseek(mpFile, sizeof(peeksz), SEEK_CUR);
	return fread(data, 1, peeksz, mpFile);
}

size_t DiskCache::peeksize()
{
	if (NULL == mpFile)
		return 0;

	size_t peeksz = 0;
	if (fread(&peeksz, 1, sizeof(peeksz), mpFile) != sizeof(peeksz))
		return 0;

	fseek(mpFile, -sizeof(peeksz), SEEK_CUR);
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
