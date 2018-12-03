// Copyright (c) 2017-2018 The Multiverse developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MULTIVERSE_DBP_SERVICE_H
#define MULTIVERSE_DBP_SERVICE_H

#include "mvbase.h"
#include "dbpserver.h"
#include "event.h"
#include "forkpeerevent.h"
#include "mvpeernet.h"
#include "walleve/walleve.h"

#include <set>
#include <utility>
#include <unordered_map>


namespace multiverse
{

using namespace network;

class IDbpService  :  public walleve::IIOModule, virtual public CDBPEventListener, 
                    virtual public CMvDBPEventListener, virtual public CFkNodeEventListener
{
public:
    IDbpService() : walleve::IIOModule("dbpservice") {}

    virtual void SetIsForkNode(bool isForkNode) = 0;
};

class CDbpService : public IDbpService
{
public:
    CDbpService();
    virtual ~CDbpService() noexcept;

    bool HandleEvent(CMvEventDbpConnect& event) override;
    bool HandleEvent(CMvEventDbpSub& event) override;
    bool HandleEvent(CMvEventDbpUnSub& event) override;
    bool HandleEvent(CMvEventDbpMethod& event) override;
    bool HandleEvent(CMvEventDbpPong& event) override;
    bool HandleEvent(CMvEventDbpBroken& event) override;
    bool HandleEvent(CMvEventDbpAdded& event) override;
    bool HandleEvent(CMvEventDbpRemoveSession& event) override;
    // client post event register fork id
    bool HandleEvent(CMvEventDbpRegisterForkID& event) override;
    // client post event update fork state
    bool HandleEvent(CMvEventDbpUpdateForkState& event) override;

    // notify add msg(block tx ...) to event handler
    bool HandleEvent(CMvEventDbpUpdateNewBlock& event) override;
    bool HandleEvent(CMvEventDbpUpdateNewTx& event) override;

    // notify block and tx from virtual peer net
    bool HandleEvent(CFkEventNodeBlockArrive& event) override;
    bool HandleEvent(CFkEventNodeTxArrive& event) override;

    void SetIsForkNode(bool isForkNode) override;

protected:
    bool WalleveHandleInitialize() override;
    void WalleveHandleDeinitialize() override;

private:
    typedef std::set<std::string> ForksType;
    typedef std::set<std::string> IdsType;
    typedef std::tuple<int, std::string> ForkStates; // (lastHeight, lastBlockHash)
    typedef std::tuple<std::string, std::string, int> ParentForkInfo;  // (parentForkHash, parentJointHash, parentJointPointHeight)
    typedef std::vector<ParentForkInfo>  ForkTopology;
    
    bool CalcForkPoints(const uint256& forkHash);
    void TrySwitchFork(const uint256& blockHash, uint256& forkHash);
    bool GetLwsBlocks(const uint256& forkHash, const uint256& startHash, int32 n, std::vector<CMvDbpBlock>& blocks);
    bool GetSnBlocks(const uint256& forkHash, const uint256& startHash, int32 n, std::vector<CMvDbpBlock>& blocks);
    bool IsEmpty(const uint256& hash);
    bool IsForkHash(const uint256& hash);
    bool IsInMyForkPath(const uint256& forkHash, int blockHeight);
    bool IsInChildNodeForkPath(const uint256& forkHash, int blockHeight);
    
    void HandleGetBlocks(CMvEventDbpMethod& event);
    void HandleGetTransaction(CMvEventDbpMethod& event);
    void HandleSendTransaction(CMvEventDbpMethod& event);
    void HandleRegisterFork(CMvEventDbpMethod& event);
    void HandleSendBlock(CMvEventDbpMethod& event);
    void HandleSendTx(CMvEventDbpMethod& event);
    void HandleSendBlockNotice(CMvEventDbpMethod& event);
    void HandleSendTxNotice(CMvEventDbpMethod& event);
    void HandleGetSNBlocks(CMvEventDbpMethod& event);
    void HandleUpdateForkState(CMvEventDbpMethod& event);
    void HandleAddedBlock(const CMvDbpBlock& block);
    void HandleAddedTx(const CMvDbpTransaction& tx);
    void HandleAddedSysCmd(const CMvDbpSysCmd& cmd);
    void HandleAddedBlockCmd(const CMvDbpBlockCmd& cmd);
    void HandleAddedTxCmd(const CMvDbpTxCmd& cmd);

