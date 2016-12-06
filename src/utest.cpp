#include "utest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cppunit/extensions/TestFactoryRegistry.h"
#include "cppunit/ui/text/TestRunner.h"

CPPUNIT_TEST_SUITE_REGISTRATION(UTest);

using namespace tun;

UTest::~UTest()
{}
    
void UTest::setUp()
{}
    
void UTest::tearDown()
{}

static const int TEST_COUNT = 128;
static uint8* buf[TEST_COUNT];
static uint16 buflen[TEST_COUNT];
typedef msg::MessageReceiver<UTest, 65535, uint16> MsgReceiver;
void UTest::testMessageReceiver()
{
    MsgReceiver *msgrcv = new MsgReceiver(this, &UTest::_onRecvMessage, &UTest::_onRecvMsgError);
    
    for (int i = 0; i < TEST_COUNT; ++i)
    {
        buflen[i] = random() % 65535;
        if (buflen[i] == 0)
            buflen[i] = 1;
        
        buf[i] = (uint8*)malloc(buflen[i]);
        for (int j = 0; j < buflen[i]; ++j)
        {
            buf[i][j] = (i+j)%256;
        }
       
        uint16 len = sizeof(int)+buflen[i];
        MemoryStream header;
        header<<len;
        header<<i;

        size_t k = 0;
        while (k < header.length())
        {
            uint16 randlen = random()%9;
            if (k+randlen > header.length())
                randlen = header.length()-k;
            msgrcv->input(header.data()+k, randlen, NULL);
            k += randlen;
        }
        k = 0;
        while (k < buflen[i])           
        {
            uint16 randlen = random()%1000;
            if (k+randlen > buflen[i])
                randlen = buflen[i]-k;
            msgrcv->input(buf[i]+k, randlen, NULL);
            k += randlen;
        }       
    }
}

void UTest::_onRecvMessage(const void *data, uint16 datalen, void *)
{
    CPPUNIT_ASSERT(datalen >= sizeof(int));
    
    const char *ptr = (const char *)data;
    MemoryStream header;
    
    header.append(ptr, sizeof(int));
    ptr += sizeof(int);
    datalen -= sizeof(int);

    int index = -1;
    header>>index;
    CPPUNIT_ASSERT(index >= 0);
    CPPUNIT_ASSERT(buflen[index] == datalen);
    CPPUNIT_ASSERT(memcmp(ptr, buf[index], datalen) == 0);
}

void UTest::_onRecvMsgError(void *)
{
    CPPUNIT_ASSERT(false);
}

void UTest::testDiskCache()
{   
    const char *strs[] = {
        "test1",
        "test23",
        "fkadsfkjsdakfjasdjfkasjrieuwqrnkvnakfhijlkf5a4s5f74asf42asd1fasdf",
        "jfakdsiofruewiorewnckjdkfjasfjdnafkmnmnkjujwiue9q  k safjkdsajfkmmjjlklk",
    };

    DiskCache c;
    for (int i = 0; i < sizeof(strs)/sizeof(const char *); ++i)
    {
        c.write(strs[i], strlen(strs[i]));
    }

    char *ptr = NULL;
    ssize_t sz = 0;
    int i = 0;
    while ((sz = c.peeksize()) > 0)
    {
        ptr = (char *)malloc(sz);
        c.read(ptr, sz);
        CPPUNIT_ASSERT(memcmp(ptr, strs[i++], sz) == 0);
    }
}


int main(int argc, char *argv[])
{
    core::createTrace();
    // core::output2Console();
    core::output2File("utest.log");

    CppUnit::TextUi::TestRunner runner;
    runner.addTest(CppUnit::TestFactoryRegistry::getRegistry().makeTest());
    runner.run();

    core::closeTrace();
    // getchar();
    exit(0);
}
