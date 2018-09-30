// Copyright (c) 2017-2018 The Multiverse developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MULTIVERSE_DBP_SERVER_H
#define MULTIVERSE_DBP_SERVER_H

#include "walleve/netio/ioproc.h"
#include "event.h"

#include <boost/bimap.hpp>
#include <boost/any.hpp>

#include "dbp.pb.h"
#include "lws.pb.h"

using namespace walleve;

namespace multiverse
{

class CDbpServer;

class CDbpHostConfig
{
public:
  CDbpHostConfig() {}
  CDbpHostConfig(const boost::asio::ip::tcp::endpoint &epHostIn, unsigned int nMaxConnectionsIn, unsigned int nSessionTimeoutIn,
                 const CIOSSLOption &optSSLIn, const std::map<std::string, std::string> &mapUserPassIn,
                 const std::vector<std::string> &vAllowMaskIn, const std::string &strIOModuleIn)
      : epHost(epHostIn),
        nMaxConnections(nMaxConnectionsIn),
        nSessionTimeout(nSessionTimeoutIn),
        optSSL(optSSLIn),
        mapUserPass(mapUserPassIn),
        vAllowMask(vAllowMaskIn),
        strIOModule(strIOModuleIn)
  {
  }

public:
  boost::asio::ip::tcp::endpoint epHost;
  unsigned int nMaxConnections;
  unsigned int nSessionTimeout;
  CIOSSLOption optSSL;
  std::map<std::string, std::string> mapUserPass;
  std::vector<std::string> vAllowMask;
  std::string strIOModule;
};

class CDbpProfile
{
public:
  CDbpProfile() : pIOModule(NULL), pSSLContext(NULL) {}

public:
  IIOModule *pIOModule;
  boost::asio::ssl::context *pSSLContext;
  std::map<std::string, std::string> mapAuthrizeUser;
  std::vector<std::string> vAllowMask;
  unsigned int nMaxConnections;
  unsigned int nSessionTimeout;
};

class CDbpClient
{
public:
  enum SendType
  {
    ADDED = 0,
    PING = 1,
    OTHER = 100
  };

public:
  CDbpClient(CDbpServer *pServerIn, CDbpProfile *pProfileIn,
             CIOClient *pClientIn, uint64 nonce);
  ~CDbpClient();

  CDbpProfile *GetProfile();
  uint64 GetNonce();

  std::string GetSession() const;
  void SetSession(const std::string &session);

  void Activate();
  void SendResponse(CMvDbpConnected &body);
  void SendResponse(CMvDbpFailed &body);
  void SendResponse(CMvDbpNoSub &body);
  void SendResponse(CMvDbpReady &body);
  void SendResponse(CMvDbpAdded &body);
  void SendResponse(CMvDbpMethodResult &body);
  void SendPong(const std::string &id);
  void SendPing(const std::string &id);
  void SendResponse(const std::string &reason, const std::string &description);
  void SendMessage(dbp::Base *pBaseMsg);
  void SendPingMessage(dbp::Base *pBaseMsg);
  void SendAddedMessage(dbp::Base *pBaseMsg);

protected:
  void StartReadHeader();
  void StartReadPayload(std::size_t nLength);

  void HandleReadHeader(std::size_t nTransferred);
  void HandleReadPayload(std::size_t nTransferred, uint32_t len);
  void HandleReadCompleted(uint32_t len);
  void HandleWritenResponse(std::size_t nTransferred, SendType type);

  void WriteMessageToSendStream(dbp::Base *pBaseMsg);
  bool IsSentComplete();

private:
  std::string session_;
  std::queue<dbp::Base> addedSendQueue;

protected:
  CDbpServer *pServer;
  CDbpProfile *pProfile;
  CIOClient *pClient;
  uint64 nNonce;

  CWalleveBufStream ssSend;
  CWalleveBufStream ssRecv;

