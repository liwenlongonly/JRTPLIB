//
// Created by jqq on 2022/8/12.
//
#include "rtpconfig.h"
#include "rtpsocketutil.h"
#include "rtpsocketutilinternal.h"
#include "rtpsession.h"
#include "rtpsessionparams.h"
#include "rtperrors.h"
#include "rtpsourcedata.h"
#include "rtptcpaddress.h"
#include "rtptcptransmitter.h"
#include "rtppacket.h"
#include "rtpselect.h"

#include <string.h>
#include <stdlib.h>
#include <iostream>
#include <vector>
#include <thread>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>


#ifndef LOGLEVEL
#define LOGLEVEL DEBUG
#endif

using namespace std;
using namespace jrtplib;

#ifdef __cplusplus
extern "C" {
#endif

enum LogLevel
{
    ERROR1 = 0,
    WARN  = 1,
    INFO  = 2,
    DEBUG = 3,
};

void mylog1(const char* filename, int line, enum LogLevel level, const char* fmt, ...) __attribute__((format(printf,4,5)));

#define Log(level, format, ...) mylog1(__FILE__, __LINE__, level, format, ## __VA_ARGS__)

#ifdef __cplusplus
};
#endif

// 使用了GNU C扩展语法，只在gcc（C语言）生效，
// g++的c++版本编译不通过
static const char* s_loginfo[] = {
        [ERROR1] = "ERROR",
        [WARN]  = "WARN",
        [INFO]  = "INFO",
        [DEBUG] = "DEBUG",
};

static void get_timestamp(char *buffer)
{
    time_t t;
    struct tm *p;
    struct timeval tv;
    int len;
    int millsec;

    t = time(NULL);
    p = localtime(&t);

    gettimeofday(&tv, NULL);
    millsec = (int)(tv.tv_usec / 1000);

    /* 时间格式：[2011-11-15 12:47:34:888] */
    len = snprintf(buffer, 32, "[%04d-%02d-%02d %02d:%02d:%02d:%03d] ",
                   p->tm_year+1900, p->tm_mon+1,
                   p->tm_mday, p->tm_hour, p->tm_min, p->tm_sec, millsec);

    buffer[len] = '\0';
}

void mylog1(const char* filename, int line, enum LogLevel level, const char* fmt, ...)
{
    if(level > LOGLEVEL)
        return;

    va_list arg_list;
    char buf[1024];
    memset(buf, 0, 1024);
    va_start(arg_list, fmt);
    vsnprintf(buf, 1024, fmt, arg_list);
    char time[32] = {0};

    // 去掉*可能*存在的目录路径，只保留文件名
    const char* tmp = strrchr(filename, '/');
    if (!tmp) tmp = filename;
    else tmp++;
    get_timestamp(time);

    switch(level){
        case DEBUG:
            //绿色
            printf("\033[1;32m%s[%s] [%s:%d] %s\n\033[0m", time, s_loginfo[level], tmp, line, buf);
            break;
        case INFO:
            //蓝色
            printf("\033[1;34m%s[%s] [%s:%d] %s\n\033[0m", time, s_loginfo[level], tmp, line, buf);
            break;
        case ERROR1:
            //红色
            printf("\033[1;31m%s[%s] [%s:%d] %s\n\033[0m", time, s_loginfo[level], tmp, line, buf);
            break;
        case WARN:
            //黄色
            printf("\033[1;33m%s[%s] [%s:%d] %s\n\033[0m", time, s_loginfo[level], tmp, line, buf);
            break;
    }
    va_end(arg_list);
}

inline void checkerror(int rtperr)
{
    if (rtperr < 0)
    {
        cerr << "ERROR: " << RTPGetErrorString(rtperr) << std::endl;
        exit(-1);
    }
}

bool is_not_done = true;

class MyRTPSession : public RTPSession
{
public:
    MyRTPSession() : RTPSession() { }
    ~MyRTPSession() { }
protected:
    void OnValidatedRTPPacket(RTPSourceData *srcdat, RTPPacket *rtppack, bool isonprobation, bool *ispackethandled)
    {
        printf("SSRC %x Got packet (%d bytes) in OnValidatedRTPPacket from source 0x%04x!\n", GetLocalSSRC(),
               (int)rtppack->GetPayloadLength(), srcdat->GetSSRC());
        DeletePacket(rtppack);
        *ispackethandled = true;
    }

    void OnRTCPSDESItem(RTPSourceData *srcdat, RTCPSDESPacket::ItemType t, const void *itemdata, size_t itemlength)
    {
        char msg[1024];

        memset(msg, 0, sizeof(msg));
        if (itemlength >= sizeof(msg))
            itemlength = sizeof(msg)-1;

        memcpy(msg, itemdata, itemlength);
        printf("SSRC %x Received SDES item (%d): %s from SSRC %x\n", GetLocalSSRC(), (int)t, msg, srcdat->GetSSRC());
    }
};

class MyTCPTransmitter : public RTPTCPTransmitter
{
public:
    MyTCPTransmitter(const string &name) : RTPTCPTransmitter(0), m_name(name) { }

    void OnSendError(SocketType sock)
    {
        is_not_done = false;
        Log(DEBUG, "Error sending over socket %d, removing destination", sock);
        DeleteDestination(RTPTCPAddress(sock));
    }

    void OnReceiveError(SocketType sock)
    {
        is_not_done = false;
        Log(DEBUG, "Error sending over socket %d, removing destination", sock);
        DeleteDestination(RTPTCPAddress(sock));
    }
private:
    string m_name;
};

