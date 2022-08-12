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
#include <string.h>
#include <stdlib.h>
#include <iostream>
#include <vector>
#include <thread>

using namespace std;
using namespace jrtplib;

inline void checkerror(int rtperr)
{
    if (rtperr < 0)
    {
        cerr << "ERROR: " << RTPGetErrorString(rtperr) << std::endl;
        exit(-1);
    }
}

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
        cout << m_name << ": Error sending over socket " << sock << ", removing destination" << endl;
        DeleteDestination(RTPTCPAddress(sock));
    }

    void OnReceiveError(SocketType sock)
    {
        cout << m_name << ": Error receiving from socket " << sock << ", removing destination" << endl;
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

    int num = 30;
    for (int i = 1 ; i <= num ; i++)
    {
        session.SendPacket((void *)&pack[0],len,0,false,10);
        printf("\nSending packet %d/%d\n",i,num);
        RTPTime::Wait(RTPTime(1,0));
    }
    session.BYEDestroy(RTPTime(10,0),0,0);

    RTPCLOSE(sockSrv);
}

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
    RTPCLOSE(listener);

    cout << "Got connected socket pair" << endl;

    const int packSize = 1500;
    RTPSessionParams sessParams;
    RTPTCPTransmitter trans2 = RTPTCPTransmitter(0);
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

    while(1)
    {
        sess2.BeginDataAccess();
        if (sess2.GotoFirstSourceWithData())
        {
            do
            {
                RTPPacket *pack;

                while ((pack = sess2.GetNextPacket()) != NULL)
                {
                    // You can examine the data here
                    printf("Got packet !\n");
                    // we don't longer need the packet, so
                    // we'll delete it
                    sess2.DeletePacket(pack);
                }
            } while (sess2.GotoNextSourceWithData());
        }
        sess2.EndDataAccess();

#ifndef RTP_SUPPORT_THREAD
        checkerror(sess2.Poll());
#endif // RTP_SUPPORT_THREAD

        RTPTime::Wait(RTPTime(1,0));
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