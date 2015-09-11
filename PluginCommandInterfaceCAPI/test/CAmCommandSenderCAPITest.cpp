/**
 *  Copyright (c) 2012 BMW
 *
 *  \author Aleksandar Donchev, aleksander.donchev@partner.bmw.de BMW 2013
 *
 *  \copyright
 *  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction,
 *  including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
 *  subject to the following conditions:
 *  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
 *  THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *  For further information see http://www.genivi.org/.
 */

#include "CAmCommandSenderCAPITest.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <string>
#include <vector>
#include <set>
#include "TAmPluginTemplate.h"
#include "MockIAmCommandReceive.h"
#include "CAmDltWrapper.h"
#include "../include/CAmCommandSenderCAPI.h"
#include "../include/CAmCommandSenderCommon.h"
#include "MockNotificationsClient.h"
#include <CommonAPI/CommonAPI.hpp>
#include <sys/time.h>



using namespace am;
using namespace testing;

static CAmTestsEnvironment* env;

pthread_cond_t      cond  		= PTHREAD_COND_INITIALIZER;
pthread_mutex_t     mutex		= PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t      condPxy 	= PTHREAD_COND_INITIALIZER;
pthread_mutex_t     mutexPxy    = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t      condSer 	= PTHREAD_COND_INITIALIZER;
pthread_mutex_t     mutexSer    = PTHREAD_MUTEX_INITIALIZER;

void* run_client(void*)
{
	CAmSocketHandler socketHandler;
	env->mpSocketHandlerClient = &socketHandler;
	CAmTestCAPIWrapper wrapper(&socketHandler, "AudioManager-client");
	env->mSocketHandlerClient = &socketHandler;

	env->mProxy = wrapper.buildProxy<am_commandcontrol::CommandControlProxy>(CAmCommandSenderCAPI::DEFAULT_DOMAIN, CAmCommandSenderCAPI::COMMAND_SENDER_INSTANCE);//"AudioManager-client"
	assert(env->mProxy);
	env->mProxy->getProxyStatusEvent().subscribe(std::bind(&CAmTestsEnvironment::onServiceStatusEvent,env,std::placeholders::_1));
	MockNotificationsClient mock;
	env->mpMockClient = &mock;
	env->mProxy->getNumberOfSourceClassesChangedEvent().subscribe(
											std::bind(&MockNotificationsClient::onNumberOfSourceClassesChangedEvent, std::ref(mock)));

	env->mProxy->getNewMainConnectionEvent().subscribe(std::bind(&MockNotificationsClient::onNewMainConnection, std::ref(mock), std::placeholders::_1));

	pthread_mutex_lock(&mutexSer);
	env->mIsProxyInitilized = true;
	pthread_mutex_unlock(&mutexSer);
	pthread_cond_signal(&condSer);

	socketHandler.start_listenting();

//Cleanup
    env->mProxy.reset();
    env->mSocketHandlerClient = NULL;

    return (NULL);
}

void* run_service(void*)
{
	CAmSocketHandler socketHandler;
	env->mpSocketHandlerService = &socketHandler;
	CAmTestCAPIWrapper wrapper(&socketHandler, "AudioManager");
	CAmCommandSenderCAPI *pPlugin = CAmCommandSenderCAPI::newCommandSenderCAPI(&wrapper);
	env->mpPlugin = pPlugin;
	env->mSocketHandlerService = &socketHandler;
	MockIAmCommandReceive mock;
	env->mpCommandReceive = &mock;
    if(pPlugin->startupInterface(env->mpCommandReceive)!=E_OK)
	{
		logError("CommandSendInterface can't start!");
	}
    else
    {
    	ON_CALL(*env->mpCommandReceive, getListMainSources(_)).WillByDefault(Return(E_OK));
    	ON_CALL(*env->mpCommandReceive, getListMainSinks(_)).WillByDefault(Return(E_OK));
    	ON_CALL(*env->mpCommandReceive, getListMainSourceSoundProperties(_,_)).WillByDefault(Return(E_OK));

    	EXPECT_CALL(*env->mpCommandReceive,confirmCommandReady(10,_));
    	pPlugin->setCommandReady(10);
    	socketHandler.start_listenting();

    	EXPECT_CALL(*env->mpCommandReceive,confirmCommandRundown(10,_));
    	pPlugin->setCommandRundown(10);
    	pPlugin->tearDownInterface(env->mpCommandReceive);
    }

//Cleanup
    env->mpPlugin = NULL;
    env->mpCommandReceive = NULL;
    env->mSocketHandlerClient = NULL;

    return (NULL);
}

void* run_listener(void*)
{
    pthread_mutex_lock(&mutexSer);
    while (env->mIsProxyInitilized==false)
    {
    	std::cout << "\n\r Intialize proxy..\n\r" ;
    	pthread_cond_wait(&condSer, &mutexSer);
    }
    pthread_mutex_unlock(&mutexSer);

    time_t start = time(0);
    time_t now = start;
    pthread_mutex_lock(&mutexPxy);
    while ( env->mIsServiceAvailable==false && now-start <= 15 )
    {
    	std::cout << " Waiting for proxy..\n\r" ;
        struct timespec ts = { 0, 0 };
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 5;
        pthread_cond_timedwait(&condPxy, &mutexPxy, &ts);
        now = time(0);
    }
    pthread_mutex_unlock(&mutexPxy);
    pthread_cond_signal(&cond);

	return NULL;
}

CAmTestsEnvironment::CAmTestsEnvironment() :
		mListenerThread(0),
		mServicePThread(0),
		mClientPThread(0),
		mSocketHandlerService(NULL),
		mSocketHandlerClient(NULL),
		mIsProxyInitilized(false),
		mIsServiceAvailable(false),
		mpCommandReceive(NULL),
		mpPlugin(NULL)
{
    env=this;

	CAmDltWrapper::instance()->registerApp("capiTest", "capiTest");
	pthread_create(&mListenerThread, NULL, run_listener, NULL);
    pthread_create(&mServicePThread, NULL, run_service, NULL);
    pthread_create(&mClientPThread, NULL, run_client, NULL);
}

CAmTestsEnvironment::~CAmTestsEnvironment()
{

}

void CAmTestsEnvironment::SetUp()
{
	pthread_cond_wait(&cond, &mutex);
	mpSerializer = new CAmSerializer(env->mpSocketHandlerService);
}

void CAmTestsEnvironment::TearDown()
{
//	mWrapperClient.factory().reset();

	mSocketHandlerClient->exit_mainloop();
    pthread_join(mClientPThread, NULL);
	mSocketHandlerService->exit_mainloop();
    pthread_join(mServicePThread, NULL);
    sleep(1);
}

