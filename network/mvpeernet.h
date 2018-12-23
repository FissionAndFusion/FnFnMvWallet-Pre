// Copyright (c) 2017-2018 The Multiverse developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef  MULTIVERSE_PEERNET_H
#define  MULTIVERSE_PEERNET_H

#include "mvproto.h"
#include "mvpeerevent.h"

#include "walleve/walleve.h"

namespace multiverse 
{
namespace network 
{

class IMvNetChannel : public walleve::IIOModule, virtual public CMvPeerEventListener
{
public:
    IMvNetChannel() : IIOModule("netchannel") {}
    virtual int GetPrimaryChainHeight() = 0;
    virtual bool IsForkSynchronized(const uint256& hashFork) const = 0;
    virtual void BroadcastBlockInv(const uint256& hashFork,const uint256& hashBlock,const std::set<uint64>& setKnownPeer=std::set<uint64>()) = 0;
    virtual void BroadcastTxInv(const uint256& hashFork) = 0;
    virtual void SubscribeFork(const uint256& hashFork) = 0;
    virtual void UnsubscribeFork(const uint256& hashFork) = 0;
    virtual bool IsContains(const uint256& hashFork) = 0;
    virtual void EnableSuperNode(bool fIsFork) = 0;
};

class IMvDelegatedChannel : public walleve::IIOModule, virtual public CMvPeerEventListener
{
public:
    IMvDelegatedChannel() : IIOModule("delegatedchannel") {}
    virtual void PrimaryUpdate(int nStartHeight,
                               const std::vector<std::pair<uint256,std::map<CDestination,size_t> > >& vEnrolledWeight,
                               const std::map<CDestination,std::vector<unsigned char> >& mapDistributeData, 
                               const std::map<CDestination,std::vector<unsigned char> >& mapPublishData) = 0;
};

class CMvPeerNet : public walleve::CPeerNet, virtual public CMvPeerEventListener
{
public:
    enum class SUPER_NODE_TYPE : int
    {
        SUPER_NODE_TYPE_UNKN,
        SUPER_NODE_TYPE_FNFN = 0,
        SUPER_NODE_TYPE_ROOT,
        SUPER_NODE_TYPE_FORK
    };

public:
    CMvPeerNet();
    CMvPeerNet(const std::string& walleveOwnKeyIn);
    ~CMvPeerNet();
    virtual void BuildHello(walleve::CPeer *pPeer,walleve::CWalleveBufStream& ssPayload);
    void HandlePeerWriten(walleve::CPeer *pPeer) override;
    virtual bool HandlePeerHandshaked(walleve::CPeer *pPeer,uint32 nTimerId);
    virtual bool HandlePeerRecvMessage(walleve::CPeer *pPeer,int nChannel,int nCommand,
                               walleve::CWalleveBufStream& ssPayload);
protected:
    bool HandleForkPeerActive(const CMvEventPeerActive& eventActive);
    bool HandleForkPeerDeactive(const CMvEventPeerDeactive& eventDeactive);
    virtual bool HandlePeerHandshakedForForkNode(const CMvEventPeerActive& peerActive);
    virtual bool DestroyPeerForForkNode(const CMvEventPeerDeactive& peerDeactive);

    virtual bool HandleRootPeerSub(const uint64& nNonce, const uint256& hashFork, std::vector<uint256>& data);
    virtual bool HandleRootPeerUnSub(const uint64& nNonce, const uint256& hashFork, std::vector<uint256>& data);
    virtual bool HandleRootPeerGetBlocks(const uint64& nNonce, const uint256& hashFork, CBlockLocator& data);
    virtual bool HandleRootPeerInv(const uint64& nNonce, const uint256& hashFork, std::vector<CInv>& data);
    virtual bool HandleRootPeerGetData(const uint64& nNonce, const uint256& hashFork, std::vector<CInv>& data);
    virtual bool HandleRootPeerBlock(const uint64& nNonce, const uint256& hashFork, CBlock& data);
    virtual bool HandleRootPeerTx(const uint64& nNonce, const uint256& hashFork, CTransaction& data);
    virtual bool IsMainFork(const uint256& hashFork);
    bool IsThisNodeData(const uint256& hashFork, uint64 nNonce, const uint256& dataHash);
protected:
    bool WalleveHandleInitialize() override;
    void WalleveHandleDeinitialize() override;
    bool HandleEvent(CMvEventPeerSubscribe& eventSubscribe) override;
    bool HandleEvent(CMvEventPeerUnsubscribe& eventUnsubscribe) override;
    bool HandleEvent(CMvEventPeerInv& eventInv) override;
    bool HandleEvent(CMvEventPeerGetData& eventGetData) override;
    bool HandleEvent(CMvEventPeerGetBlocks& eventGetBlocks) override;
    bool HandleEvent(CMvEventPeerTx& eventTx) override;
    bool HandleEvent(CMvEventPeerBlock& eventBlock) override;
    bool HandleEvent(CMvEventPeerBulletin& eventBulletin) override;
    bool HandleEvent(CMvEventPeerGetDelegated& eventGetDelegated) override;
    bool HandleEvent(CMvEventPeerDistribute& eventDistribute) override;
    bool HandleEvent(CMvEventPeerPublish& eventPublish) override;

    bool HandleEventForkNode(CMvEventPeerGetData& eventGetData);
    walleve::CPeer* CreatePeer(walleve::CIOClient *pClient,uint64 nNonce,bool fInBound) override;
    void DestroyPeer(walleve::CPeer* pPeer) override;
    walleve::CPeerInfo* GetPeerInfo(walleve::CPeer* pPeer,walleve::CPeerInfo* pInfo) override;
    bool SendDataMessage(uint64 nNonce,int nCommand,walleve::CWalleveBufStream& ssPayload);
    bool SendDelegatedMessage(uint64 nNonce,int nCommand,walleve::CWalleveBufStream& ssPayload);
    void SetInvTimer(uint64 nNonce,std::vector<CInv>& vInv);
    virtual void ProcessAskFor(walleve::CPeer* pPeer);
    void Configure(uint32 nMagicNumIn,uint32 nVersionIn,uint64 nServiceIn,const std::string& subVersionIn,bool fEnclosedIn)
    {
        nMagicNum = nMagicNumIn; nVersion = nVersionIn; nService = nServiceIn;
        subVersion = subVersionIn; fEnclosed = fEnclosedIn;
    }
    virtual bool CheckPeerVersion(uint32 nVersionIn,uint64 nServiceIn,const std::string& subVersionIn) = 0;
protected:
    IMvNetChannel* pNetChannel;
    IMvDelegatedChannel* pDelegatedChannel;
    uint32 nMagicNum;
    uint32 nVersion;
    uint64 nService;
    bool fEnclosed;
    std::string subVersion;    
    std::set<boost::asio::ip::tcp::endpoint> setDNSeed;
    SUPER_NODE_TYPE typeNode;
    typedef std::pair<uint256, uint64> ForkNonceKeyType;
    std::map<ForkNonceKeyType, std::set<uint256>> mapThisNodeGetData; 
};

} // namespace network
} // namespace multiverse

#endif //MULTIVERSE_PEERNET_H