int clientSend(){

    RTPSession session;
    RTPAbortDescriptors m_descriptors;

    RTPSessionParams sessionparams;
    sessionparams.SetAcceptOwnPackets(true);
    sessionparams.SetOwnTimestampUnit(1.0/10.0);

    m_descriptors.Init();

    RTPTCPTransmissionParams transparams;
    transparams.SetCreatedAbortDescriptors(&m_descriptors);
    int status = session.Create(sessionparams,&transparams,RTPTransmitter::TCPProto);

    if (status < 0)
    {
        //std::cerr << "[session.Create]" << QString::fromStdString(RTPGetErrorString(status));
        std::cout << "ERROR: " << RTPGetErrorString(status) << std::endl;
        return -1;
    }

    //初始化socket
    SocketType sockSrv = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addrSrv;
    addrSrv.sin_addr.s_addr = inet_addr("127.0.0.1");
    addrSrv.sin_family = AF_INET;
    addrSrv.sin_port = htons(15000);

    //连接服务器
    connect( sockSrv, (struct sockaddr*)&addrSrv, sizeof(struct sockaddr));

    RTPTCPAddress addr(sockSrv);

    status = session.AddDestination(addr);
    if (status < 0)
    {
        //std::cerr << "session.AddDestination" << QString::fromStdString(RTPGetErrorString(status));
        std::cout << "ERROR: " << RTPGetErrorString(status) << std::endl;
        return -1;
    }

    session.SetDefaultPayloadType(96);
    session.SetDefaultMark(false);
    session.SetDefaultTimestampIncrement(160);

    //发送数据
    std::vector<uint8_t> pack(1500);
    int len = 1200;

    int num = 5;
    for (int i = 1 ; i <= num ; i++)
    {
        session.SendPacket((void *)&pack[0],len,0,false,10);
        Log(DEBUG,"Sending packet %d/%d",i,num);
        RTPTime::Wait(RTPTime(3,0));
    }
    session.BYEDestroy(RTPTime(10,0),0,0);

    RTPCLOSE(sockSrv);
}

vector<SocketType> m_sockets;

int serverRecv(){
    // Create a listener socket and listen on it
    SocketType listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener == RTPSOCKERR)
    {
        cerr << "Can't create listener socket" << endl;
        return -1;
    }

    struct sockaddr_in servAddr;

    memset(&servAddr, 0, sizeof(servAddr));
    servAddr.sin_family = AF_INET;
    servAddr.sin_port = htons(15000);

    if (bind(listener, (struct sockaddr *)&servAddr, sizeof(servAddr)) != 0)
    {
        cerr << "Can't bind listener socket" << endl;
        return -1;
    }

    listen(listener, 1);

    // 阻塞方式监听端口
    SocketType server = accept(listener, 0, 0);
    if (server == RTPSOCKERR)
    {
        cerr << "Can't accept incoming connection" << endl;
        return -1;
    }
    m_sockets.push_back(server);
    RTPCLOSE(listener);

    cout << "Got connected socket pair" << endl;

    const int packSize = 1500;
    RTPSessionParams sessParams;
    MyTCPTransmitter trans2("serverRecv");
    RTPSession sess2;

    sessParams.SetProbationType(RTPSources::NoProbation);
    sessParams.SetOwnTimestampUnit(1.0/packSize);
    sessParams.SetMaximumPacketSize(packSize + 64); // some extra room for rtp header

    bool threadsafe = false;
#ifdef RTP_SUPPORT_THREAD
    threadsafe = true;
#endif // RTP_SUPPORT_THREAD


    checkerror(trans2.Init(threadsafe));

    checkerror(trans2.Create(65535, 0));

    checkerror(sess2.Create(sessParams, &trans2));
    cout << "Session 2 created " << endl;

    checkerror(sess2.AddDestination(RTPTCPAddress(server)));

    vector<uint8_t> pack(packSize);
    vector<int8_t> flags(m_sockets.size());

    while(is_not_done)
    {
        sess2.IsActive();
        RTPTime waitTime(1);
        //cout << "Waiting at most " << minInt << " seconds in select" << endl;
        int status = RTPSelect(&m_sockets[0], &flags[0], m_sockets.size(), waitTime);
        checkerror(status);
        if(status > 0){
#ifndef RTP_SUPPORT_THREAD
            checkerror(sess2.Poll());
#endif // RTP_SUPPORT_THREAD
            sess2.BeginDataAccess();
            if (sess2.GotoFirstSourceWithData())
            {
                do
                {
                    RTPPacket *pack;
                    while ((pack = sess2.GetNextPacket()) != NULL)
                    {
                        // You can examine the data here
                        Log(DEBUG,"Got packet ! ");
                        // we don't longer need the packet, so
                        // we'll delete it
                        sess2.DeletePacket(pack);
                    }
                } while (sess2.GotoNextSourceWithData());
            }else{
                Log(DEBUG,"RTPTime::Wait ! ");
                RTPTime::Wait(RTPTime(1,0));
            }
            sess2.EndDataAccess();
        }
        Log(DEBUG, "Loop Event finish! status:%d", status);
    }
    sess2.BYEDestroy(RTPTime(10,0),0,0);
    return 0;
}

int main(int argc, char *argv[]){
#ifdef RTP_SOCKETTYPE_WINSOCK
    WSADATA dat;
    WSAStartup(MAKEWORD(2,2),&dat);
#endif

    std::thread t1(serverRecv);
    std::thread t2(clientSend);

    t2.join();
    t1.join();

    return 0;
}