void CAmTestsEnvironment::onServiceStatusEvent(const CommonAPI::AvailabilityStatus& serviceStatus)
{
    std::stringstream  avail;
    avail  << "(" << static_cast<int>(serviceStatus) << ")";

    logInfo("Service Status changed to ", avail.str());
    std::cout << std::endl << "Service Status changed to " << avail.str() << std::endl;
    pthread_mutex_lock(&mutexPxy);
    mIsServiceAvailable = (serviceStatus==CommonAPI::AvailabilityStatus::AVAILABLE);
    pthread_mutex_unlock(&mutexPxy);
    pthread_cond_signal(&condPxy);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new CAmTestsEnvironment);
    return RUN_ALL_TESTS();
}

CAmCommandSenderCAPITest::CAmCommandSenderCAPITest()
{

}

CAmCommandSenderCAPITest::~CAmCommandSenderCAPITest()
{

}

void CAmCommandSenderCAPITest::SetUp()
{
	::testing::GTEST_FLAG(throw_on_failure) = false;
	::testing::FLAGS_gmock_verbose = "error";
//	::testing::DefaultValue<am_Error_e>::Set(am_Error_e(E_OK));
}

void CAmCommandSenderCAPITest::TearDown()
{
	::testing::GTEST_FLAG(throw_on_failure) = true;
}

TEST_F(CAmCommandSenderCAPITest, ClientStartupTest)
{
	ASSERT_TRUE(env->mIsServiceAvailable);
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpCommandReceive));
}

ACTION(returnClientConnect)
{
arg2=101;
}

TEST_F(CAmCommandSenderCAPITest, ConnectTest)
{
	ASSERT_TRUE(env->mIsServiceAvailable);
	if(env->mIsServiceAvailable)
	{
		am_types::am_sourceID_t sourceID = 500;
		am_types::am_sinkID_t sinkID = 400;
		CommonAPI::CallStatus callStatus = CommonAPI::CallStatus::NOT_AVAILABLE;
		am_types::am_Error_e result = am_types::am_Error_e::E_OK;
		am_types::am_mainConnectionID_t mainConnectionID = 0;

		EXPECT_CALL(*env->mpCommandReceive, connect(_, _, _)).WillOnce(DoAll(returnClientConnect(), Return(E_OK)));
		env->mProxy->connect(sourceID, sinkID, callStatus, mainConnectionID, result);
		ASSERT_EQ(callStatus, CommonAPI::CallStatus::SUCCESS);
		ASSERT_EQ((int)result, am_types::am_Error_e::E_OK);
		ASSERT_EQ(mainConnectionID, 101);
		EXPECT_CALL(*env->mpCommandReceive, disconnect(mainConnectionID)).WillOnce(Return(am_Error_e::E_OK));
		ASSERT_EQ((int)result, am_types::am_Error_e::E_OK);
		env->mProxy->disconnect(mainConnectionID, callStatus, result);
	}
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpCommandReceive));
}

TEST_F(CAmCommandSenderCAPITest, SetVolumeTest)
{
	ASSERT_TRUE(env->mIsServiceAvailable);
	if(env->mIsServiceAvailable)
	{
		am_types::am_mainVolume_t volume = 100;
		am_types::am_sinkID_t sinkID = 400;
		CommonAPI::CallStatus callStatus = CommonAPI::CallStatus::NOT_AVAILABLE;
		am_types::am_Error_e result = am_types::am_Error_e::E_OK;

		EXPECT_CALL(*env->mpCommandReceive, setVolume(sinkID,volume)).WillOnce(Return(E_OK));
		env->mProxy->setVolume(sinkID, volume, callStatus, result);
		ASSERT_EQ((int)result, am_types::am_Error_e::E_OK);
	}
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpCommandReceive));
}

TEST_F(CAmCommandSenderCAPITest, VolumeStepTest)
{

	ASSERT_TRUE(env->mIsServiceAvailable);
	if(env->mIsServiceAvailable)
	{
		am_types::am_mainVolume_t volume = 100;
		am_types::am_sinkID_t sinkID = 400;
		CommonAPI::CallStatus callStatus = CommonAPI::CallStatus::NOT_AVAILABLE;
		am_types::am_Error_e result = am_types::am_Error_e::E_OK;

		EXPECT_CALL(*env->mpCommandReceive, volumeStep(sinkID,volume)).WillOnce(Return(E_OK));
		env->mProxy->volumeStep(sinkID, volume, callStatus, result);
		ASSERT_EQ((int)result, am_types::am_Error_e::E_OK);
	}
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpCommandReceive));
}

TEST_F(CAmCommandSenderCAPITest, SetSinkMuteStateTest)
{
	ASSERT_TRUE(env->mIsServiceAvailable);
	if(env->mIsServiceAvailable)
	{
		am_types::am_MuteState_e value = am_types::am_MuteState_e::MS_UNKNOWN;
		am_types::am_sinkID_t sinkID = 400;
		CommonAPI::CallStatus callStatus = CommonAPI::CallStatus::NOT_AVAILABLE;
		am_types::am_Error_e result = am_types::am_Error_e::E_OK;

		EXPECT_CALL(*env->mpCommandReceive, setSinkMuteState(sinkID, am_MuteState_e::MS_UNKNOWN)).WillOnce(Return(E_OK));
		env->mProxy->setSinkMuteState(sinkID, value, callStatus, result);
		ASSERT_EQ((int)result, am_types::am_Error_e::E_OK);
	}
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpCommandReceive));
}

TEST_F(CAmCommandSenderCAPITest, SetMainSinkSoundPropertyTest)
{
	ASSERT_TRUE(env->mIsServiceAvailable);
	if(env->mIsServiceAvailable)
	{
		am_types::am_sinkID_t sinkID = 400;
		CommonAPI::CallStatus callStatus = CommonAPI::CallStatus::NOT_AVAILABLE;
		am_types::am_Error_e result = am_types::am_Error_e::E_OK;

		EXPECT_CALL(*env->mpCommandReceive, setMainSinkSoundProperty(AllOf(
				Field(&am_MainSoundProperty_s::value, 3),
				Field(&am_MainSoundProperty_s::type, MSP_UNKNOWN)), sinkID)).WillOnce(Return(E_OK));
		am_types::am_MainSoundProperty_s value(MSP_UNKNOWN, (const int16_t)3);
		env->mProxy->setMainSinkSoundProperty(sinkID, value, callStatus, result);
		ASSERT_EQ((int)result, am_types::am_Error_e::E_OK);
	}
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpCommandReceive));
}

