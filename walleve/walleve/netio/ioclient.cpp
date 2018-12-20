// Copyright (c) 2016-2017 The LoMoCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "ioclient.h"
#include "iocontainer.h"

#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/asio/ssl/rfc2818_verification.hpp>

#include "walleve/util.h"

using namespace std;
using namespace walleve;
using boost::asio::ip::tcp;

///////////////////////////////
// CIOClient
CIOClient::CIOClient(CIOContainer* pContainerIn)
    : pContainer(pContainerIn)
{
    nRefCount = 0;
}

CIOClient::~CIOClient()
{
}

const tcp::endpoint CIOClient::GetRemote()
{
    if (epRemote == tcp::endpoint())
    {
        try
        {
            epRemote = SocketGetRemote();
        }
        catch (exception& e)
        {
            StdError(__PRETTY_FUNCTION__, e.what());
        }
    }
    return epRemote;
}

const tcp::endpoint CIOClient::GetLocal()
{
    try
    {
        return SocketGetLocal();
    }
    catch (exception& e)
    {
        StdError(__PRETTY_FUNCTION__, e.what());
    }
    return (tcp::endpoint());
}

void CIOClient::Close()
{
    if (IsSocketOpen())
    {
        CloseSocket();
        epRemote = tcp::endpoint();
        Release();
    }
}

void CIOClient::Release()
{
    if (!(--nRefCount))
    {
        pContainer->ClientClose(this);
    }
}

void CIOClient::Shutdown()
{
    CloseSocket();
    epRemote = tcp::endpoint();
}

void CIOClient::Accept(tcp::acceptor& acceptor, CallBackConn fnAccepted)
{
    ++nRefCount;
    AsyncAccept(acceptor, fnAccepted);
}

void CIOClient::Connect(const tcp::endpoint& epRemote, CallBackConn fnConnected)
{
    ++nRefCount;
    AsyncConnect(epRemote, fnConnected);
}

void CIOClient::Read(CWalleveBufStream& ssRecv, size_t nLength, CallBackFunc fnCompleted)
{
    ++nRefCount;
    AsyncRead(ssRecv, nLength, fnCompleted);
}

void CIOClient::ReadUntil(CWalleveBufStream& ssRecv, const string& delim, CallBackFunc fnCompleted)
{
    ++nRefCount;
    AsyncReadUntil(ssRecv, delim, fnCompleted);
}

void CIOClient::Write(CWalleveBufStream& ssSend, CallBackFunc fnCompleted)
{
    ++nRefCount;
    AsyncWrite(ssSend, fnCompleted);
}

void CIOClient::HandleCompleted(CallBackFunc fnCompleted,
                                const boost::system::error_code& err, size_t transferred)
{
    if (err != boost::asio::error::operation_aborted && IsSocketOpen())
    {
        fnCompleted(!err ? transferred : 0);
    }
    Release();
}

void CIOClient::HandleConnCompleted(CallBackConn fnCompleted, const boost::system::error_code& err)
{
    fnCompleted(IsSocketOpen() ? err : boost::asio::error::operation_aborted);

    if (!IsSocketOpen() && nRefCount)
    {
        Release();
    }
}

///////////////////////////////
// CSocketClient
CSocketClient::CSocketClient(CIOContainer* pContainerIn, boost::asio::io_service& ioservice)
    : CIOClient(pContainerIn), sockClient(ioservice)
{
}

CSocketClient::~CSocketClient()
{
    CloseSocket();
}

void CSocketClient::AsyncAccept(tcp::acceptor& acceptor, CallBackConn fnAccepted)
{
    acceptor.async_accept(sockClient, boost::bind(&CSocketClient::HandleConnCompleted, this,
                                                  fnAccepted, boost::asio::placeholders::error));
}

void CSocketClient::AsyncConnect(const tcp::endpoint& epRemote, CallBackConn fnConnected)
{
    sockClient.async_connect(epRemote, boost::bind(&CSocketClient::HandleConnCompleted, this,
                                                   fnConnected, boost::asio::placeholders::error));
}

