// Copyright (c) 2016-2017 The LoMoCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef WALLEVE_IOCLIENT_H
#define WALLEVE_IOCLIENT_H

#include "walleve/stream/stream.h"

#include <string>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/function.hpp>

namespace walleve
{

class CIOContainer;

class CIOClient
{
public:
    typedef boost::function<void(std::size_t)> CallBackFunc;
    typedef boost::function<void(const boost::system::error_code&)> CallBackConn;

    CIOClient(CIOContainer* pContainerIn);
    virtual ~CIOClient();
    const boost::asio::ip::tcp::endpoint GetRemote();
    const boost::asio::ip::tcp::endpoint GetLocal();
    void Close();
    void Release();
    void Shutdown();
    void Accept(boost::asio::ip::tcp::acceptor& acceptor, CallBackConn fnAccepted);
    void Connect(const boost::asio::ip::tcp::endpoint& epRemote, CallBackConn fnConnected);
    void Read(CWalleveBufStream& ssRecv, std::size_t nLength, CallBackFunc fnCompleted);
    void ReadUntil(CWalleveBufStream& ssRecv, const std::string& delim, CallBackFunc fnCompleted);
    void Write(CWalleveBufStream& ssSend, CallBackFunc fnCompleted);

protected:
    void HandleCompleted(CallBackFunc fnCompleted,
                         const boost::system::error_code& err, std::size_t transferred);
    void HandleConnCompleted(CallBackConn fnCompleted, const boost::system::error_code& err);
    virtual const boost::asio::ip::tcp::endpoint SocketGetRemote() = 0;
    virtual const boost::asio::ip::tcp::endpoint SocketGetLocal() = 0;
    virtual void CloseSocket() = 0;
    virtual bool IsSocketOpen() = 0;
    virtual void AsyncAccept(boost::asio::ip::tcp::acceptor& acceptor, CallBackConn fnAccepted) = 0;
    virtual void AsyncConnect(const boost::asio::ip::tcp::endpoint& epRemote, CallBackConn fnConnected) = 0;
    virtual void AsyncRead(CWalleveBufStream& ssRecv, std::size_t nLength, CallBackFunc fnCompleted) = 0;
    virtual void AsyncReadUntil(CWalleveBufStream& ssRecv, const std::string& delim, CallBackFunc fnCompleted) = 0;
    virtual void AsyncWrite(CWalleveBufStream& ssSend, CallBackFunc fnCompleted) = 0;

protected:
    CIOContainer* pContainer;
    boost::asio::ip::tcp::endpoint epRemote;
    int nRefCount;
};

class CSocketClient : public CIOClient
{
public:
    CSocketClient(CIOContainer* pContainerIn, boost::asio::io_service& ioservice);
    ~CSocketClient();

protected:
    void AsyncAccept(boost::asio::ip::tcp::acceptor& acceptor, CallBackConn fnAccepted) override;
    void AsyncConnect(const boost::asio::ip::tcp::endpoint& epRemote, CallBackConn fnConnected) override;
    void AsyncRead(CWalleveBufStream& ssRecv, std::size_t nLength, CallBackFunc fnCompleted) override;
    void AsyncReadUntil(CWalleveBufStream& ssRecv, const std::string& delim, CallBackFunc fnCompleted) override;
    void AsyncWrite(CWalleveBufStream& ssSend, CallBackFunc fnCompleted) override;
    const boost::asio::ip::tcp::endpoint SocketGetRemote() override;
    const boost::asio::ip::tcp::endpoint SocketGetLocal() override;
    void CloseSocket() override;
    bool IsSocketOpen() override;

protected:
    boost::asio::ip::tcp::socket sockClient;
};

class CSSLClient : public CIOClient
{
public:
    CSSLClient(CIOContainer* pContainerIn, boost::asio::io_service& ioservice,
               boost::asio::ssl::context& context,
               const std::string& strVerifyHost = "");
    ~CSSLClient();

    void SetVerifyPeer(bool bIfVerify);

protected:
    void AsyncAccept(boost::asio::ip::tcp::acceptor& acceptor, CallBackConn fnAccepted);
    void AsyncConnect(const boost::asio::ip::tcp::endpoint& epRemote, CallBackConn fnConnected);
    void AsyncRead(CWalleveBufStream& ssRecv, std::size_t nLength, CallBackFunc fnCompleted);
    void AsyncReadUntil(CWalleveBufStream& ssRecv, const std::string& delim, CallBackFunc fnCompleted);
    void AsyncWrite(CWalleveBufStream& ssSend, CallBackFunc fnCompleted);
    const boost::asio::ip::tcp::endpoint SocketGetRemote();
    const boost::asio::ip::tcp::endpoint SocketGetLocal();
    void CloseSocket();
    bool IsSocketOpen();
    void HandleConnected(CallBackConn fnHandshaked,
                         boost::asio::ssl::stream_base::handshake_type type,
                         const boost::system::error_code& err);
    bool VerifyCertificate(bool preverified, boost::asio::ssl::verify_context& ctx);
    /*bool VerifyCertificate(const std::string& strVerifyHost, bool fPreverified,
                           boost::asio::ssl::verify_context& ctx);*/

protected:
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket> sslClient;
    bool bIfVerifyPeer;
    std::string sVerifyHost;
};

} // namespace walleve

#endif //WALLEVE_IOCLIENT_H
