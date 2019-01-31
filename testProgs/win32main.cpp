/*
	����������	һ���򵥵�RTSP/RTP�Խӹ��ܣ���RTSPԴͨ��������RTSPClient���̣���ȡ��RTP��ý������
				��ͨ����׼RTSP���͹��̣�ANNOUNCE/SETUP/PLAY��������ȡ��RTP�������͸�Darwin��ý��
				�ַ���������
				��Demoֻ��ʾ�˵���Դ��ת�������͹���!
				
	Author��	sunpany@qq.com
	ʱ�䣺		2014/06/25
*/

#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"
#include "RTSPCommon.hh"

char* server = "www.easydss.com";	//RTSP��ý��ת����������ַ��<���޸�Ϊ�Լ������ý���������ַ>
int port = 8554;					//RTSP��ý��ת���������˿ڣ�<���޸�Ϊ�Լ������ý��������˿�>
char* streamName = "live.sdp";		//�����ƣ����͵�Darwin�������Ʊ�����.sdp��β
char* src = "rtsp://218.204.223.237:554/live/1/66251FC11353191F/e7ooqwcfbqjoo80j.sdp";	//Դ��URL

UsageEnvironment* env = NULL;		//live555 global environment
TaskScheduler* scheduler = NULL;
char eventLoopWatchVariable = 0;

DarwinInjector* injector = NULL;	//DarwinInjector
FramedSource* vSource = NULL;		//Video Source
FramedSource* aSource = NULL;		//Audio Source

RTPSink* vSink = NULL;				//Video Sink
RTPSink* aSink = NULL;				//Audio Sink

Groupsock* rtpGroupsockVideo = NULL;//Video Socket
Groupsock* rtpGroupsockAudio = NULL;//Audio Socket

ProxyServerMediaSession* sms = NULL;//proxy session

// ��ת������
bool RedirectStream(char const* ip, unsigned port);

// ��ת����������ص�
void afterPlaying(void* clientData);

// ʵ�ֵȴ�����
void sleep(void* clientSession)  
{
	char* var = (char*)clientSession;
    *var = ~0; 
}  

// Main
int main(int argc, char** argv) 
{
	// ��ʼ��������live555����
	scheduler = BasicTaskScheduler::createNew();
	env = BasicUsageEnvironment::createNew(*scheduler);

	// �½�ת��SESSION
	sms = ProxyServerMediaSession::createNew(*env, NULL, src);
	
	// ѭ���ȴ�ת�ӳ�����Դ�����ӳɹ�
	while(sms->numSubsessions() <= 0 )
	{
		char fWatchVariable  = 0;  
		env->taskScheduler().scheduleDelayedTask(2*1000000,(TaskFunc*)sleep,&fWatchVariable);  
		env->taskScheduler().doEventLoop(&fWatchVariable);  
	}
	
	// ��ʼת������
	RedirectStream(server, port);

	env->taskScheduler().doEventLoop(&eventLoopWatchVariable);

	return 0;
}


// ������Ƶ����ý�������
bool RedirectStream(char const* ip, unsigned port)
{
	// ת��SESSION���뱣֤����
	if( sms == NULL) return false;

	// �ж�sms�Ƿ��Ѿ�������Դ��
	if( sms->numSubsessions() <= 0 ) 
	{
		*env << "sms numSubsessions() == 0\n";
		return false;
	}

	// DarwinInjector��Ҫ������Darwin����RTSP/RTP����
	injector = DarwinInjector::createNew(*env);

	struct in_addr dummyDestAddress;
	dummyDestAddress.s_addr = 0;
	rtpGroupsockVideo = new Groupsock(*env, dummyDestAddress, 0, 0);

	struct in_addr dummyDestAddressAudio;
	dummyDestAddressAudio.s_addr = 0;
	rtpGroupsockAudio = new Groupsock(*env, dummyDestAddressAudio, 0, 0);

	ServerMediaSubsession* subsession = NULL;
	ServerMediaSubsessionIterator iter(*sms);
    while ((subsession = iter.next()) != NULL)
	{
		ProxyServerMediaSubsession* proxySubsession = (ProxyServerMediaSubsession*)subsession;
						
		unsigned streamBitrate;
		FramedSource* source = proxySubsession->createNewStreamSource(1, streamBitrate);
		
		if (strcmp(proxySubsession->mediumName(), "video") == 0)
		{
			// ��ProxyServerMediaSubsession����Video��RTPSource
			vSource = source;
			unsigned char rtpPayloadType = proxySubsession->rtpPayloadFormat();
			// ����Video��RTPSink
			vSink = proxySubsession->createNewRTPSink(rtpGroupsockVideo,rtpPayloadType,source);
			// ��Video��RTPSink��ֵ��DarwinInjector��������ƵRTP��Darwin
			injector->addStream(vSink,NULL);
		}
		else
		{
			// ��ProxyServerMediaSubsession����Audio��RTPSource
			aSource = source;
			unsigned char rtpPayloadType = proxySubsession->rtpPayloadFormat();
			// ����Audio��RTPSink
			aSink = proxySubsession->createNewRTPSink(rtpGroupsockVideo,rtpPayloadType,source);
			// ��Audio��RTPSink��ֵ��DarwinInjector��������ƵRTP��Darwin
			injector->addStream(aSink,NULL);
		}
    }

	// RTSP ANNOUNCE/SETUP/PLAY���͹���
	if (!injector->setDestination(ip, streamName, "live555", "LIVE555", port)) 
	{
		*env << "injector->setDestination() failed: " << env->getResultMsg() << "\n";
		return false;
	}

	// ��ʼת����ƵRTP����
	if((vSink != NULL) && (vSource != NULL))
		vSink->startPlaying(*vSource,afterPlaying,vSink);

	// ��ʼת����ƵRTP����
	if((aSink != NULL) && (aSource != NULL))
		aSink->startPlaying(*aSource,afterPlaying,aSink);

	*env << "\nBeginning to get camera video...\n";
	return true;
}


// ֹͣ���ͣ��ͷ����б���
void afterPlaying(void* clientData) 
{
	if( clientData == NULL ) return;

	if(vSink != NULL)
		vSink->stopPlaying();

	if(aSink != NULL)
		aSink->stopPlaying();

	if(injector != NULL)
	{
		Medium::close(*env, injector->name());
		injector == NULL;
	}

	ServerMediaSubsession* subsession = NULL;
	ServerMediaSubsessionIterator iter(*sms);
    while ((subsession = iter.next()) != NULL)
	{
		ProxyServerMediaSubsession* proxySubsession = (ProxyServerMediaSubsession*)subsession;
		if (strcmp(proxySubsession->mediumName(), "video") == 0)
			proxySubsession->closeStreamSource(vSource);

		else
			proxySubsession->closeStreamSource(aSource);
	}

	if(vSink != NULL)
		Medium::close(vSink);

	if(aSink != NULL)
		Medium::close(aSink);

	if(vSource != NULL)
		Medium::close(vSource);

	if(aSource != NULL)
		Medium::close(aSource);

	delete rtpGroupsockVideo;
	rtpGroupsockVideo = NULL;
	delete rtpGroupsockAudio;
	rtpGroupsockAudio = NULL;
}