TEST_F(CAmCommandSenderCAPITest, SetMainSourceSoundPropertyTest)
{
	ASSERT_TRUE(env->mIsServiceAvailable);
	if(env->mIsServiceAvailable)
	{
		am_types::am_sourceID_t sID = 400;
		CommonAPI::CallStatus callStatus = CommonAPI::CallStatus::NOT_AVAILABLE;
		am_types::am_Error_e result = am_types::am_Error_e::E_OK;

		EXPECT_CALL(*env->mpCommandReceive, setMainSourceSoundProperty(AllOf(
				Field(&am_MainSoundProperty_s::value, 3),
				Field(&am_MainSoundProperty_s::type, MSP_UNKNOWN)), sID)).WillOnce(Return(E_OK));
		am_types::am_MainSoundProperty_s value(MSP_UNKNOWN, (const int16_t)3);
		env->mProxy->setMainSourceSoundProperty(sID, value, callStatus, result);
		ASSERT_EQ((int)result, am_types::am_Error_e::E_OK);
	}
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpCommandReceive));
}

TEST_F(CAmCommandSenderCAPITest, SetSystemPropertyTest)
{
	ASSERT_TRUE(env->mIsServiceAvailable);
	if(env->mIsServiceAvailable)
	{
		CommonAPI::CallStatus callStatus = CommonAPI::CallStatus::NOT_AVAILABLE;
		am_types::am_Error_e result = am_types::am_Error_e::E_OK;

		EXPECT_CALL(*env->mpCommandReceive, setSystemProperty(Field(&am_SystemProperty_s::value, 2))).WillOnce(Return(E_OK));

		am_types::am_SystemProperty_s value(SYP_UNKNOWN, (const int16_t)2);
		env->mProxy->setSystemProperty(value, callStatus, result);
		ASSERT_EQ((int)result, am_types::am_Error_e::E_OK);
	}
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpCommandReceive));
}

ACTION(returnListConnections){
	std::vector<am_MainConnectionType_s> list;
	am_MainConnectionType_s listItem;
	listItem.mainConnectionID=15;
	listItem.sinkID=4;
	listItem.sourceID=3;
	listItem.connectionState=CS_UNKNOWN;
	listItem.delay=34;
	list.push_back(listItem);
	arg0=list;
}

TEST_F(CAmCommandSenderCAPITest, GetListMainConnectionsTest)
{
	ASSERT_TRUE(env->mIsServiceAvailable);
	if(env->mIsServiceAvailable)
	{
		CommonAPI::CallStatus callStatus = CommonAPI::CallStatus::NOT_AVAILABLE;
		am_types::am_Error_e result = am_types::am_Error_e::E_OK;

		EXPECT_CALL(*env->mpCommandReceive, getListMainConnections(_)).WillOnce(DoAll(returnListConnections(), Return(E_OK)));
		am_types::am_MainConnection_L listConnections;
		env->mProxy->getListMainConnections(callStatus, listConnections, result);
		ASSERT_EQ((int)result, am_types::am_Error_e::E_OK);
		ASSERT_EQ(1, listConnections.size());
		ASSERT_EQ(15, listConnections.at(0).getMainConnectionID());
		ASSERT_EQ(4, listConnections.at(0).getSinkID());
	}
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpCommandReceive));
}

ACTION(returnListSinks){
	std::vector<am_SinkType_s> list;
	am_SinkType_s listItem;
	listItem.availability.availability=A_UNAVAILABLE;
	listItem.availability.availabilityReason=AR_GENIVI_NOMEDIA;
	listItem.muteState=MS_UNMUTED;
	listItem.name="mySink";
	listItem.sinkClassID=34;
	listItem.sinkID=24;
	listItem.volume=124;
	list.push_back(listItem);
	arg0=list;
}

TEST_F(CAmCommandSenderCAPITest, GetListMainSinksTest)
{
	ASSERT_TRUE(env->mIsServiceAvailable);
	if(env->mIsServiceAvailable)
	{
		CommonAPI::CallStatus callStatus = CommonAPI::CallStatus::NOT_AVAILABLE;
		am_types::am_Error_e result = am_types::am_Error_e::E_OK;

		EXPECT_CALL(*env->mpCommandReceive, getListMainSinks(_)).WillOnce(DoAll(returnListSinks(), Return(E_OK)));
		am_types::am_SinkType_L listMainSinks;
		env->mProxy->getListMainSinks(callStatus, listMainSinks,result);
		ASSERT_EQ((int)result, am_types::am_Error_e::E_OK);
		ASSERT_EQ(1, listMainSinks.size());
		ASSERT_EQ(34, listMainSinks.at(0).getSinkClassID());
		ASSERT_EQ(24, listMainSinks.at(0).getSinkID());
		ASSERT_EQ(am_types::am_Availability_e::A_UNAVAILABLE, listMainSinks.at(0).getAvailability().getAvailability());
		ASSERT_EQ(AR_GENIVI_NOMEDIA, listMainSinks.at(0).getAvailability().getAvailabilityReason());
	}
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpCommandReceive));
}

ACTION(returnListSources){
	std::vector<am_SourceType_s> list;
	am_SourceType_s listItem;
	listItem.availability.availability=A_MAX;
	listItem.availability.availabilityReason=AR_GENIVI_SAMEMEDIA;
	listItem.name="MySource";
	listItem.sourceClassID=12;
	listItem.sourceID=224;
	list.push_back(listItem);
	listItem.name="NextSource";
	listItem.sourceID=22;
	list.push_back(listItem);
	arg0=list;
}
TEST_F(CAmCommandSenderCAPITest, GetListMainSourcesTest)
{
	ASSERT_TRUE(env->mIsServiceAvailable);
	if(env->mIsServiceAvailable)
	{
		CommonAPI::CallStatus callStatus = CommonAPI::CallStatus::NOT_AVAILABLE;
		am_types::am_Error_e result = am_types::am_Error_e::E_OK;

		EXPECT_CALL(*env->mpCommandReceive, getListMainSources(_)).WillOnce(DoAll(returnListSources(), Return(E_OK)));
		am_types::am_SourceType_L list;
		env->mProxy->getListMainSources(callStatus, list, result);
		ASSERT_EQ((int)result, am_types::am_Error_e::E_OK);
		ASSERT_EQ(2, list.size());
		ASSERT_EQ(12, list.at(0).getSourceClassID());
		ASSERT_TRUE(224==list.at(0).getSourceID()||224==list.at(1).getSourceID());
		ASSERT_EQ(am_types::am_Availability_e::A_MAX, list.at(0).getAvailability().getAvailability());
		ASSERT_EQ(AR_GENIVI_SAMEMEDIA, list.at(0).getAvailability().getAvailabilityReason());
		ASSERT_TRUE(22==list.at(0).getSourceID()||22==list.at(1).getSourceID());
	}
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpCommandReceive));
}

ACTION(returnListMainSinkSoundProperties){
	std::vector<am_MainSoundProperty_s> list;
	am_MainSoundProperty_s listItem;
	listItem.type=MSP_UNKNOWN;
	listItem.value=223;
	list.push_back(listItem);
	listItem.type=MSP_GENIVI_BASS;
	listItem.value=2;
	list.push_back(listItem);
	arg1=list;
}

