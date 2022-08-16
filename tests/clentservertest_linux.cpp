//
// Created by ilong on 2022/8/12.
//
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
#include "rtpudpv4transmitter.h"

#include <string.h>
#include <stdlib.h>
#include <iostream>
#include <vector>
#include <thread>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <netinet/tcp.h>

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

#define SERVER_PORT 9001

int tcpSendClient(){

    RTPSession session;
    RTPAbortDescriptors m_descriptors;

    RTPSessionParams sessionparams;
    sessionparams.SetAcceptOwnPackets(true);
    sessionparams.SetOwnTimestampUnit(1.0/10.0);

    m_descriptors.Init();

    RTPTCPTransmissionParams transparams;
    transparams.SetCreatedAbortDescriptors(&m_descriptors);
    int status = session.Create(sessionparams,&transparams,RTPTransmitter::TCPProto);

    if (status < 0){
        //std::cerr << "[session.Create]" << QString::fromStdString(RTPGetErrorString(status));
        std::cout << "ERROR: " << RTPGetErrorString(status) << std::endl;
        return -1;
    }

    //初始化socket
    SocketType sockSrv = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addrSrv;
    addrSrv.sin_addr.s_addr = inet_addr("127.0.0.1");
    addrSrv.sin_family = AF_INET;
    addrSrv.sin_port = htons(SERVER_PORT);

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

    int num = 20;
    for (int i = 1 ; i <= num ; i++)
    {
        bool mark = i % 5 ==0 ? true : false;
        session.SendPacket((void *)&pack[0], len,96, mark, mark?10:0);
        Log(DEBUG,"Sending packet %d/%d",i,num);
        RTPTime::Wait(RTPTime(0,200*1000));
    }
    session.BYEDestroy(RTPTime(10,0),0,0);

    RTPCLOSE(sockSrv);
}

class MyTCPTransmitter : public RTPTCPTransmitter
{
public:
    MyTCPTransmitter(const string &name) : RTPTCPTransmitter(0), m_name(name) { }

    void OnSendError(SocketType sock)
    {
        Log(ERROR1, "%s Error sending over socket", m_name.c_str());
        DeleteDestination(RTPTCPAddress(sock));
    }

    void OnReceiveError(SocketType sock)
    {
        Log(ERROR1, "%s Error receiving from socket", m_name.c_str());
        DeleteDestination(RTPTCPAddress(sock));
    }
private:
    string m_name;
};

vector<SocketType> m_sockets;

int tcpRecvServer(){
    // Create a listener socket and listen on it
    SocketType listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener == RTPSOCKERR)
    {
        Log(ERROR1, "Can't create listener socket");
        return -1;
    }

    struct sockaddr_in servAddr;

    memset(&servAddr, 0, sizeof(servAddr));
    servAddr.sin_family = AF_INET;
    servAddr.sin_port = htons(SERVER_PORT);

    if (bind(listener, (struct sockaddr *)&servAddr, sizeof(servAddr)) != 0)
    {
        Log(ERROR1, "Can't bind listener socket");
        return -1;
    }

    listen(listener, 1);
    m_sockets.push_back(listener);
    vector<int8_t> listenerFlags(m_sockets.size());

    while (true){
        // 非阻塞方式监听端口
        RTPTime waitTime(0.2);
        int status = RTPSelect(&m_sockets[0], &listenerFlags[0], m_sockets.size(), waitTime);
        checkerror(status);
        if(status > 0){
            if(listenerFlags[0]){
                SocketType server = accept(listener, 0, 0);
                if (server == RTPSOCKERR)
                {
                    cerr << "Can't accept incoming connection" << endl;
                    return -1;
                }
                m_sockets.pop_back();
                m_sockets.push_back(server);
                RTPCLOSE(listener);
                break;
            }
        }
        Log(DEBUG, "rtpselect before accept!");
    }

    cout << "Got connected socket pair" << endl;

    const int packSize = 1500;
    RTPSessionParams sessParams;
    MyTCPTransmitter trans("tcpRecvServer");
    RTPSession sess;

    sessParams.SetProbationType(RTPSources::NoProbation);
    sessParams.SetOwnTimestampUnit(1.0/packSize);
    sessParams.SetMaximumPacketSize(packSize + 64); // some extra room for rtp header

    bool threadsafe = false;
    checkerror(trans.Init(threadsafe));
    checkerror(trans.Create(65535, 0));
    checkerror(sess.Create(sessParams, &trans));
    checkerror(sess.AddDestination(RTPTCPAddress(m_sockets[0])));
    vector<uint8_t> pack(packSize);
    vector<int8_t> flags(m_sockets.size());

    while(true)
    {
        // 判断tcp 链接是否断开
        struct tcp_info info;
        int len = sizeof(struct tcp_info);
        getsockopt(m_sockets[0], IPPROTO_TCP, TCP_INFO, &info, (socklen_t *)&len);
        if ((info.tcpi_state == TCP_ESTABLISHED)) {
            Log(DEBUG,"tcp socket connected\n");
            // Select
            RTPTime waitTime(1);
            int status = RTPSelect(&m_sockets[0], &flags[0], m_sockets.size(), waitTime);
            checkerror(status);
            if(status > 0){

                checkerror(sess.Poll());
                sess.BeginDataAccess();
                if (sess.GotoFirstSourceWithData())
                {
                    do
                    {
                        RTPPacket *pack;
                        while ((pack = sess.GetNextPacket()) != NULL)
                        {
                            // You can examine the data here
                            Log(DEBUG,"Got packet-> seqNum:%d pt:%d ssrc:%u mark:%d payloadLength:%llu timestamp:%u",
                                pack->GetSequenceNumber(),
                                pack->GetPayloadType(),
                                pack->GetSSRC(),
                                pack->HasMarker(),
                                pack->GetPayloadLength(),
                                pack->GetTimestamp());
                            // we don't longer need the packet, so
                            // we'll delete it
                            sess.DeletePacket(pack);
                        }
                    } while (sess.GotoNextSourceWithData());
                }
                sess.EndDataAccess();
            }
        } else {
            Log(DEBUG,"tcp socket disconnected\n");
            break;
        }
        Log(DEBUG, "Loop Event finish!");
    }

    sess.BYEDestroy(RTPTime(10,0),0,0);
    return 0;
}