void CSocketClient::AsyncRead(CWalleveBufStream& ssRecv, size_t nLength, CallBackFunc fnCompleted)
{
    boost::asio::async_read(sockClient,
                            (boost::asio::streambuf &)ssRecv,
                            boost::asio::transfer_exactly(nLength),
                            boost::bind(&CSocketClient::HandleCompleted, this, fnCompleted,
                                        boost::asio::placeholders::error,
                                        boost::asio::placeholders::bytes_transferred));
}

void CSocketClient::AsyncReadUntil(CWalleveBufStream& ssRecv, const string& delim, CallBackFunc fnCompleted)
{
    boost::asio::async_read_until(sockClient,
                                  (boost::asio::streambuf&)ssRecv,
                                  delim,
                                  boost::bind(&CSocketClient::HandleCompleted, this, fnCompleted,
                                              boost::asio::placeholders::error,
                                              boost::asio::placeholders::bytes_transferred));
}

void CSocketClient::AsyncWrite(CWalleveBufStream& ssSend, CallBackFunc fnCompleted)
{
    boost::asio::async_write(sockClient,
                             (boost::asio::streambuf&)ssSend,
                             boost::asio::transfer_all(),
                             boost::bind(&CSocketClient::HandleCompleted, this, fnCompleted,
                                         boost::asio::placeholders::error,
                                         boost::asio::placeholders::bytes_transferred));
}

const tcp::endpoint CSocketClient::SocketGetRemote()
{
    return sockClient.remote_endpoint();
}

const tcp::endpoint CSocketClient::SocketGetLocal()
{
    return sockClient.local_endpoint();
}

void CSocketClient::CloseSocket()
{
    sockClient.close();
}

bool CSocketClient::IsSocketOpen()
{
    return sockClient.is_open();
}

///////////////////////////////
// CSSLClient
CSSLClient::CSSLClient(CIOContainer* pContainerIn, boost::asio::io_service& ioserivce,
                       boost::asio::ssl::context& context,
                       const string& strVerifyHost)
    : CIOClient(pContainerIn), sslClient(ioserivce, context), sVerifyHost(strVerifyHost)
{
    //if (!strVerifyHost.empty())
    //{
        //        sslClient.set_verify_callback(boost::bind(&CSSLClient::VerifyCertificate,this,
        //                                                   strVerifyHost,_1,_2));
        //sslClient.set_verify_callback(boost::asio::ssl::rfc2818_verification(strVerifyHost));
    //}
}

CSSLClient::~CSSLClient()
{
    CloseSocket();
}

void CSSLClient::SetVerifyPeer(bool bIfVerify)
{
    bIfVerifyPeer = bIfVerify;
}

void CSSLClient::AsyncAccept(tcp::acceptor& acceptor, CallBackConn fnAccepted)
{
    acceptor.async_accept(sslClient.lowest_layer(),
                          boost::bind(&CSSLClient::HandleConnected, this, fnAccepted,
                                      boost::asio::ssl::stream_base::server,
                                      boost::asio::placeholders::error));
}

void CSSLClient::AsyncConnect(const tcp::endpoint& epRemote, CallBackConn fnConnected)
{
    sslClient.lowest_layer().async_connect(epRemote,
                                           boost::bind(&CSSLClient::HandleConnected, this, fnConnected,
                                                       boost::asio::ssl::stream_base::client,
                                                       boost::asio::placeholders::error));
}

void CSSLClient::AsyncRead(CWalleveBufStream& ssRecv, size_t nLength, CallBackFunc fnCompleted)
{
    boost::asio::async_read(sslClient,
                            (boost::asio::streambuf&)ssRecv,
                            boost::asio::transfer_exactly(nLength),
                            boost::bind(&CSSLClient::HandleCompleted, this, fnCompleted,
                                        boost::asio::placeholders::error,
                                        boost::asio::placeholders::bytes_transferred));
}