TEST_F(CAmCommandSenderCAPITest, GetListMainSinkSoundPropertiesTest)
{
	ASSERT_TRUE(env->mIsServiceAvailable);
	if(env->mIsServiceAvailable)
	{
		am_types::am_sinkID_t sID = 400;
		CommonAPI::CallStatus callStatus = CommonAPI::CallStatus::NOT_AVAILABLE;
		am_types::am_Error_e result = am_types::am_Error_e::E_OK;

		EXPECT_CALL(*env->mpCommandReceive, getListMainSinkSoundProperties(sID,_)).WillOnce(DoAll(returnListMainSinkSoundProperties(), Return(E_OK)));
		am_types::am_MainSoundProperty_L list;
		env->mProxy->getListMainSinkSoundProperties(sID, callStatus, list, result);
		ASSERT_EQ((int)result, am_types::am_Error_e::E_OK);
		ASSERT_EQ(2, list.size());
		ASSERT_EQ(223, list.at(0).getValue());
		ASSERT_EQ(MSP_UNKNOWN, list.at(0).getType());
		ASSERT_EQ(2, list.at(1).getValue());
		ASSERT_EQ(MSP_GENIVI_BASS, list.at(1).getType());
	}
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpCommandReceive));
}

ACTION(returnListMainSourceSoundProperties){
	std::vector<am_MainSoundProperty_s> list;
	am_MainSoundProperty_s listItem;
	listItem.type=MSP_GENIVI_MID;
	listItem.value=223;
	list.push_back(listItem);
	listItem.type=MSP_GENIVI_BASS;
	listItem.value=2;
	list.push_back(listItem);
	arg1=list;
}

TEST_F(CAmCommandSenderCAPITest, GetListMainSourceSoundPropertiesTest)
{
	ASSERT_TRUE(env->mIsServiceAvailable);
	if(env->mIsServiceAvailable)
	{
		am_types::am_sourceID_t sID = 400;
		CommonAPI::CallStatus callStatus = CommonAPI::CallStatus::NOT_AVAILABLE;
		am_types::am_Error_e result = am_types::am_Error_e::E_OK;

		EXPECT_CALL(*env->mpCommandReceive, getListMainSourceSoundProperties(sID,_)).WillOnce(DoAll(returnListMainSourceSoundProperties(), Return(E_OK)));
		am_types::am_MainSoundProperty_L list;
		env->mProxy->getListMainSourceSoundProperties(sID, callStatus, list, result);
		ASSERT_EQ((int)result, am_types::am_Error_e::E_OK);
		ASSERT_EQ(2, list.size());
		ASSERT_EQ(223, list.at(0).getValue());
		ASSERT_EQ(MSP_GENIVI_MID, list.at(0).getType());
		ASSERT_EQ(2, list.at(1).getValue());
		ASSERT_EQ(MSP_GENIVI_BASS, list.at(1).getType());
	}
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpCommandReceive));
}

ACTION(returnListSourceClasses){
	std::vector<am_SourceClass_s> list;
	am_SourceClass_s listItem;
	am_ClassProperty_s property;
	property.classProperty=CP_UNKNOWN;
	property.value=12;
	listItem.name="FirstCLass";
	listItem.sourceClassID=23;
	listItem.listClassProperties.push_back(property);
	list.push_back(listItem);
	listItem.name="SecondCLass";
	listItem.sourceClassID=2;
	listItem.listClassProperties.push_back(property);
	list.push_back(listItem);
	arg0=list;
}

TEST_F(CAmCommandSenderCAPITest, GetListSourceClassesTest)
{
	ASSERT_TRUE(env->mIsServiceAvailable);
	if(env->mIsServiceAvailable)
	{
		CommonAPI::CallStatus callStatus = CommonAPI::CallStatus::NOT_AVAILABLE;
		am_types::am_Error_e result = am_types::am_Error_e::E_OK;

		EXPECT_CALL(*env->mpCommandReceive, getListSourceClasses(_)).WillOnce(DoAll(returnListSourceClasses(), Return(E_OK)));
		am_types::am_SourceClass_L list;
		env->mProxy->getListSourceClasses(callStatus, list, result);
		ASSERT_EQ((int)result, am_types::am_Error_e::E_OK);
		ASSERT_EQ(2, list.size());

		ASSERT_EQ(23, list.at(0).getSourceClassID());
		ASSERT_EQ(1, list.at(0).getListClassProperties().size());
		ASSERT_EQ(CP_UNKNOWN, list.at(0).getListClassProperties().at(0).getClassProperty());

		ASSERT_EQ(2, list.at(1).getSourceClassID());
		ASSERT_EQ(2, list.at(1).getListClassProperties().size());
		ASSERT_EQ(CP_UNKNOWN, list.at(1).getListClassProperties().at(0).getClassProperty());
	}
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpCommandReceive));
}

ACTION(returnListSinkClasses){
	std::vector<am_SinkClass_s> list;
	am_SinkClass_s listItem;
	am_ClassProperty_s property;
	property.classProperty=CP_UNKNOWN;
	property.value=122;
	listItem.name="FirstCLass";
	listItem.sinkClassID=23;
	listItem.listClassProperties.push_back(property);
	list.push_back(listItem);
	listItem.name="SecondCLass";
	listItem.sinkClassID=2;
	listItem.listClassProperties.push_back(property);
	list.push_back(listItem);
	arg0=list;
}

TEST_F(CAmCommandSenderCAPITest, GetListSinkClassesTest)
{
	ASSERT_TRUE(env->mIsServiceAvailable);
	if(env->mIsServiceAvailable)
	{
		CommonAPI::CallStatus callStatus = CommonAPI::CallStatus::NOT_AVAILABLE;
		am_types::am_Error_e result = am_types::am_Error_e::E_OK;

		EXPECT_CALL(*env->mpCommandReceive, getListSinkClasses(_)).WillOnce(DoAll(returnListSinkClasses(), Return(E_OK)));
		am_types::am_SinkClass_L list;
		env->mProxy->getListSinkClasses(callStatus, list,result);
		ASSERT_EQ((int)result, am_types::am_Error_e::E_OK);
		ASSERT_EQ(2, list.size());

		ASSERT_EQ(0, list.at(0).getName().compare("FirstCLass"));
		ASSERT_EQ(23, list.at(0).getSinkClassID());
		ASSERT_EQ(1, list.at(0).getListClassProperties().size());
		ASSERT_EQ(CP_UNKNOWN, list.at(0).getListClassProperties().at(0).getClassProperty());

		ASSERT_EQ(0, list.at(1).getName().compare("SecondCLass"));
		ASSERT_EQ(2, list.at(1).getSinkClassID());
		ASSERT_EQ(2, list.at(1).getListClassProperties().size());
		ASSERT_EQ(CP_UNKNOWN, list.at(1).getListClassProperties().at(0).getClassProperty());
	}
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpCommandReceive));
}

