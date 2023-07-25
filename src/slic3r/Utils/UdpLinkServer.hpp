
#pragma once

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio.hpp>
#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/array.hpp>

using boost::asio::ip::udp;

#define UDP_DATA_PACKAGE_MAX_LENGTH 1024

typedef void(CALLBACK *SendDataCallback)(const boost::system::error_code &error,
                                         std::size_t                      bytes_transferred,
                                         DWORD                            dwUserData1,
                                         DWORD                            dwUserData2);
typedef void(CALLBACK *RecvDataCallback)(const boost::system::error_code &error,
                                         char *                           pData,
                                         int                              nDataLength,
                                         char *                           pPeerIp,
                                         unsigned short                   usPeerPort,
                                         DWORD                            dwUserData1,
                                         DWORD                            dwUserData2);

class UdpLinkServer
{
public:
    UdpLinkServer(unsigned short usPort, bool bBroadcast);
    virtual ~UdpLinkServer(void);

    typedef boost::function<
        void *(const boost::system::error_code &error, std::size_t bytes_transferred, DWORD dwUserData1, DWORD dwUserData2)>
        SendDataCallbackHandler;

    void SetRecvDataCallback(bool bAutoRecvData, RecvDataCallback pfunc, DWORD dwUserData1, DWORD dwUserData2);

    int Start(boost::asio::io_service &ioService);

    int Stop();

    int SendDataEx(
        udp::endpoint endpointRemote, char *pBuffer, int nBufferSize, SendDataCallback pfunc, DWORD dwUserData1, DWORD dwUserData2);

    int SendData(char *pBuffer, int nBufferSize, bool bAsync);

    udp::endpoint &GetBroadcastEndPoint();

    void handleRecvDataByManual(RecvDataCallback pfunc = NULL, DWORD dwUserData1 = 0, DWORD dwUserData2 = 0);
    void handleSendData(char *pBuffer, int nBufferSize, const boost::system::error_code &error, std::size_t bytes_transferred);
    void handleSendDataInner(SendDataCallback                 pfunc,
                             DWORD                            dwUserData1,
                             DWORD                            dwUserData2,
                             const boost::system::error_code &error,
                             std::size_t                      bytes_transferred);
    // void handleSendData(boost::shared_ptr<std::string> strMessage,const boost::system::error_code& error,std::size_t bytes_transferred);

    static void WINAPI SendDataCallbackOuter(const boost::system::error_code &error,
                                             std::size_t                      bytes_transferred,
                                             DWORD                            dwUserData1,
                                             DWORD                            dwUserData2);

protected:
    void RecvDataProcess(RecvDataCallback pfunc, DWORD dwUserData1, DWORD dwUserData2);
    void handleRecvData(const boost::system::error_code &error,
                        std::size_t                      bytes_transferred,
                        RecvDataCallback                 pfunc,
                        DWORD                            dwUserData1,
                        DWORD                            dwUserData2);

    bool IsStop();

private:
    udp::socket *                                   m_sockUdp;           
    udp::endpoint                                   m_endpointRemote;    
    udp::endpoint                                   m_endpointBroadcast; 
    boost::array<char, UDP_DATA_PACKAGE_MAX_LENGTH> m_recvBuf;           
    bool                                            m_bStop;             
    bool                                            m_bBroadcast;        
    unsigned short                                  m_usPort;            
    bool m_bAutoRecvData; 
    RecvDataCallback m_pfuncRecvDataCallback;       
    DWORD            m_dwRecvDataCallbackUserData1; 
    DWORD            m_dwRecvDataCallbackUserData2; 
};