    bool IsTopicExist(const std::string& topic);

    void SubTopic(const std::string& id, const std::string& session, const std::string& topic);
    void UnSubTopic(const std::string& id);
    void RemoveSession(const std::string& session);

    void PushBlock(const std::string& forkid, const CMvDbpBlock& block);
    void PushTx(const std::string& forkid, const CMvDbpTransaction& dbptx);
    void PushSysCmd(const std::string& forkid, const CMvDbpSysCmd& syscmd);
    void PushTxCmd(const std::string& forkid, const CMvDbpTxCmd& txcmd);
    void PushBlockCmd(const std::string& forkid, const CMvDbpBlockCmd& blockcmd);

    void RespondFailed(CMvEventDbpConnect& event);
    void RespondConnected(CMvEventDbpConnect& event);
    void RespondNoSub(CMvEventDbpSub& event);
    void RespondReady(CMvEventDbpSub& event);

    ///////////  super node  ////////////
    bool IsForkNode();
    bool IsMainFork(const uint256& hash);
    bool IsMyFork(const uint256& hash);
    bool IsChildNodeFork(const uint256& hash);
    bool IsBlockExist(const uint256& hash);
    void GetForkState(const uint256& forkHash, int& lastHeight, uint256& lastBlockHash);
    ForkTopology CalcForkToplogy(const uint256& forkHash);

    void UpdateThisNodeForkState(const uint256& forkHash);
    void UpdateChildNodeForks(const std::string& session, const std::string& forks);
    void UpdateChildNodeForksStates(const std::string& forkid, int currentHeight, const std::string& lastBlockHash);
    void UpdateChildNodeForksToParent();
    void UpdateChildNodeForksStatesToParent();
    void UpdateThisNodeForkToplogy();
    void UpdateChildNodesForkToplogy();

    void SendBlockToParent(const std::string& id, const CMvDbpBlock& block);
    void SendTxToParent(const std::string& id, const CMvDbpTransaction& tx);
    void SendBlockNoticeToParent(const std::string& forkid, const std::string& height, const std::string& hash);
    void SendTxNoticeToParent(const std::string& forkid, const std::string& hash);
    void GetBlocksToParent(const std::string& forkid, const std::string& hash, int32 num);

protected:
    walleve::IIOProc* pDbpServer;
    walleve::IIOProc* pDbpClient;
    walleve::IIOProc* pVirtualPeerNet;
    IService* pService;
    ICoreProtocol* pCoreProtocol;
    IWallet* pWallet;
    IMvNetChannel* pNetChannel;
    IForkManager* pForkManager;

private:
    std::map<std::string, ForksType> mapSessionChildNodeForks; // session => child node forks
    std::map<std::string, ForkStates> mapThisNodeForkStates; // fork id => fork states
    std::map<std::string, ForkStates> mapChildNodeForksStates; // fork id => fork states
    std::map<std::string, ForkTopology> mapThisNodeForkToplogy;       // fork id => fork toplogy
    std::map<std::string, ForkTopology> mapChildNodesForkToplogy;     // fork id => fork toplogy
    ForkStates tupleMainForkStates;

    std::map<std::string, std::string> mapIdSubedSession;       // id => session
    std::unordered_map<std::string, IdsType> mapTopicIds;       // topic => ids

    std::unordered_map<std::string, std::pair<uint256,uint256>> mapForkPoint; // fork point hash => (fork hash, fork point hash)

    bool fIsForkNode;
};

} // namespace multiverse

#endif //MULTIVERSE_DBP_SERVICE_H