ACTION(returnListSystemProperties){
	std::vector<am_SystemProperty_s> list;
	am_SystemProperty_s listItem;
	listItem.type=SYP_UNKNOWN;
	listItem.value=-2245;
	list.push_back(listItem);
	arg0=list;
}

TEST_F(CAmCommandSenderCAPITest, GetListSystemPropertiesTest)
{
	ASSERT_TRUE(env->mIsServiceAvailable);
	if(env->mIsServiceAvailable)
	{
		CommonAPI::CallStatus callStatus = CommonAPI::CallStatus::NOT_AVAILABLE;
		am_types::am_Error_e result = am_types::am_Error_e::E_OK;

		EXPECT_CALL(*env->mpCommandReceive, getListSystemProperties(_)).WillOnce(DoAll(returnListSystemProperties(), Return(E_OK)));
		am_types::am_SystemProperty_L list;
		env->mProxy->getListSystemProperties(callStatus, list,result);
		ASSERT_EQ((int)result, am_types::am_Error_e::E_OK);
		ASSERT_EQ(1, list.size());

		ASSERT_EQ(-2245, list.at(0).getValue());
		ASSERT_EQ(SYP_UNKNOWN, list.at(0).getType());
	}
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpCommandReceive));
}


/**
 * Signal tests
 */

#define SIMPLE_THREADS_SYNC_MICROSEC() usleep(500000)

#define THREAD_SYNC_VARS()\
std::mutex mutex;\
std::condition_variable cond_var;\
bool done(false);

#define THREAD_NOTIFY_ALL()\
WillOnce(InvokeWithoutArgs([&]() {std::lock_guard<std::mutex> lock(mutex);done = true;cond_var.notify_one();}))

#define THREAD_WAIT()\
{\
   std::unique_lock<std::mutex> lock(mutex);\
   auto now = std::chrono::system_clock::now();\
   EXPECT_TRUE(cond_var.wait_until(lock, now + std::chrono::milliseconds(1000), [&done] { return done; }));\
}


MATCHER_P(connectionEqualTo, value, "") {
	am_types::am_MainConnectionType_s lh = arg;
	return lh.getConnectionState() == value.getConnectionState() &&
		   lh.getDelay() == value.getDelay() &&
		   lh.getMainConnectionID() == value.getMainConnectionID()&&
		   lh.getSinkID() == value.getSinkID()&&
		   lh.getSourceID() == value.getSourceID();
}

TEST_F(CAmCommandSenderCAPITest, onNewMainConnection)
{
	ASSERT_TRUE(env->mIsServiceAvailable);
	if(env->mIsServiceAvailable)
	{
		THREAD_SYNC_VARS()

		am_MainConnectionType_s mainConnection;
		mainConnection.connectionState=am_ConnectionState_e::CS_CONNECTING;
		mainConnection.delay=400;
		mainConnection.mainConnectionID=3;
		mainConnection.sinkID=4;
		mainConnection.sourceID=5;
		am_types::am_MainConnectionType_s mainConnectionCAPI;
		mainConnectionCAPI.setConnectionState(CAmConvert2CAPIType(mainConnection.connectionState));
		mainConnectionCAPI.setDelay(mainConnection.delay);
		mainConnectionCAPI.setMainConnectionID(mainConnection.mainConnectionID);
		mainConnectionCAPI.setSinkID(mainConnection.sinkID);
		mainConnectionCAPI.setSourceID(mainConnection.sourceID);
		EXPECT_CALL(*env->mpMockClient, onNewMainConnection(mainConnectionCAPI)).THREAD_NOTIFY_ALL();

		auto t = std::make_tuple(mainConnection);
		env->mpSerializer->doAsyncCall(env->mpPlugin, &CAmCommandSenderCAPI::cbNewMainConnection, t);
		THREAD_WAIT()
	}
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpCommandReceive));
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpMockClient));

}

TEST_F(CAmCommandSenderCAPITest, removedMainConnection)
{
	ASSERT_TRUE(env->mIsServiceAvailable);
	if(env->mIsServiceAvailable)
	{
		MockNotificationsClient mock;
		auto subscription = env->mProxy->getRemovedMainConnectionEvent().subscribe(std::bind(&MockNotificationsClient::removedMainConnection, std::ref(mock), std::placeholders::_1));
		am_mainConnectionID_t mainConnectionID(3);
		am_types::am_mainConnectionID_t mainConnectionIDCAPI(mainConnectionID);
		EXPECT_CALL(mock, removedMainConnection(mainConnectionIDCAPI));
		env->mpPlugin->cbRemovedMainConnection(mainConnectionID);
		SIMPLE_THREADS_SYNC_MICROSEC();
		env->mProxy->getRemovedMainConnectionEvent().unsubscribe(subscription);
	}
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpCommandReceive));
}

TEST_F(CAmCommandSenderCAPITest, onNumberOfSourceClassesChangedEventTest)
{
	ASSERT_TRUE(env->mIsServiceAvailable);
	if(env->mIsServiceAvailable)
	{
		THREAD_SYNC_VARS()
		EXPECT_CALL(*env->mpMockClient, onNumberOfSourceClassesChangedEvent()).THREAD_NOTIFY_ALL();
		auto t = std::make_tuple();
		env->mpSerializer->doAsyncCall(env->mpPlugin, &CAmCommandSenderCAPI::cbNumberOfSourceClassesChanged, t);
		THREAD_WAIT()
	}
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpCommandReceive));
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpMockClient));
}

TEST_F(CAmCommandSenderCAPITest, onMainConnectionStateChangedEventTest)
{
	ASSERT_TRUE(env->mIsServiceAvailable);
	if(env->mIsServiceAvailable)
	{
		MockNotificationsClient mock;
		auto subscription = env->mProxy->getMainConnectionStateChangedEvent().subscribe(std::bind(&MockNotificationsClient::onMainConnectionStateChangedEvent, std::ref(mock),
													   std::placeholders::_1, std::placeholders::_2));
		EXPECT_CALL(mock, onMainConnectionStateChangedEvent(101, am_types::am_ConnectionState_e(am_types::am_ConnectionState_e::CS_SUSPENDED)));
		env->mpPlugin->cbMainConnectionStateChanged(101, CS_SUSPENDED);
		SIMPLE_THREADS_SYNC_MICROSEC();
		env->mProxy->getMainConnectionStateChangedEvent().unsubscribe(subscription);
	}
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpCommandReceive));
}

