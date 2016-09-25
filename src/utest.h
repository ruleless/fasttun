#ifndef __UTEST_H__
#define __UTEST_H__

#include "cppunit/TestFixture.h"
#include "cppunit/extensions/HelperMacros.h"

#include "FasttunBase.h"
#include "MessageReceiver.h"
#include "DiskCache.h"

class UTest : public CppUnit::TestFixture
{
	CPPUNIT_TEST_SUITE(UTest);
	CPPUNIT_TEST(testMessageReceiver);
	CPPUNIT_TEST(testDiskCache);
	CPPUNIT_TEST_SUITE_END();
  public:
    UTest()
	{}
	
    virtual ~UTest();
	
	virtual void setUp();
	
	virtual void tearDown();

	void testMessageReceiver();	
	void _onRecvMessage(const void *data, uint16 datalen, void*);
	void _onRecvMsgError(void *);

	void testDiskCache();
};

#endif // __UTEST_H__