void CSSLClient::AsyncReadUntil(CWalleveBufStream& ssRecv, const string& delim, CallBackFunc fnCompleted)
{
    boost::asio::async_read_until(sslClient,
                                  (boost::asio::streambuf&)ssRecv,
                                  delim,
                                  boost::bind(&CSSLClient::HandleCompleted, this, fnCompleted,
                                              boost::asio::placeholders::error,
                                              boost::asio::placeholders::bytes_transferred));
}

void CSSLClient::AsyncWrite(CWalleveBufStream& ssSend, CallBackFunc fnCompleted)
{
    boost::asio::async_write(sslClient,
                             (boost::asio::streambuf&)ssSend,
                             boost::bind(&CSSLClient::HandleCompleted, this, fnCompleted,
                                         boost::asio::placeholders::error,
                                         boost::asio::placeholders::bytes_transferred));
}

const tcp::endpoint CSSLClient::SocketGetRemote()
{
    return sslClient.lowest_layer().remote_endpoint();
}

const tcp::endpoint CSSLClient::SocketGetLocal()
{
    return sslClient.lowest_layer().local_endpoint();
}

void CSSLClient::CloseSocket()
{
    sslClient.lowest_layer().close();
}

bool CSSLClient::IsSocketOpen()
{
    return sslClient.lowest_layer().is_open();
}


void CSSLClient::HandleConnected(CallBackConn fnHandshaked,
                                 boost::asio::ssl::stream_base::handshake_type type,
                                 const boost::system::error_code& err)
{
    if (!err)
    {
        if (bIfVerifyPeer)
        {
            sslClient.set_verify_mode(boost::asio::ssl::verify_peer);
            sslClient.set_verify_callback(boost::bind(&CSSLClient::VerifyCertificate, this, _1, _2));
        }
        
        sslClient.async_handshake(type, boost::bind(&CSSLClient::HandleConnCompleted, this, 
            fnHandshaked, boost::asio::placeholders::error));
    }
    else
    {
        HandleConnCompleted(fnHandshaked, err);
    }
}

bool CSSLClient::VerifyCertificate(bool preverified,
      boost::asio::ssl::verify_context& ctx)
{
    /*char subject_name[256];
    X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
    X509_NAME_oneline(X509_get_subject_name(cert), subject_name, 256);
    std::cout << "Verify: " << (preverified ? "success" : "fail") << ", info: " << subject_name << "\n";*/

    if (preverified)
        //walleve::StdDebug("SSL", "Ssl verify success.");
        cout << "Ssl verify success." << endl;
    else
        //walleve::StdDebug("SSL", "Ssl verify fail.");
        cout << "Ssl verify fail." << endl;

    return preverified;
}

/*bool CSSLClient::VerifyCertificate(const string& strVerifyHost, bool fPreverified,
                                   boost::asio::ssl::verify_context& ctx)
{
    char subject_name[256];
    X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
    X509_NAME_oneline(X509_get_subject_name(cert), subject_name, 256);
    std::cout << "Verifying " << fPreverified << " " << subject_name << "\n";

    X509_STORE_CTX* cts = ctx.native_handle();

    //int32_t length = 0;
    //X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
    int cts_error;

#ifdef USE_SSL_110
    cts_error = X509_STORE_CTX_get_error(cts);
#else
    cts_error = cts->error;
#endif
    std::cout << "CTX ERROR : " << cts_error << std::endl;

    int32_t depth = X509_STORE_CTX_get_error_depth(cts);
    std::cout << "CTX DEPTH : " << depth << std::endl;

    switch (cts_error)
    {
    case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT:
        cout << "X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT\n";
        break;
    case X509_V_ERR_CERT_NOT_YET_VALID:
    case X509_V_ERR_ERROR_IN_CERT_NOT_BEFORE_FIELD:
        cout << "Certificate not yet valid!!\n";
        break;
    case X509_V_ERR_CERT_HAS_EXPIRED:
    case X509_V_ERR_ERROR_IN_CERT_NOT_AFTER_FIELD:
        cout << "Certificate expired..\n";
        break;
    case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN:
        cout << "Self signed certificate in chain!!!\n";
        //preverified = true;
        break;
    default:
        break;
    }
    return fPreverified;
}*/