TEST_F(CAmCommandSenderCAPITest, onSourceAddedEventTest)
{
	ASSERT_TRUE(env->mIsServiceAvailable);
	if(env->mIsServiceAvailable)
	{
		MockNotificationsClient mock;
		auto subscription = env->mProxy->getNewSourceEvent().subscribe(std::bind(&MockNotificationsClient::onSourceAddedEvent, std::ref(mock),std::placeholders::_1));
		am_types::am_SourceType_s destination;
		destination.setSourceID(100);
		destination.setName("Name");
		am_types::am_Availability_e ave(am_types::am_Availability_e::A_MAX);
		am_types::am_AvailabilityReason_pe avr(0);
		destination.setAvailability(am_types::am_Availability_s(ave, avr));
		destination.setSourceClassID(200);

		am_SourceType_s origin;
		origin.sourceID = 100;
		origin.name = "Name";
		origin.availability.availability = A_MAX;
		origin.availability.availabilityReason = 0;
 		origin.sourceClassID = 200;
		EXPECT_CALL(mock, onSourceAddedEvent(destination));
		env->mpPlugin->cbNewSource(origin);
		SIMPLE_THREADS_SYNC_MICROSEC();
		env->mProxy->getNewSourceEvent().unsubscribe(subscription);
	}
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpCommandReceive));
}

TEST_F(CAmCommandSenderCAPITest, onSourceRemovedEventTest)
{
	ASSERT_TRUE(env->mIsServiceAvailable);
	if(env->mIsServiceAvailable)
	{
		MockNotificationsClient mock;
		auto subscription = env->mProxy->getRemovedSourceEvent().subscribe(std::bind(&MockNotificationsClient::onSourceRemovedEvent, std::ref(mock),
													   std::placeholders::_1));
		am_sourceID_t source = 101;
		EXPECT_CALL(mock, onSourceRemovedEvent(source));
		env->mpPlugin->cbRemovedSource(source);
		SIMPLE_THREADS_SYNC_MICROSEC();
		env->mProxy->getRemovedSourceEvent().unsubscribe(subscription);
	}
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpCommandReceive));
}

TEST_F(CAmCommandSenderCAPITest, onMainSourceSoundPropertyChangedEventTest)
{
	ASSERT_TRUE(env->mIsServiceAvailable);
	if(env->mIsServiceAvailable)
	{
		MockNotificationsClient mock;
		auto subscription = env->mProxy->getMainSourceSoundPropertyChangedEvent().subscribe(std::bind(&MockNotificationsClient::onMainSourceSoundPropertyChangedEvent, std::ref(mock),
													   std::placeholders::_1, std::placeholders::_2));

		am_MainSoundProperty_s soundProperty;
		soundProperty.value = 10;
		soundProperty.type = MSP_UNKNOWN;

		am_types::am_MainSoundProperty_s destination(MSP_UNKNOWN, 10);

		EXPECT_CALL(mock, onMainSourceSoundPropertyChangedEvent(101, destination));
		env->mpPlugin->cbMainSourceSoundPropertyChanged(101, soundProperty);
		SIMPLE_THREADS_SYNC_MICROSEC();
		env->mProxy->getMainSourceSoundPropertyChangedEvent().unsubscribe(subscription);
	}
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpCommandReceive));
}

TEST_F(CAmCommandSenderCAPITest, onSourceAvailabilityChangedEventTest)
{
	ASSERT_TRUE(env->mIsServiceAvailable);
	if(env->mIsServiceAvailable)
	{
		MockNotificationsClient mock;
		auto subscription = env->mProxy->getSourceAvailabilityChangedEvent().subscribe(std::bind(&MockNotificationsClient::onSourceAvailabilityChangedEvent, std::ref(mock),
													   std::placeholders::_1, std::placeholders::_2));

		am_Availability_s availability;
		availability.availability = A_MAX;
		availability.availabilityReason = AR_UNKNOWN;

		am_types::am_Availability_s destination(am_types::am_Availability_e::A_MAX, AR_UNKNOWN);

		EXPECT_CALL(mock, onSourceAvailabilityChangedEvent(101, destination));
		env->mpPlugin->cbSourceAvailabilityChanged(101, availability);
		SIMPLE_THREADS_SYNC_MICROSEC();
		env->mProxy->getSourceAvailabilityChangedEvent().unsubscribe(subscription);
	}
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpCommandReceive));
}

TEST_F(CAmCommandSenderCAPITest, onNumberOfSinkClassesChangedEventTest)
{
	ASSERT_TRUE(env->mIsServiceAvailable);
	if(env->mIsServiceAvailable)
	{
		MockNotificationsClient mock;
		auto subscription = env->mProxy->getNumberOfSinkClassesChangedEvent().subscribe(std::bind(&MockNotificationsClient::onNumberOfSinkClassesChangedEvent, std::ref(mock)));
		EXPECT_CALL(mock, onNumberOfSinkClassesChangedEvent());
		env->mpPlugin->cbNumberOfSinkClassesChanged();
		SIMPLE_THREADS_SYNC_MICROSEC();
		env->mProxy->getNumberOfSinkClassesChangedEvent().unsubscribe(subscription);
	}
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpCommandReceive));
}

TEST_F(CAmCommandSenderCAPITest, onSinkAddedEventTest)
{
	ASSERT_TRUE(env->mIsServiceAvailable);
	if(env->mIsServiceAvailable)
	{
		MockNotificationsClient mock;
		auto subscription = env->mProxy->getNewSinkEvent().subscribe(std::bind(&MockNotificationsClient::onSinkAddedEvent, std::ref(mock),
													   std::placeholders::_1));
		am_types::am_SinkType_s destination;
		destination.setSinkID(100);
		destination.setName("Name");

		am_types::am_Availability_e ave(am_types::am_Availability_e::A_MAX);
		am_types::am_AvailabilityReason_pe avr(0);
		destination.setAvailability(am_types::am_Availability_s(ave, avr));

		destination.setMuteState(am_types::am_MuteState_e(am_types::am_MuteState_e::MS_MAX));
		destination.setVolume(1);
		destination.setSinkClassID(100);

		am_SinkType_s origin;
		origin.sinkID = 100;
		origin.name = "Name";
		origin.availability.availability = A_MAX;
		origin.availability.availabilityReason = AR_UNKNOWN;
 		origin.muteState = am_MuteState_e::MS_MAX;
 		origin.volume = 1;
 		origin.sinkClassID = 100;

		EXPECT_CALL(mock, onSinkAddedEvent(destination));
		env->mpPlugin->cbNewSink(origin);
		SIMPLE_THREADS_SYNC_MICROSEC();
		env->mProxy->getNewSinkEvent().unsubscribe(subscription);
	}
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpCommandReceive));
}