int udpSendClient(){
    RTPSession sess;
    uint16_t portbase = 3000;
    uint16_t destport = SERVER_PORT;
    uint32_t destip;

    int status;
    destip = inet_addr("127.0.0.1");
    if (destip == INADDR_NONE)
    {
        std::cerr << "Bad IP address specified" << std::endl;
        return -1;
    }

    destip = ntohl(destip);

    RTPUDPv4TransmissionParams transparams;
    RTPSessionParams sessparams;

    sessparams.SetOwnTimestampUnit(1.0/10.0);
    sessparams.SetAcceptOwnPackets(true);

    transparams.SetPortbase(portbase);

    status = sess.Create(sessparams,&transparams);
    checkerror(status);

    RTPIPv4Address addr(destip,destport);
    status = sess.AddDestination(addr);
    checkerror(status);

    int num = 20;
    for (int i = 1 ; i <= num ; i++)
    {
        // send the packet
        bool mark = i % 5 ==0 ? true : false;
        status = sess.SendPacket((void *)"1234567890",10,0,mark,mark?10:0);
        checkerror(status);
        Log(DEBUG,"Sending packet %d/%d",i,num);
        RTPTime::Wait(RTPTime(0,200*1000));
    }

    sess.BYEDestroy(RTPTime(10,0),0,0);

    return 0;
}

int udpRecvServer(){
    RTPSession sess;
    RTPUDPv4TransmissionParams transparams;
    RTPSessionParams sessparams;
    // IMPORTANT: The local timestamp unit MUST be set, otherwise
    //            RTCP Sender Report info will be calculated wrong
    // In this case, we'll be just use 8000 samples per second.
    sessparams.SetUsePollThread(false);
    sessparams.SetOwnTimestampUnit(1.0/8000.0);

    sessparams.SetAcceptOwnPackets(true);
    transparams.SetPortbase(SERVER_PORT);

    RTPUDPv4Transmitter transmitter(0);
    checkerror(transmitter.Init(false));
    checkerror(transmitter.Create(64000, &transparams));

    RTPUDPv4TransmissionInfo *pInfo = static_cast<RTPUDPv4TransmissionInfo *>(transmitter.GetTransmissionInfo());

    SocketType sockFd = pInfo->GetRTPSocket();
    m_sockets.push_back(sockFd);
    vector<int8_t> flags(m_sockets.size());

    int status = sess.Create(sessparams,&transmitter);
    checkerror(status);

    while(true)
    {
        // Select
        RTPTime waitTime(1);
        int status = RTPSelect(&m_sockets[0], &flags[0], m_sockets.size(), waitTime);
        checkerror(status);
        if(status > 0){
            checkerror(sess.Poll());
            sess.BeginDataAccess();
            if (sess.GotoFirstSourceWithData())
            {
                do
                {
                    RTPPacket *pack;
                    while ((pack = sess.GetNextPacket()) != NULL)
                    {
                        // You can examine the data here
                        Log(DEBUG,"Got packet-> seqNum:%d pt:%d ssrc:%u mark:%d payloadLength:%llu timestamp:%u",
                            pack->GetSequenceNumber(),
                            pack->GetPayloadType(),
                            pack->GetSSRC(),
                            pack->HasMarker(),
                            pack->GetPayloadLength(),
                            pack->GetTimestamp());
                        // we don't longer need the packet, so
                        // we'll delete it
                        sess.DeletePacket(pack);
                    }
                } while (sess.GotoNextSourceWithData());
            }
            sess.EndDataAccess();
        }
        Log(DEBUG, "Loop Event finish!");
    }
    sess.BYEDestroy(RTPTime(10,0),0,0);
    return 0;
}

#define TCP

int main(int argc, char *argv[]){

#ifdef TCP
    std::thread tcpServer(tcpRecvServer);
    std::thread tcpClient(tcpSendClient);
    tcpClient.join();
    tcpServer.join();
#else
    std::thread udpServer(udpRecvServer);
    //std::thread udpClient(udpSendClient);
    //udpClient.join();
    udpServer.join();
#endif
    return 0;
}