  std::string SendSaver;
};

class CSessionProfile
{
public:
  CDbpClient *pDbpClient;
  std::string sessionId;
  uint64 timestamp;
  std::shared_ptr<boost::asio::deadline_timer> pingTimerPtr;
  std::string forkid;
};

class CDbpServer : public CIOProc, virtual public CDBPEventListener, virtual public CMvDBPEventListener
{
public:
  CDbpServer();
  virtual ~CDbpServer() noexcept;
  virtual CIOClient *CreateIOClient(CIOContainer *pContainer) override;

  void HandleClientRecv(CDbpClient *pDbpClient, const boost::any &anyObj);
  void HandleClientSent(CDbpClient *pDbpClient);
  void HandleClientError(CDbpClient *pDbpClient);

  void HandleClientConnect(CDbpClient *pDbpClient, google::protobuf::Any *any);
  void HandleClientSub(CDbpClient *pDbpClient, google::protobuf::Any *any);
  void HandleClientUnSub(CDbpClient *pDbpClient, google::protobuf::Any *any);
  void HandleClientMethod(CDbpClient *pDbpClient, google::protobuf::Any *any);
  void HandleClientPing(CDbpClient *pDbpClient, google::protobuf::Any *any);
  void HandleClientPong(CDbpClient *pDbpClient, google::protobuf::Any *any);

  void RespondError(CDbpClient *pDbpClient, const std::string &reason, const std::string &strError = "");
  void RespondFailed(CDbpClient *pDbpClient, const std::string &reason);

  void AddNewHost(const CDbpHostConfig &confHost);

protected:
  bool WalleveHandleInitialize() override;
  void WalleveHandleDeinitialize() override;
  void EnterLoop() override;
  void LeaveLoop() override;

  bool ClientAccepted(const boost::asio::ip::tcp::endpoint &epService, CIOClient *pClient) override;

  bool CreateProfile(const CDbpHostConfig &confHost);
  CDbpClient *AddNewClient(CIOClient *pClient, CDbpProfile *pDbpProfile);
  void RemoveClient(CDbpClient *pDbpClient);
  void RemoveSession(CDbpClient *pDbpClient);

  bool HandleEvent(CMvEventDbpConnected &event) override;
  bool HandleEvent(CMvEventDbpFailed &event) override;
  bool HandleEvent(CMvEventDbpNoSub &event) override;
  bool HandleEvent(CMvEventDbpReady &event) override;
  bool HandleEvent(CMvEventDbpAdded &event) override;
  bool HandleEvent(CMvEventDbpMethodResult &event) override;

  bool IsSessionTimeOut(CDbpClient *pDbpClient);
  bool GetSessionForkId(CDbpClient *pDbpClient, std::string &forkid);
  bool IsSessionReconnect(const std::string &session);
  bool IsSessionExist(const std::string &session);
  bool HaveAssociatedSessionOf(CDbpClient *pDbpClient);

  std::string GetUdata(dbp::Connect *pConnect, const std::string &keyName);
  std::string GenerateSessionId();
  void CreateSession(const std::string &session, const std::string &forkID, CDbpClient *pDbpClient);
  void UpdateSession(const std::string &session, CDbpClient *pDbpClient);

  void SendPingHandler(const boost::system::error_code &err, const CSessionProfile &sessionProfile);

protected:
  std::vector<CDbpHostConfig> vecHostConfig;
  std::map<boost::asio::ip::tcp::endpoint, CDbpProfile> mapProfile;
  std::map<uint64, CDbpClient *> mapClient; // nonce => CDbpClient

  typedef boost::bimap<std::string, CDbpClient *> SessionClientBimapType;
  typedef SessionClientBimapType::value_type position_pair;
  SessionClientBimapType sessionClientBimap;                //session id <=> CDbpClient
  std::map<std::string, CSessionProfile> sessionProfileMap; // session id => session profile
};
} //namespace multiverse
#endif //MULTIVERSE_DBP_SERVER_H