TEST_F(CAmCommandSenderCAPITest, onSinkRemovedEventTest)
{
	ASSERT_TRUE(env->mIsServiceAvailable);
	if(env->mIsServiceAvailable)
	{
		MockNotificationsClient mock;
		auto subscription = env->mProxy->getRemovedSinkEvent().subscribe(std::bind(&MockNotificationsClient::onSinkRemovedEvent, std::ref(mock),
													   std::placeholders::_1));
		EXPECT_CALL(mock, onSinkRemovedEvent(101));
		env->mpPlugin->cbRemovedSink(101);
		SIMPLE_THREADS_SYNC_MICROSEC();
		env->mProxy->getRemovedSinkEvent().unsubscribe(subscription);
	}
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpCommandReceive));
}

TEST_F(CAmCommandSenderCAPITest, onMainSinkSoundPropertyChangedEventTest)
{
	ASSERT_TRUE(env->mIsServiceAvailable);
	if(env->mIsServiceAvailable)
	{
		MockNotificationsClient mock;
		auto subscription = env->mProxy->getMainSinkSoundPropertyChangedEvent().subscribe(std::bind(&MockNotificationsClient::onMainSinkSoundPropertyChangedEvent, std::ref(mock),
													   std::placeholders::_1, std::placeholders::_2));

		am_MainSoundProperty_s soundProperty;
		soundProperty.value = 10;
		soundProperty.type = MSP_UNKNOWN;

		am_types::am_MainSoundProperty_s destination(MSP_UNKNOWN, 10);

		EXPECT_CALL(mock, onMainSinkSoundPropertyChangedEvent(101, destination));
		env->mpPlugin->cbMainSinkSoundPropertyChanged(101, soundProperty);
		SIMPLE_THREADS_SYNC_MICROSEC();
		env->mProxy->getMainSinkSoundPropertyChangedEvent().unsubscribe(subscription);
	}
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpCommandReceive));
}

TEST_F(CAmCommandSenderCAPITest, onSinkAvailabilityChangedEventTest)
{
	ASSERT_TRUE(env->mIsServiceAvailable);
	if(env->mIsServiceAvailable)
	{
		MockNotificationsClient mock;
		auto subscription = env->mProxy->getSinkAvailabilityChangedEvent().subscribe(std::bind(&MockNotificationsClient::onSinkAvailabilityChangedEvent, std::ref(mock),
													   std::placeholders::_1, std::placeholders::_2));

		am_Availability_s availability;
		availability.availability = A_MAX;
		availability.availabilityReason = AR_UNKNOWN;

		am_types::am_Availability_s destination(am_types::am_Availability_e::A_MAX, AR_UNKNOWN);

		EXPECT_CALL(mock, onSinkAvailabilityChangedEvent(101, destination));
		env->mpPlugin->cbSinkAvailabilityChanged(101, availability);
		SIMPLE_THREADS_SYNC_MICROSEC();
		env->mProxy->getSinkAvailabilityChangedEvent().unsubscribe(subscription);
	}
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpCommandReceive));
}

TEST_F(CAmCommandSenderCAPITest, onVolumeChangedEventTest)
{
	ASSERT_TRUE(env->mIsServiceAvailable);
	if(env->mIsServiceAvailable)
	{
		MockNotificationsClient mock;
		auto subscription = env->mProxy->getVolumeChangedEvent().subscribe(std::bind(&MockNotificationsClient::onVolumeChangedEvent, std::ref(mock),
													   std::placeholders::_1, std::placeholders::_2));
		EXPECT_CALL(mock, onVolumeChangedEvent(101, 4));
		env->mpPlugin->cbVolumeChanged(101, 4);
		SIMPLE_THREADS_SYNC_MICROSEC();
		env->mProxy->getVolumeChangedEvent().unsubscribe(subscription);
	}
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpCommandReceive));
}

TEST_F(CAmCommandSenderCAPITest, onSinkMuteStateChangedEventTest)
{
	ASSERT_TRUE(env->mIsServiceAvailable);
	if(env->mIsServiceAvailable)
	{
		MockNotificationsClient mock;
		auto subscription = env->mProxy->getSinkMuteStateChangedEvent().subscribe(std::bind(&MockNotificationsClient::onSinkMuteStateChangedEvent, std::ref(mock),
													   std::placeholders::_1, std::placeholders::_2));
		EXPECT_CALL(mock, onSinkMuteStateChangedEvent(101, am_types::am_MuteState_e(am_types::am_MuteState_e::MS_MAX)));
		env->mpPlugin->cbSinkMuteStateChanged(101, am_MuteState_e::MS_MAX);
		SIMPLE_THREADS_SYNC_MICROSEC();
		env->mProxy->getSinkMuteStateChangedEvent().unsubscribe(subscription);
	}
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpCommandReceive));
}

TEST_F(CAmCommandSenderCAPITest, onSystemPropertyChangedEventTest)
{
	ASSERT_TRUE(env->mIsServiceAvailable);
	if(env->mIsServiceAvailable)
	{
		MockNotificationsClient mock;
		auto subscription = env->mProxy->getSystemPropertyChangedEvent().subscribe(std::bind(&MockNotificationsClient::onSystemPropertyChangedEvent, std::ref(mock),
													   std::placeholders::_1));

		am_types::am_SystemProperty_s value(static_cast<am_types::am_SystemPropertyType_pe>(SYP_UNKNOWN), (const int16_t)2);
		am_SystemProperty_s systemProperty;
		systemProperty.value = 2;
		systemProperty.type = SYP_UNKNOWN;

		EXPECT_CALL(mock, onSystemPropertyChangedEvent(value));
		env->mpPlugin->cbSystemPropertyChanged(systemProperty);
		SIMPLE_THREADS_SYNC_MICROSEC();
		env->mProxy->getSystemPropertyChangedEvent().unsubscribe(subscription);
	}
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpCommandReceive));
}

TEST_F(CAmCommandSenderCAPITest, onTimingInformationChangedEventTest)
{
	ASSERT_TRUE(env->mIsServiceAvailable);
	if(env->mIsServiceAvailable)
	{
		MockNotificationsClient mock;
		auto subscription = env->mProxy->getTimingInformationChangedEvent().subscribe(std::bind(&MockNotificationsClient::onTimingInformationChangedEvent, std::ref(mock),
													   std::placeholders::_1, std::placeholders::_2));

		EXPECT_CALL(mock, onTimingInformationChangedEvent(1, 2));
		env->mpPlugin->cbTimingInformationChanged(1, 2);
		SIMPLE_THREADS_SYNC_MICROSEC();
		env->mProxy->getTimingInformationChangedEvent().unsubscribe(subscription);
	}
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpCommandReceive));
}

