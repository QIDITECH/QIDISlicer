//#include "StdAfx.h"
#include "UdpLinkServer.hpp"
#include <boost/exception/all.hpp>

UdpLinkServer::UdpLinkServer(unsigned short usPort, bool bBroadcast)
{
    m_bStop                       = false;
    m_bBroadcast                  = bBroadcast;
    m_usPort                      = usPort;
    m_sockUdp                     = NULL;
    m_bAutoRecvData               = true;
    m_pfuncRecvDataCallback       = NULL;
    m_dwRecvDataCallbackUserData1 = 0;
    m_dwRecvDataCallbackUserData2 = 0;
}

UdpLinkServer::~UdpLinkServer(void)
{
    if (m_sockUdp != NULL) {
        m_sockUdp->close();
        delete m_sockUdp;
        m_sockUdp = NULL;
    }
}

void UdpLinkServer::SetRecvDataCallback(bool bAutoRecvData, RecvDataCallback pfunc, DWORD dwUserData1, DWORD dwUserData2)
{
    m_bAutoRecvData               = bAutoRecvData;
    m_pfuncRecvDataCallback       = pfunc;
    m_dwRecvDataCallbackUserData1 = dwUserData1;
    m_dwRecvDataCallbackUserData2 = dwUserData2;
}

int UdpLinkServer::Start(boost::asio::io_service &ioService)
{
    try {
        if (m_bBroadcast) {
            m_sockUdp = new udp::socket(ioService, udp::endpoint(udp::v4(), 0));
            m_sockUdp->set_option(boost::asio::socket_base::reuse_address(true));
            m_sockUdp->set_option(boost::asio::socket_base::broadcast(true));
            m_endpointBroadcast = udp::endpoint(boost::asio::ip::address_v4::broadcast(), m_usPort);
        } else {
            m_sockUdp = new udp::socket(ioService, udp::endpoint(udp::v4(), m_usPort));
            if (!m_sockUdp->is_open()) {
                return -1;
            }
            m_sockUdp->set_option(boost::asio::socket_base::reuse_address(true));
        }

    } catch (boost::exception &e) {
        return -1;
    }

    m_bStop = false;
    if (m_bAutoRecvData) {
        RecvDataProcess(m_pfuncRecvDataCallback, m_dwRecvDataCallbackUserData1, m_dwRecvDataCallbackUserData2);
    }
    return 0;
}

int UdpLinkServer::Stop()
{
    m_bStop = true;

    return 0;
}

bool UdpLinkServer::IsStop() { return m_bStop; }

udp::endpoint &UdpLinkServer::GetBroadcastEndPoint() { return m_endpointBroadcast; }

void UdpLinkServer::SendDataCallbackOuter(const boost::system::error_code &error,
                                          std::size_t                      bytes_transferred,
                                          DWORD                            dwUserData1,
                                          DWORD                            dwUserData2)
{
    int i = 0;
}

int UdpLinkServer::SendDataEx(
    udp::endpoint endpointRemote, char *pBuffer, int nBufferSize, SendDataCallback pfunc, DWORD dwUserData1, DWORD dwUserData2)
{
    m_sockUdp->async_send_to(boost::asio::buffer(pBuffer, nBufferSize), endpointRemote,
                             boost::bind(&UdpLinkServer::handleSendDataInner, this, pfunc, dwUserData1, dwUserData2,
                                         boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));

    return 0;
}

int UdpLinkServer::SendData(char *pBuffer, int nBufferSize, bool bAsync)
{
    if (!m_bBroadcast) {
        if (bAsync) {
            m_sockUdp->async_send_to(boost::asio::buffer(pBuffer, nBufferSize), m_endpointRemote,
                                     boost::bind(&UdpLinkServer::handleSendData, this, pBuffer, nBufferSize,
                                                 boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
        } else {
            m_sockUdp->send_to(boost::asio::buffer(pBuffer, nBufferSize), m_endpointRemote);
        }
    } else {
        if (bAsync) {
            m_sockUdp->async_send_to(boost::asio::buffer(pBuffer, nBufferSize), m_endpointBroadcast,
                                     boost::bind(&UdpLinkServer::handleSendData, this, pBuffer, nBufferSize,
                                                 boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
        } else {
            m_sockUdp->send_to(boost::asio::buffer(pBuffer, nBufferSize), m_endpointBroadcast);
        }
    }

    return 0;
}

void UdpLinkServer::RecvDataProcess(RecvDataCallback pfunc, DWORD dwUserData1, DWORD dwUserData2)
{
    m_sockUdp->async_receive_from(boost::asio::buffer(m_recvBuf), m_endpointRemote,
                                  boost::bind(&UdpLinkServer::handleRecvData, this, boost::asio::placeholders::error,
                                              boost::asio::placeholders::bytes_transferred, pfunc, dwUserData1, dwUserData2));
}

void UdpLinkServer::handleRecvDataByManual(RecvDataCallback pfunc, DWORD dwUserData1, DWORD dwUserData2)
{
    if (IsStop())
        return;

    RecvDataProcess(pfunc, dwUserData1, dwUserData2);
}

void UdpLinkServer::handleRecvData(
    const boost::system::error_code &error, std::size_t bytes_transferred, RecvDataCallback pfunc, DWORD dwUserData1, DWORD dwUserData2)
{
    if (IsStop())
        return;

    if (!error || error == boost::asio::error::message_size) {
        if (bytes_transferred > UDP_DATA_PACKAGE_MAX_LENGTH) {
            return;
        }

        if (pfunc != NULL) {
            pfunc(error, m_recvBuf.data(), bytes_transferred, (char *) m_endpointRemote.address().to_string().c_str(),
                  m_endpointRemote.port(), dwUserData1, dwUserData2);
        }

        RecvDataProcess(pfunc, dwUserData1, dwUserData2);
    } else {
        if (pfunc != NULL) {
            pfunc(error, NULL, bytes_transferred, NULL, 0, dwUserData1, dwUserData2);
        }
    }
}

void UdpLinkServer::handleSendData(char *pBuffer, int nBufferSize, const boost::system::error_code &error, std::size_t bytes_transferred)
{
    int n = 0;
}
void UdpLinkServer::handleSendDataInner(
    SendDataCallback pfunc, DWORD dwUserData1, DWORD dwUserData2, const boost::system::error_code &error, std::size_t bytes_transferred)
{
    //if (error != NULL) {
    printf("%s", boost::system::system_error(error).what());
    //}
    //if (pfunc != NULL) {
    pfunc(error, bytes_transferred, dwUserData1, dwUserData2);
    //}
    int n = 0;
}
