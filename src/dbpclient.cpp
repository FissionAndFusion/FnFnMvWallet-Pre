// Copyright (c) 2017-2018 The Multiverse developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "dbpclient.h"
#include "dbputils.h"
#include "walleve/netio/netio.h"
#include "uint256.h"

#include <thread>
#include <chrono>
#include <memory>
#include <algorithm>
#include <openssl/rand.h>
#include <boost/foreach.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/lexical_cast.hpp>

static std::size_t MSG_HEADER_LEN = 4;

#define DBPCLIENT_CONNECT_TIMEOUT 10 
namespace multiverse
{

CMvDbpClientSocket::CMvDbpClientSocket(IIOModule* pIOModuleIn,const uint64 nNonceIn,
                   CMvDbpClient* pDbpClientIn,CIOClient* pClientIn)
: pIOModule(pIOModuleIn),
  nNonce(nNonceIn),
  pDbpClient(pDbpClientIn),
  pClient(pClientIn),
  IsReading(false)
{
    ssRecv.Clear();
}
    
CMvDbpClientSocket::~CMvDbpClientSocket()
{
    if(pClient)
    {
        pClient->Close();
    }
}

IIOModule* CMvDbpClientSocket::GetIOModule()
{
    return pIOModule;
}
    
uint64 CMvDbpClientSocket::GetNonce()
{
    return nNonce;
}
    
CNetHost CMvDbpClientSocket::GetHost()
{
    return CNetHost(pClient->GetRemote());
}

std::string CMvDbpClientSocket::GetSession() const
{
    return strSessionId;
}

void CMvDbpClientSocket::SetSession(const std::string& session)
{
    strSessionId = session;
}

void CMvDbpClientSocket::ReadMessage()
{
    StartReadHeader();
}

void CMvDbpClientSocket::SendPong(const std::string& id)
{
    dbp::Pong pong;
    pong.set_id(id);

    google::protobuf::Any *any = new google::protobuf::Any();
    any->PackFrom(pong);

    SendMessage(dbp::Msg::PONG,any);
}

void CMvDbpClientSocket::SendPing(const std::string& id)
{
    dbp::Ping ping;
    ping.set_id(id);

    google::protobuf::Any *any = new google::protobuf::Any();
    any->PackFrom(ping);

    SendMessage(dbp::Msg::PING,any);
}

void CMvDbpClientSocket::SendBlockNotice(const std::string& fork, const std::string& height, const std::string& hash)
{
    sn::SendBlockNoticeArg arg;
    
    uint256 forkid, blockHash;
    std::vector<uint8> forkidBin, hashBin;
    walleve::CWalleveODataStream forkidSS(forkidBin);
    walleve::CWalleveODataStream hashSS(hashBin);
    
    forkid.SetHex(fork);
    blockHash.SetHex(hash);
    forkid.ToDataStream(forkidSS);
    blockHash.ToDataStream(hashSS);
    
    arg.set_forkid(std::string(forkidBin.begin(), forkidBin.end()));
    arg.set_hash(std::string(hashBin.begin(), hashBin.end()));
    arg.set_height(height);

    google::protobuf::Any *argAny = new google::protobuf::Any();
    argAny->PackFrom(arg);
    
    dbp::Method method;
    method.set_method("sendblocknotice");
    std::string id(CDbpUtils::RandomString());
    method.set_id(id);
    method.set_allocated_params(argAny);

    google::protobuf::Any *any = new google::protobuf::Any();
    any->PackFrom(method);

    SendMessage(dbp::Msg::METHOD,any);
}

void CMvDbpClientSocket::SendTxNotice(const std::string& fork, const std::string& hash)
{
    sn::SendTxNoticeArg arg;
    
    uint256 forkid, txHash;
    std::vector<uint8> forkidBin, hashBin;
    walleve::CWalleveODataStream forkidSS(forkidBin);
    walleve::CWalleveODataStream hashSS(hashBin);
    
    forkid.SetHex(fork);
    txHash.SetHex(hash);
    forkid.ToDataStream(forkidSS);
    txHash.ToDataStream(hashSS);
    
    arg.set_forkid(std::string(forkidBin.begin(), forkidBin.end()));
    arg.set_hash(std::string(hashBin.begin(), hashBin.end()));

    google::protobuf::Any *argAny = new google::protobuf::Any();
    argAny->PackFrom(arg);
    
    dbp::Method method;
    method.set_method("sendtxnotice");
    std::string id(CDbpUtils::RandomString());
    method.set_id(id);
    method.set_allocated_params(argAny);

    google::protobuf::Any *any = new google::protobuf::Any();
    any->PackFrom(method);

    SendMessage(dbp::Msg::METHOD,any);
}

void CMvDbpClientSocket::SendBlock(const std::string& id, const CMvDbpBlock& block)
{
    sn::SendBlockArg arg;
    arg.set_id(id);
    sn::Block *pBlock = new sn::Block();
    CDbpUtils::DbpToSnBlock(&block, (*pBlock));
    arg.set_allocated_block(pBlock);

    google::protobuf::Any *argAny = new google::protobuf::Any();
    argAny->PackFrom(arg);
    
    dbp::Method method;
    method.set_method("sendblock");
    method.set_id(CDbpUtils::RandomString());
    method.set_allocated_params(argAny);

    google::protobuf::Any *any = new google::protobuf::Any();
    any->PackFrom(method);

    SendMessage(dbp::Msg::METHOD,any);
}

void CMvDbpClientSocket::SendTx(const std::string& id, const CMvDbpTransaction& tx)
{
    sn::SendTxArg arg;
    arg.set_id(id);
    sn::Transaction *pTx = new sn::Transaction();
    CDbpUtils::DbpToSnTransaction(&tx, pTx);
    arg.set_allocated_tx(pTx);

    google::protobuf::Any *argAny = new google::protobuf::Any();
    argAny->PackFrom(arg);
    
    dbp::Method method;
    method.set_method("sendtx");
    method.set_id(CDbpUtils::RandomString());
    method.set_allocated_params(argAny);

    google::protobuf::Any *any = new google::protobuf::Any();
    any->PackFrom(method);

    SendMessage(dbp::Msg::METHOD,any);
}

void CMvDbpClientSocket::GetBlocks(const std::string& fork, const std::string& startHash, int32 num)
{
    sn::GetBlocksArg arg;
    uint256 forkid, blockHash;
    std::vector<uint8> forkidBin, hashBin;
    walleve::CWalleveODataStream forkidSS(forkidBin);
    walleve::CWalleveODataStream hashSS(hashBin);
    
    forkid.SetHex(fork);
    blockHash.SetHex(startHash);
    forkid.ToDataStream(forkidSS);
    blockHash.ToDataStream(hashSS);

    arg.set_forkid(std::string(forkidBin.begin(), forkidBin.end()));
    arg.set_hash(std::string(hashBin.begin(), hashBin.end()));
    arg.set_number(num);

    google::protobuf::Any *argAny = new google::protobuf::Any();
    argAny->PackFrom(arg);
    
    dbp::Method method;
    method.set_method("getblocks");
    method.set_id(CDbpUtils::RandomString());
    method.set_allocated_params(argAny);

    google::protobuf::Any *any = new google::protobuf::Any();
    any->PackFrom(method);

    SendMessage(dbp::Msg::METHOD,any);
}

void CMvDbpClientSocket::SendForkStateUpdate(const std::string& fork, const std::string& currentHeight, 
    const std::string& lastBlockHash)
{
    sn::UpdateForkStateArg arg;
    uint256 forkid, blockHash;
    std::vector<uint8> forkidBin, hashBin;
    walleve::CWalleveODataStream forkidSS(forkidBin);
    walleve::CWalleveODataStream hashSS(hashBin);

    forkid.SetHex(fork);
    blockHash.SetHex(lastBlockHash);
    forkid.ToDataStream(forkidSS);
    blockHash.ToDataStream(hashSS);

    arg.set_forkid(std::string(forkidBin.begin(), forkidBin.end()));
    arg.set_lastblockhash(std::string(hashBin.begin(), hashBin.end()));
    arg.set_currentheight(currentHeight);

    google::protobuf::Any *argAny = new google::protobuf::Any();
    argAny->PackFrom(arg);
    
    dbp::Method method;
    method.set_method("updateforkstate");
    method.set_id(CDbpUtils::RandomString());
    method.set_allocated_params(argAny);

    google::protobuf::Any *any = new google::protobuf::Any();
    any->PackFrom(method);

    SendMessage(dbp::Msg::METHOD,any);

}

void CMvDbpClientSocket::SendForkId(const std::string& fork)
{
    sn::RegisterForkIDArg arg;
    
    uint256 forkid;
    forkid.SetHex(fork);
    std::vector<uint8> forkidBin;
    walleve::CWalleveODataStream forkidSS(forkidBin);
    forkid.ToDataStream(forkidSS);
    arg.set_forkid(std::string(forkidBin.begin(), forkidBin.end()));

    google::protobuf::Any *argAny = new google::protobuf::Any();
    argAny->PackFrom(arg);
    
    dbp::Method method;
    method.set_method("registerforkid");
    std::string id(CDbpUtils::RandomString());  
    method.set_id(id);
    method.set_allocated_params(argAny);

    google::protobuf::Any *any = new google::protobuf::Any();
    any->PackFrom(method);

    SendMessage(dbp::Msg::METHOD,any);

}

void CMvDbpClientSocket::SendSubscribeTopic(const std::string& topic)
{
    dbp::Sub sub;
    sub.set_name(topic);
    std::string id(CDbpUtils::RandomString());
    sub.set_id(id);
    google::protobuf::Any *any = new google::protobuf::Any();
    any->PackFrom(sub);

    SendMessage(dbp::Msg::SUB,any);
}

void CMvDbpClientSocket::SendForkIds(const std::vector<std::string>& forks)
{
    for(const auto& fork : forks)
    {
        SendForkId(fork);
    }
}

void CMvDbpClientSocket::SendSubScribeTopics(const std::vector<std::string>& topics)
{
    for(const auto& topic : topics)
    {
        SendSubscribeTopic(topic);
    }
}

void CMvDbpClientSocket::SendConnectSession(const std::string& session, const std::vector<std::string>& forks)
{
    dbp::Connect connect;
    connect.set_session(session);
    connect.set_client("supernode");
    connect.set_version(1);

    google::protobuf::Any anyFork;
    sn::ForkID forkidMsg;

    for(const auto& fork : forks)
    {
        forkidMsg.add_ids(fork);
    }
    
    anyFork.PackFrom(forkidMsg);
    (*connect.mutable_udata())["supernode-forks"] = anyFork;

    google::protobuf::Any *any = new google::protobuf::Any();
    any->PackFrom(connect);

    SendMessage(dbp::Msg::CONNECT,any);
}

bool CMvDbpClientSocket::IsSentComplete()
{
    return (ssSend.GetSize() == 0 && queueMessage.empty());
}

void CMvDbpClientSocket::SendMessage(dbp::Msg type, google::protobuf::Any* any)
{
    dbp::Base base;
    base.set_msg(type);
    base.set_allocated_object(any);

    std::string bytes;
    base.SerializeToString(&bytes);

    uint32_t len;
    char len_buffer[4];
    len = bytes.size();
    len = htonl(len);
    std::memcpy(&len_buffer[0], &len, 4);
    bytes.insert(0, len_buffer, 4);

    if(!IsSentComplete())
    {
        queueMessage.push(std::make_pair(type,bytes));
        return;
    }
    
    ssSend.Write((char*)bytes.data(),bytes.size());
    pClient->Write(ssSend,boost::bind(&CMvDbpClientSocket::HandleWritenRequest,this,_1, type));
    
}

void CMvDbpClientSocket::StartReadHeader()
{
    IsReading = true;
    pClient->Read(ssRecv, MSG_HEADER_LEN,
                  boost::bind(&CMvDbpClientSocket::HandleReadHeader, this, _1));
}

void CMvDbpClientSocket::StartReadPayload(std::size_t nLength)
{
    pClient->Read(ssRecv, nLength,
                  boost::bind(&CMvDbpClientSocket::HandleReadPayload, this, _1, nLength));
}

void CMvDbpClientSocket::HandleWritenRequest(std::size_t nTransferred, dbp::Msg type)
{ 
    if(nTransferred != 0)
    {
        if(ssSend.GetSize() != 0)
        {
            pClient->Write(ssSend,boost::bind(&CMvDbpClientSocket::HandleWritenRequest,this,_1, type));
            return;
        }

        if(ssSend.GetSize() == 0 && !queueMessage.empty())
        {
            auto messagePair = queueMessage.front();
            dbp::Msg msgType = messagePair.first;
            std::string bytes = messagePair.second;
            queueMessage.pop();
            ssSend.Write((char*)bytes.data(),bytes.size());
            pClient->Write(ssSend,boost::bind(&CMvDbpClientSocket::HandleWritenRequest,this,_1, msgType));
            return;
        }

        if(!IsReading)
        {
            pDbpClient->HandleClientSocketSent(this);
        }
    
    }
    else
    {
        pDbpClient->HandleClientSocketError(this);
    }
}
    
void CMvDbpClientSocket::HandleReadHeader(std::size_t nTransferred)
{   
    if (nTransferred == MSG_HEADER_LEN)
    {
        std::string lenBuffer(ssRecv.GetData(), ssRecv.GetData() + MSG_HEADER_LEN);
        uint32_t nMsgHeaderLen = CDbpUtils::ParseLenFromMsgHeader(&lenBuffer[0], MSG_HEADER_LEN);
        if (nMsgHeaderLen == 0)
        {
            std::cerr << "Msg Base header length is 0 [dbpclient]" << std::endl;
            pDbpClient->HandleClientSocketError(this);
            return;
        }

        StartReadPayload(nMsgHeaderLen);
    }
    else
    {
        std::cerr << "Msg Base header length is not 4 [dbpclient]" << std::endl;
        pDbpClient->HandleClientSocketError(this);
    }
}
    
void CMvDbpClientSocket::HandleReadPayload(std::size_t nTransferred,uint32_t len)
{
    if (nTransferred == len)
    {
        HandleReadCompleted(len);
    }
    else
    {
        std::cerr << "pay load is not len. [dbpclient]" << std::endl;
        pDbpClient->HandleClientSocketError(this);
    }
}

void CMvDbpClientSocket::HandleReadCompleted(uint32_t len)
{ 
    char head[4];
    ssRecv.Read(head,4);
    std::string payloadBuffer(len, 0);
    ssRecv.Read(&payloadBuffer[0], len);
    IsReading = false;

    dbp::Base msgBase;
    if (!msgBase.ParseFromString(payloadBuffer))
    {
        std::cerr << "parse payload failed. [dbpclient]" << std::endl;
        pDbpClient->HandleClientSocketError(this);
        return;
    }

    dbp::Msg currentMsgType = msgBase.msg();
    google::protobuf::Any anyObj = msgBase.object();
    switch(currentMsgType)
    {
    case dbp::CONNECTED:
        pDbpClient->HandleClientSocketRecv(this,anyObj);
        break;
    case dbp::FAILED:
        pDbpClient->HandleClientSocketRecv(this,anyObj);
        break;
    case dbp::PING:
        pDbpClient->HandleClientSocketRecv(this,anyObj);
        break;
    case dbp::PONG:
        pDbpClient->HandleClientSocketRecv(this,anyObj);
        pDbpClient->HandleClientSocketSent(this);
        break;
    case dbp::RESULT:
        pDbpClient->HandleClientSocketRecv(this,anyObj);
        pDbpClient->HandleClientSocketSent(this);
        break;
    case dbp::NOSUB:
        pDbpClient->HandleClientSocketRecv(this,anyObj);
        break;
    case dbp::READY:
        pDbpClient->HandleClientSocketRecv(this,anyObj);
        pDbpClient->HandleClientSocketSent(this);
        break;
    case dbp::ADDED:
        pDbpClient->HandleClientSocketRecv(this,anyObj);
        pDbpClient->HandleClientSocketSent(this);
        break;
    default:
        std::cerr << "is not Message Base Type is unknown. [dbpclient]" << std::endl;
        pDbpClient->HandleClientSocketError(this);
        break;
    }
}

CMvDbpClient::CMvDbpClient()
  : walleve::CIOProc("dbpclient")
{
    pDbpService = NULL;
    fIsForkNode = false;
}

CMvDbpClient::~CMvDbpClient(){}


void CMvDbpClient::HandleClientSocketError(CMvDbpClientSocket* pClientSocket)
{
    std::cerr << "Client Socket Error." << std::endl; 
    
    CMvEventDbpBroken *pEventDbpBroken = new CMvEventDbpBroken(pClientSocket->GetSession());
    if(pEventDbpBroken)
    {
        pClientSocket->GetIOModule()->PostEvent(pEventDbpBroken);
    }

    auto epRemote = pClientSocket->GetHost().ToEndPoint();
    RemoveClientSocket(pClientSocket);

    auto it = mapProfile.find(epRemote);
    if(it != mapProfile.end())
    {
        StartConnection(epRemote,DBPCLIENT_CONNECT_TIMEOUT, 
            it->second.optSSL.fEnable,it->second.optSSL);
    }
    else
    {
        std::cerr << "cannot find reconnect parent node " << 
            epRemote.address().to_string() << " failed, " 
            << "port " << epRemote.port() << std::endl;
    }
}

void CMvDbpClient::HandleClientSocketSent(CMvDbpClientSocket* pClientSocket)
{
    pClientSocket->ReadMessage();
}

void CMvDbpClient::HandleClientSocketRecv(CMvDbpClientSocket* pClientSocket, const boost::any& anyObj)
{
    if(anyObj.type() != typeid(google::protobuf::Any))
    {
        return;
    }

    google::protobuf::Any any = boost::any_cast<google::protobuf::Any>(anyObj);

    if(any.Is<dbp::Connected>())
    {
        HandleConnected(pClientSocket,&any);
    }
    else if(any.Is<dbp::Failed>())
    {
        HandleFailed(pClientSocket,&any);
    }
    else if(any.Is<dbp::Ping>())
    {
        if(!HaveAssociatedSessionOf(pClientSocket))
        {
            return;
        }
        
        HandlePing(pClientSocket,&any);
    }
    else if(any.Is<dbp::Pong>())
    {
        if(!HaveAssociatedSessionOf(pClientSocket))
        {
            return;
        }

        HandlePong(pClientSocket,&any);
    }
    else if(any.Is<dbp::Result>())
    {
        if(!HaveAssociatedSessionOf(pClientSocket))
        {
            return;
        }

        HandleResult(pClientSocket,&any);
    }
    else if(any.Is<dbp::Added>())
    {
        if(!HaveAssociatedSessionOf(pClientSocket))
        {
            return;
        }

        HandleAdded(pClientSocket,&any);
    }
    else if(any.Is<dbp::Ready>())
    {
        if(!HaveAssociatedSessionOf(pClientSocket))
        {
            return;
        }

        HandleReady(pClientSocket,&any);
    }
    else if(any.Is<dbp::Nosub>())
    {
        if(!HaveAssociatedSessionOf(pClientSocket))
        {
            return;
        }

        HandleNoSub(pClientSocket,&any);
    }
    else
    {

    }

}

void CMvDbpClient::AddNewClient(const CDbpClientConfig& confClient)
{
    vecClientConfig.push_back(confClient);
}

void CMvDbpClient::HandleConnected(CMvDbpClientSocket* pClientSocket, google::protobuf::Any* any)
{
    dbp::Connected connected;
    any->UnpackTo(&connected);
    CreateSession(connected.session(),pClientSocket);
    
    if(IsSessionExist(connected.session()))
    {
        StartPingTimer(connected.session());
        RegisterDefaultForks(pClientSocket);
        SubscribeDefaultTopics(pClientSocket);
        UpdateDefaultForksState(pClientSocket);
    }
}

void CMvDbpClient::HandleFailed(CMvDbpClientSocket* pClientSocket, google::protobuf::Any* any)
{   
    dbp::Failed failed;
    any->UnpackTo(&failed);
    
    if(failed.reason() == "002")
    {
        std::cerr << "[<] session timeout: " << failed.reason() << std::endl;    
    }
    
   
    auto epRemote = pClientSocket->GetHost().ToEndPoint();
    RemoveClientSocket(pClientSocket);
    
    auto it = mapProfile.find(epRemote);
    if(it != mapProfile.end())
    {
        StartConnection(epRemote,DBPCLIENT_CONNECT_TIMEOUT, 
            it->second.optSSL.fEnable,it->second.optSSL);
    }
    else
    {
        std::cerr << "cannot find reconnect parent node " << 
            epRemote.address().to_string() << " failed, " 
            << "port " << epRemote.port() << std::endl;
    }
}
    
void CMvDbpClient::HandlePing(CMvDbpClientSocket* pClientSocket, google::protobuf::Any* any)
{
    dbp::Ping ping;
    any->UnpackTo(&ping);
    pClientSocket->SendPong(ping.id());
}
    
void CMvDbpClient::HandlePong(CMvDbpClientSocket* pClientSocket, google::protobuf::Any* any)
{
    dbp::Pong pong;
    any->UnpackTo(&pong);

    std::string session = bimapSessionClientSocket.right.at(pClientSocket);
    if(IsSessionExist(session))
    {
        mapSessionProfile[session].nTimeStamp = CDbpUtils::CurrentUTC();
    }
}

void CMvDbpClient::HandleResult(CMvDbpClientSocket* pClientSocket, google::protobuf::Any* any)
{
    dbp::Result result;
    any->UnpackTo(&result);

    if (!result.error().empty())
    {
        std::cerr << "[<]method error:" << result.error()  <<  " [dbpclient]" << std::endl;    
        return;
    }

    int size = result.result_size();
    for (int i = 0; i < size; i++)
    {
        google::protobuf::Any any = result.result(i);
        
        if(any.Is<sn::RegisterForkIDRet>())
        {
            sn::RegisterForkIDRet ret;
            any.UnpackTo(&ret);

            std::cout << "[<] register forkid method return : \n";
            uint256 forkid(std::vector<uint8>(ret.forkid().begin(), ret.forkid().end()));
            std::cout << forkid.ToString() << "\n";
        }

        if(any.Is<sn::SendBlockRet>())
        {
            sn::SendBlockRet ret;
            any.UnpackTo(&ret);

            std::cout << "[<] send block method return : \n";
            uint256 hash(std::vector<uint8>(ret.hash().begin(), ret.hash().end()));
            std::cout << hash.ToString() << "\n";
        }

        if(any.Is<sn::SendTxRet>())
        {
            sn::SendTxRet ret;
            any.UnpackTo(&ret);

            std::cout << "[<] send tx method return : \n";
            uint256 hash(std::vector<uint8>(ret.hash().begin(), ret.hash().end()));
            std::cout << hash.ToString() << "\n";
        }

        if(any.Is<sn::SendBlockNoticeRet>())
        {
            sn::SendBlockNoticeRet ret;
            any.UnpackTo(&ret);

            std::cout << "[<] send block notice method return : \n";
            uint256 hash(std::vector<uint8>(ret.hash().begin(), ret.hash().end()));
            std::cout << hash.ToString() << "\n";
        }

        if(any.Is<sn::SendTxNoticeRet>())
        {
            sn::SendTxNoticeRet ret;
            any.UnpackTo(&ret);

            std::cout << "[<] send tx notice method return : \n";
            uint256 hash(std::vector<uint8>(ret.hash().begin(), ret.hash().end()));
            std::cout << hash.ToString() << "\n";
        }

        if(any.Is<sn::GetBlocksRet>())
        {
            sn::GetBlocksRet ret;
            any.UnpackTo(&ret);

            std::cout << "[<] get blocks  method return : \n";
            uint256 hash(std::vector<uint8>(ret.hash().begin(), ret.hash().end()));
            std::cout << hash.ToString() << "\n";
        }
        
    }
}

void CMvDbpClient::HandleAddedBlock(const dbp::Added& added, CMvDbpClientSocket* pClientSocket)
{
    sn::Block block;
    added.object().UnpackTo(&block);

    CMvDbpBlock dbpBlock;
    CDbpUtils::SnToDbpBlock(&block,dbpBlock);
        
    CMvEventDbpAdded* pEventAdded =  new CMvEventDbpAdded(pClientSocket->GetSession());
    pEventAdded->data.id = added.id();
    pEventAdded->data.name = added.name();
    pEventAdded->data.anyAddedObj = dbpBlock;

    pDbpService->PostEvent(pEventAdded);
}

void CMvDbpClient::HandleAddedTx(const dbp::Added& added, CMvDbpClientSocket* pClientSocket)
{
    sn::Transaction tx;
    added.object().UnpackTo(&tx);

    CMvDbpTransaction dbpTx;
    CDbpUtils::SnToDbpTransaction(&tx,&dbpTx);

    CMvEventDbpAdded* pEventAdded =  new CMvEventDbpAdded(pClientSocket->GetSession());
    pEventAdded->data.id = added.id();
    pEventAdded->data.name = added.name();
    pEventAdded->data.anyAddedObj = dbpTx;

    pDbpService->PostEvent(pEventAdded);
}

void CMvDbpClient::HandleAddedSysCmd(const dbp::Added& added, CMvDbpClientSocket* pClientSocket)
{
    sn::SysCmd cmd;
    added.object().UnpackTo(&cmd);

    CMvDbpSysCmd dbpCmd;
    CDbpUtils::SnToDbpSysCmd(&cmd, dbpCmd);

    CMvEventDbpAdded* pEventAdded =  new CMvEventDbpAdded(pClientSocket->GetSession());
    pEventAdded->data.id = added.id();
    pEventAdded->data.name = added.name();
    pEventAdded->data.anyAddedObj = dbpCmd;

    pDbpService->PostEvent(pEventAdded);
}

void CMvDbpClient::HandleAddedBlockCmd(const dbp::Added& added, CMvDbpClientSocket* pClientSocket)
{
    sn::BlockCmd cmd;
    added.object().UnpackTo(&cmd);

    CMvDbpBlockCmd dbpCmd;
    CDbpUtils::SnToDbpBlockCmd(&cmd, dbpCmd);

    CMvEventDbpAdded* pEventAdded =  new CMvEventDbpAdded(pClientSocket->GetSession());
    pEventAdded->data.id = added.id();
    pEventAdded->data.name = added.name();
    pEventAdded->data.anyAddedObj = dbpCmd;

    pDbpService->PostEvent(pEventAdded);
}

void CMvDbpClient::HandleAddedTxCmd(const dbp::Added& added, CMvDbpClientSocket* pClientSocket)
{
    sn::TxCmd cmd;
    added.object().UnpackTo(&cmd);

    CMvDbpTxCmd dbpCmd;
    CDbpUtils::SnToDbpTxCmd(&cmd, dbpCmd);

    CMvEventDbpAdded* pEventAdded =  new CMvEventDbpAdded(pClientSocket->GetSession());
    pEventAdded->data.id = added.id();
    pEventAdded->data.name = added.name();
    pEventAdded->data.anyAddedObj = dbpCmd;

    pDbpService->PostEvent(pEventAdded);
}

void CMvDbpClient::HandleAdded(CMvDbpClientSocket* pClientSocket, google::protobuf::Any* any)
{
    dbp::Added added;
    any->UnpackTo(&added);

    if (added.name() == ALL_BLOCK_TOPIC)
    {
        HandleAddedBlock(added, pClientSocket);
    }

    if (added.name() == ALL_TX_TOPIC)
    {
        HandleAddedTx(added, pClientSocket);
    }

    if (added.name() == SYS_CMD_TOPIC)
    {
        HandleAddedSysCmd(added, pClientSocket);
    }

    if (added.name() == BLOCK_CMD_TOPIC)
    {
       HandleAddedBlockCmd(added, pClientSocket);
    }

    if (added.name() == TX_CMD_TOPIC)
    {
        HandleAddedTxCmd(added, pClientSocket);
    }
}

void CMvDbpClient::HandleReady(CMvDbpClientSocket* pClientSocket, google::protobuf::Any* any)
{
    dbp::Ready ready;
    any->UnpackTo(&ready);
}

void CMvDbpClient::HandleNoSub(CMvDbpClientSocket* pClientSocket, google::protobuf::Any* any)
{
    dbp::Nosub nosub;
    any->UnpackTo(&nosub);
}

bool CMvDbpClient::WalleveHandleInitialize()
{
    // init client config
    for(const auto & confClient : vecClientConfig)
    {
        if(!CreateProfile(confClient))
        {
            return false;
        }
    }

    if(!WalleveGetObject("dbpservice",pDbpService))
    {
        WalleveLog("request dbpservice failed in dbpclient.");
        return false;
    }

    return true;
}

void CMvDbpClient::WalleveHandleDeinitialize()
{
    pDbpService = NULL;
    // delete client config
    mapProfile.clear();
}

void CMvDbpClient::EnterLoop()
{
    // start resource
    WalleveLog("Dbp Client starting:\n");
    
    for(std::map<boost::asio::ip::tcp::endpoint, CDbpClientProfile>::iterator it = mapProfile.begin();
         it != mapProfile.end(); ++it)
    {
        bool fEnableSSL = (*it).second.optSSL.fEnable;
        if(it->first.address().is_loopback())
        {
            pDbpService->SetIsForkNode(false);
            continue;
        }
        else
        {
            pDbpService->SetIsForkNode(true);
            
            if(!StartConnection(it->first,DBPCLIENT_CONNECT_TIMEOUT,fEnableSSL,it->second.optSSL))
            {
                WalleveLog("Start to connect parent node %s failed,  port = %d\n",
                       (*it).first.address().to_string().c_str(),
                       (*it).first.port());
            }
            else
            {
                WalleveLog("Start to connect parent node %s,  port = %d\n",
                       (*it).first.address().to_string().c_str(),
                       (*it).first.port());
            } 
        } 
    }
}

void CMvDbpClient::LeaveLoop()
{
    // destory resource
    std::vector<CMvSessionProfile> vProfile;
    for(auto it = mapSessionProfile.begin(); it != mapSessionProfile.end(); ++it)
    {
        vProfile.push_back((*it).second);
    }

    for(const auto& profile : vProfile)
    {
        RemoveClientSocket(profile.pClientSocket);
    }

    WalleveLog("Dbp Client stop\n");
}

bool CMvDbpClient::ClientConnected(CIOClient* pClient)
{
    auto it = mapProfile.find(pClient->GetRemote());
    if(it == mapProfile.end())
    {
        return false;
    }

    WalleveLog("Connect parent node %s success,  port = %d\n",
                       (*it).first.address().to_string().c_str(),
                       (*it).first.port());

    return ActivateConnect(pClient);
}

void CMvDbpClient::ClientFailToConnect(const boost::asio::ip::tcp::endpoint& epRemote)
{
    WalleveLog("Connect parent node %s failed,  port = %d\n reconnectting\n",
                       epRemote.address().to_string().c_str(),
                       epRemote.port());

    std::cerr << "Connect parent node " << 
        epRemote.address().to_string() << " failed, " 
        << "port " << epRemote.port() << " and reconnectting." << std::endl;
    
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    auto it = mapProfile.find(epRemote);
    if(it != mapProfile.end())
    {
        StartConnection(epRemote,DBPCLIENT_CONNECT_TIMEOUT, 
            it->second.optSSL.fEnable,it->second.optSSL);
    }
    else
    {
        std::cerr << "cannot find reconnect parent node " << 
            epRemote.address().to_string() << " failed, " 
            << "port " << epRemote.port() << std::endl;
    }
}
    
void CMvDbpClient::Timeout(uint64 nNonce,uint32 nTimerId)
{
    std::cerr << "time out" << std::endl;

    WalleveLog("Connect parent node %s timeout,  nonce = %d  timerid = %d\n",
                       nNonce,
                       nTimerId);
}

bool CMvDbpClient::CreateProfile(const CDbpClientConfig& confClient)
{
    CDbpClientProfile profile;
    if(!WalleveGetObject(confClient.strIOModule,profile.pIOModule))
    {
        WalleveLog("Failed to request %s\n", confClient.strIOModule.c_str());
        return false;
    }

    if(confClient.optSSL.fEnable)
        profile.optSSL = confClient.optSSL;
    
    profile.vSupportForks = confClient.vSupportForks;
    profile.strPrivateKey = confClient.strPrivateKey;
    profile.epParentHost = confClient.epParentHost;
    mapProfile[confClient.epParentHost] = profile;

    return true;
}

bool CMvDbpClient::StartConnection(const boost::asio::ip::tcp::endpoint& epRemote, int64 nTimeout,bool fEnableSSL,
    const CIOSSLOption& optSSL)
{
    if(fEnableSSL)
    {
        return Connect(epRemote,nTimeout) ? true : false;
    }
    else
    {
        return SSLConnect(epRemote,nTimeout,optSSL) ? true : false;
    }
}

void CMvDbpClient::SendPingHandler(const boost::system::error_code& err, const CMvSessionProfile& sessionProfile)
{
    if (err != boost::system::errc::success)
    {
        return;
    }

    if(!HaveAssociatedSessionOf(sessionProfile.pClientSocket))
    {
        return;
    }

    std::string utc = std::to_string(CDbpUtils::CurrentUTC());
    sessionProfile.pClientSocket->SendPing(utc);
    
    sessionProfile.ptrPingTimer->expires_at(sessionProfile.ptrPingTimer->expires_at() + boost::posix_time::seconds(3));
    sessionProfile.ptrPingTimer->async_wait(boost::bind(&CMvDbpClient::SendPingHandler,
                                                        this, boost::asio::placeholders::error,
                                                        boost::ref(sessionProfile)));
}
 
void CMvDbpClient::StartPingTimer(const std::string& session)
{
    auto& profile = mapSessionProfile[session];
    
    profile.ptrPingTimer = 
        std::make_shared<boost::asio::deadline_timer>(this->GetIoService(),
                                                      boost::posix_time::seconds(3));
    profile.ptrPingTimer->expires_at(profile.ptrPingTimer->expires_at() +
                                        boost::posix_time::seconds(3));
    profile.ptrPingTimer->async_wait(boost::bind(&CMvDbpClient::SendPingHandler,
                                                    this, boost::asio::placeholders::error,
                                                    boost::ref(profile)));
}

void CMvDbpClient::RegisterDefaultForks(CMvDbpClientSocket* pClientSocket)
{
    std::vector<std::string> vSupportForks = mapProfile[pClientSocket->GetHost().ToEndPoint()].vSupportForks;  
    for(const auto& fork : vSupportForks)
    {
        CMvEventDbpRegisterForkID *pEvent = new CMvEventDbpRegisterForkID("");
        pEvent->data.forkid = fork;
        pDbpService->PostEvent(pEvent);
    }

    CMvEventDbpRegisterForkID *pEventEmpty = new CMvEventDbpRegisterForkID("");
    pEventEmpty->data.forkid = std::string();
    pDbpService->PostEvent(pEventEmpty);
}

void CMvDbpClient::UpdateDefaultForksState(CMvDbpClientSocket* pClientSocket)
{
    std::vector<std::string> vSupportForks = mapProfile[pClientSocket->GetHost().ToEndPoint()].vSupportForks;
    for(const auto& fork : vSupportForks)
    {
        CMvEventDbpUpdateForkState *pEvent = new CMvEventDbpUpdateForkState("");
        pEvent->data.forkid = fork;
        pDbpService->PostEvent(pEvent);
    }

    CMvEventDbpUpdateForkState *pEventEmpty = new CMvEventDbpUpdateForkState("");
    pEventEmpty->data.forkid = std::string();
    pDbpService->PostEvent(pEventEmpty);
}

void CMvDbpClient::SubscribeDefaultTopics(CMvDbpClientSocket* pClientSocket)
{
    std::vector<std::string> vTopics{ALL_BLOCK_TOPIC,ALL_TX_TOPIC,SYS_CMD_TOPIC,TX_CMD_TOPIC, BLOCK_CMD_TOPIC};
    pClientSocket->SendSubScribeTopics(vTopics);
}

void CMvDbpClient::CreateSession(const std::string& session, CMvDbpClientSocket* pClientSocket)
{
    CMvSessionProfile profile;
    profile.strSessionId = session;
    profile.nTimeStamp = CDbpUtils::CurrentUTC();
    profile.pClientSocket = pClientSocket;

    pClientSocket->SetSession(session);

    mapSessionProfile.insert(std::make_pair(session,profile));
    bimapSessionClientSocket.insert(position_pair(session,pClientSocket));
}

bool CMvDbpClient::HaveAssociatedSessionOf(CMvDbpClientSocket* pClientSocket)
{
    return bimapSessionClientSocket.right.find(pClientSocket) != bimapSessionClientSocket.right.end();
}

bool CMvDbpClient::IsSessionExist(const std::string& session)
{
    return mapSessionProfile.find(session) != mapSessionProfile.end();
}

bool CMvDbpClient::IsForkNode()
{
    return fIsForkNode;
}

bool CMvDbpClient::ActivateConnect(CIOClient* pClient)
{
    uint64 nNonce = 0;
    RAND_bytes((unsigned char *)&nNonce, sizeof(nNonce));
    while (mapClientSocket.count(nNonce))
    {
        RAND_bytes((unsigned char *)&nNonce, sizeof(nNonce));
    }
    
    IIOModule* pIOModule = mapProfile[pClient->GetRemote()].pIOModule;
    CMvDbpClientSocket* pDbpClientSocket = new CMvDbpClientSocket(pIOModule,nNonce,this,pClient);
    if(!pDbpClientSocket)
    {
        std::cerr << "Create Client Socket error\n";
        return false;
    }

    mapClientSocket.insert(std::make_pair(nNonce,pDbpClientSocket));
    
    std::vector<std::string> vSupportForks = mapProfile[pClient->GetRemote()].vSupportForks;
    
    pDbpClientSocket->SendConnectSession("",vSupportForks);
    
    return true;
}

void CMvDbpClient::CloseConnect(CMvDbpClientSocket* pClientSocket)
{
    delete pClientSocket;
}

void CMvDbpClient::RemoveSession(CMvDbpClientSocket* pClientSocket)
{
    if (HaveAssociatedSessionOf(pClientSocket))
    {
        std::string assciatedSession = bimapSessionClientSocket.right.at(pClientSocket);
        bimapSessionClientSocket.left.erase(assciatedSession);
        bimapSessionClientSocket.right.erase(pClientSocket);
        
        mapSessionProfile[assciatedSession].ptrPingTimer->cancel();
        mapSessionProfile.erase(assciatedSession);
    }
}

void CMvDbpClient::RemoveClientSocket(CMvDbpClientSocket* pClientSocket)
{
    RemoveSession(pClientSocket);
    CloseConnect(pClientSocket);
}

CMvDbpClientSocket* CMvDbpClient::PickOneSessionSocket() const
{
    CMvDbpClientSocket* pClientSocket = nullptr;
    if(mapSessionProfile.size() > 0)
    {
        pClientSocket = mapSessionProfile.begin()->second.pClientSocket;
    }
    else
    {
        std::cerr << "mapSessionProfile is empty\n";
    }

    return pClientSocket;
}

bool CMvDbpClient::HandleEvent(CMvEventDbpRegisterForkID& event)
{
    if(!event.strSessionId.empty() || event.data.forkid.empty() || !IsForkNode())
    {
        std::cerr << "cannot handle Register fork event." << std::endl;
        return false;
    }

    CMvDbpClientSocket* pClientSocket = PickOneSessionSocket();
    if(!pClientSocket)
    {
        std::cerr << "Client Socket is invalid\n";
        return false;
    }

    std::vector<std::string> forks{event.data.forkid};
    pClientSocket->SendForkIds(forks);

    return true;
}

bool CMvDbpClient::HandleEvent(CMvEventDbpSendBlock& event)
{
    if(!event.strSessionId.empty() || event.data.id.empty() || event.data.block.type() != typeid(CMvDbpBlock)
        || !IsForkNode())
    {
        std::cerr << "cannot handle SendBlock event." << std::endl;
        return false;
    }

    CMvDbpClientSocket* pClientSocket = PickOneSessionSocket();
    if(!pClientSocket)
    {
        std::cerr << "Client Socket is invalid\n";
        return false;
    }

    CMvDbpBlock block = boost::any_cast<CMvDbpBlock>(event.data.block);
    pClientSocket->SendBlock(event.data.id, block);
    
    return true;
}

bool CMvDbpClient::HandleEvent(CMvEventDbpSendTx& event)
{
    if(!event.strSessionId.empty() || event.data.id.empty() || event.data.tx.type() != typeid(CMvDbpTransaction)
        || !IsForkNode())
    {
        std::cerr << "cannot handle SendTx event." << std::endl;
        return false;
    }
    
    CMvDbpClientSocket* pClientSocket = PickOneSessionSocket();
    if(!pClientSocket)
    {
        std::cerr << "Client Socket is invalid\n";
        return false;
    }
    
    CMvDbpTransaction tx = boost::any_cast<CMvDbpTransaction>(event.data.tx);
    pClientSocket->SendTx(event.data.id, tx);
    
    return true;
}

bool CMvDbpClient::HandleEvent(CMvEventDbpSendBlockNotice& event)
{
    if(!event.strSessionId.empty() || event.data.forkid.empty() 
        || event.data.hash.empty() || !IsForkNode())
    {
        std::cerr << "cannot handle SendBlockNotice event." << std::endl;
        return false;
    }
    
    CMvDbpClientSocket* pClientSocket = PickOneSessionSocket();
    if(!pClientSocket)
    {
        std::cerr << "Client Socket is invalid\n";
        return false;
    }

    pClientSocket->SendBlockNotice(event.data.forkid,event.data.height, event.data.height);

    return true;
}

bool CMvDbpClient::HandleEvent(CMvEventDbpSendTxNotice& event)
{
    if(!event.strSessionId.empty() || event.data.forkid.empty() 
        || event.data.hash.empty() || !IsForkNode())
    {
        std::cerr << "cannot handle SendTxNotice event." << std::endl;
        return false;
    }
    
    CMvDbpClientSocket* pClientSocket = PickOneSessionSocket();
    if(!pClientSocket)
    {
        std::cerr << "Client Socket is invalid\n";
        return false;
    }

    pClientSocket->SendTxNotice(event.data.forkid,event.data.hash);

    return true;
}

bool CMvDbpClient::HandleEvent(CMvEventDbpIsForkNode& event)
{
    fIsForkNode = event.data.IsForkNode;
    return true;
}

bool CMvDbpClient::HandleEvent(CMvEventDbpGetBlocks& event)
{
    if(!event.strSessionId.empty() || !IsForkNode())
    {
        std::cerr << "cannot handle GetBlocks event for supernode." << std::endl;
        return false;
    }
    
    CMvDbpClientSocket* pClientSocket = PickOneSessionSocket();
    if(!pClientSocket)
    {
        std::cerr << "Client Socket is invalid\n";
        return false;
    }

    pClientSocket->GetBlocks(event.data.forkid,event.data.hash,event.data.number);

    return true;
}

bool CMvDbpClient::HandleEvent(CMvEventDbpUpdateForkState& event)
{
    if(!event.strSessionId.empty() || !IsForkNode())
    {
        std::cerr << "cannot handle UpdateForkState event for supernode." << std::endl;
        return false;
    }
    
    CMvDbpClientSocket* pClientSocket = PickOneSessionSocket();
    if(!pClientSocket)
    {
        std::cerr << "Client Socket is invalid\n";
        return false;
    }

    pClientSocket->SendForkStateUpdate(event.data.forkid, event.data.currentHeight, event.data.lastBlockHash);

    return true;
}


} // namespace multiverse