TEST_F(CAmCommandSenderCAPITest, onSinkUpdatedEventTest)
{
	ASSERT_TRUE(env->mIsServiceAvailable);
	if(env->mIsServiceAvailable)
	{
		MockNotificationsClient mock;
		auto subscription = env->mProxy->getSinkUpdatedEvent().subscribe(std::bind(&MockNotificationsClient::onSinkUpdatedEvent, std::ref(mock),
													   std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
		std::vector<am_MainSoundProperty_s> listMainSoundProperties;
		am_MainSoundProperty_s prop;
		prop.value = 1;
		prop.type = MSP_UNKNOWN;
		listMainSoundProperties.push_back(prop);
		EXPECT_CALL(mock, onSinkUpdatedEvent(1, 2, _));
		env->mpPlugin->cbSinkUpdated(1, 2, listMainSoundProperties);
		SIMPLE_THREADS_SYNC_MICROSEC();
		env->mProxy->getSinkUpdatedEvent().unsubscribe(subscription);
	}
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpCommandReceive));
}

TEST_F(CAmCommandSenderCAPITest, onSourceUpdatedTest)
{
	ASSERT_TRUE(env->mIsServiceAvailable);
	if(env->mIsServiceAvailable)
	{
		MockNotificationsClient mock;
		auto subscription = env->mProxy->getSourceUpdatedEvent().subscribe(std::bind(&MockNotificationsClient::onSourceUpdatedEvent, std::ref(mock),
													   std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
		std::vector<am_MainSoundProperty_s> listMainSoundProperties;
		am_MainSoundProperty_s prop;
		prop.value = 1;
		prop.type = MSP_UNKNOWN;
		listMainSoundProperties.push_back(prop);
		EXPECT_CALL(mock, onSourceUpdatedEvent(1, 2, _));
		env->mpPlugin->cbSourceUpdated(1, 2, listMainSoundProperties);
		SIMPLE_THREADS_SYNC_MICROSEC();
		env->mProxy->getSourceUpdatedEvent().unsubscribe(subscription);
	}
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpCommandReceive));
}

TEST_F(CAmCommandSenderCAPITest, onSinkNotificationEventTest)
{
	ASSERT_TRUE(env->mIsServiceAvailable);
	if(env->mIsServiceAvailable)
	{
		MockNotificationsClient mock;
		auto subscription = env->mProxy->getSinkNotificationEvent().subscribe(std::bind(&MockNotificationsClient::onSinkNotificationEvent, std::ref(mock),
													   std::placeholders::_1, std::placeholders::_2));
		am_NotificationPayload_s orig;
		orig.type = NT_UNKNOWN;
		orig.value = 1;
		am_types::am_NotificationPayload_s dest;
		dest.setType(NT_UNKNOWN);
		dest.setValue(1);

		EXPECT_CALL(mock, onSinkNotificationEvent(1, dest));
		env->mpPlugin->cbSinkNotification(1, orig);
		SIMPLE_THREADS_SYNC_MICROSEC();
		env->mProxy->getSinkNotificationEvent().unsubscribe(subscription);
	}
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpCommandReceive));
}


TEST_F(CAmCommandSenderCAPITest, onSourceNotificationEventTest)
{
	ASSERT_TRUE(env->mIsServiceAvailable);
	if(env->mIsServiceAvailable)
	{
		MockNotificationsClient mock;
		auto subscription = env->mProxy->getSourceNotificationEvent().subscribe(std::bind(&MockNotificationsClient::onSourceNotificationEvent, std::ref(mock),
													   std::placeholders::_1, std::placeholders::_2));
		am_NotificationPayload_s orig;
		orig.type = NT_UNKNOWN;
		orig.value = 1;
		am_types::am_NotificationPayload_s dest;
		dest.setType(NT_UNKNOWN);
		dest.setValue(1);

		EXPECT_CALL(mock, onSourceNotificationEvent(1, dest));
		env->mpPlugin->cbSourceNotification(1, orig);
		SIMPLE_THREADS_SYNC_MICROSEC();
		env->mProxy->getSourceNotificationEvent().unsubscribe(subscription);
	}
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpCommandReceive));
}

TEST_F(CAmCommandSenderCAPITest, onMainSinkNotificationConfigurationChangedEventTest)
{
	ASSERT_TRUE(env->mIsServiceAvailable);
	if(env->mIsServiceAvailable)
	{
		MockNotificationsClient mock;
		auto subscription = env->mProxy->getMainSinkNotificationConfigurationChangedEvent().subscribe(std::bind(&MockNotificationsClient::onMainSinkNotificationConfigurationChangedEvent, std::ref(mock),
													   std::placeholders::_1, std::placeholders::_2));
		am_NotificationConfiguration_s orig;
		orig.type = NT_UNKNOWN;
		orig.parameter = 1;
		orig.status = am_NotificationStatus_e::NS_MAX;
		am_types::am_NotificationConfiguration_s dest;
		dest.setType(NT_UNKNOWN);
		dest.setParameter(1);
		dest.setStatus(am_types::am_NotificationStatus_e(am_types::am_NotificationStatus_e::NS_MAX));

		EXPECT_CALL(mock, onMainSinkNotificationConfigurationChangedEvent(1, dest));
		env->mpPlugin->cbMainSinkNotificationConfigurationChanged(1, orig);
		SIMPLE_THREADS_SYNC_MICROSEC();
		env->mProxy->getMainSinkNotificationConfigurationChangedEvent().unsubscribe(subscription);
	}
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpCommandReceive));
}

TEST_F(CAmCommandSenderCAPITest, onMainSourceNotificationConfigurationChangedEventTest)
{
	ASSERT_TRUE(env->mIsServiceAvailable);
	if(env->mIsServiceAvailable)
	{
		MockNotificationsClient mock;
		auto subscription = env->mProxy->getMainSourceNotificationConfigurationChangedEvent().subscribe(std::bind(&MockNotificationsClient::onMainSourceNotificationConfigurationChangedEvent, std::ref(mock),
													   std::placeholders::_1, std::placeholders::_2));
		am_NotificationConfiguration_s orig;
		orig.type = NT_UNKNOWN;
		orig.parameter = 1;
		orig.status = am_NotificationStatus_e::NS_MAX;
		am_types::am_NotificationConfiguration_s dest;
		dest.setType(NT_UNKNOWN);
		dest.setParameter(1);
		dest.setStatus(am_types::am_NotificationStatus_e(am_types::am_NotificationStatus_e::NS_MAX));

		EXPECT_CALL(mock, onMainSourceNotificationConfigurationChangedEvent(1, dest));
		env->mpPlugin->cbMainSourceNotificationConfigurationChanged(1, orig);
		SIMPLE_THREADS_SYNC_MICROSEC();
		env->mProxy->getMainSourceNotificationConfigurationChangedEvent().unsubscribe(subscription);
	}
	EXPECT_TRUE(Mock::VerifyAndClearExpectations(env->mpCommandReceive));
}

