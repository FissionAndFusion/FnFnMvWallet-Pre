// Copyright (c) 2017-2019 The Multiverse developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef  MULTIVERSE_SERVICE_H
#define  MULTIVERSE_SERVICE_H

#include "mvbase.h"
#include "network.h"
#include "walleve/walleve.h"

namespace multiverse
{


class CService : public IService
{
public:
    CService();
    ~CService();
    /* Notify */
    void NotifyWorldLineUpdate(const CWorldLineUpdate& update) override;
    void NotifyNetworkPeerUpdate(const CNetworkPeerUpdate& update) override;
    void NotifyTransactionUpdate(const CTransactionUpdate& update) override;
    /* System */
    void Shutdown() override;
    /* Network */
    int GetPeerCount() override;
    void GetPeers(std::vector<network::CMvPeerInfo>& vPeerInfo) override;
    bool AddNode(const walleve::CNetHost& node) override;
    bool RemoveNode(const walleve::CNetHost& node) override;
    /* Worldline & Tx Pool*/
    int  GetForkCount() override;
    bool HaveFork(const uint256& hashFork) override;
    int32 GetForkHeight(const uint256& hashFork) override;
    void ListFork(std::vector<std::pair<uint256,CProfile> >& vFork, bool fAll = false) override;
    bool GetForkGenealogy(const uint256& hashFork,std::vector<std::pair<uint256,int32> >& vAncestry,
                                                  std::vector<std::pair<int32,uint256> >& vSubline) override;
    bool GetBlockLocation(const uint256& hashBlock,uint256& hashFork,int32& nHeight) override;
    int  GetBlockCount(const uint256& hashFork) override;
    bool GetBlockHash(const uint256& hashFork,const int32 nHeight,uint256& hashBlock) override;
    bool GetBlockHash(const uint256& hashFork,const int32 nHeight,std::vector<uint256>& vBlockHash) override;
    bool GetBlock(const uint256& hashBlock,CBlock& block,uint256& hashFork,int32& nHeight) override;
    bool GetBlockEx(const uint256& hashBlock, CBlockEx& block, uint256& hashFork, int32& nHeight) override;
    void GetTxPool(const uint256& hashFork,std::vector<std::pair<uint256,std::size_t> >& vTxPool) override;
    bool GetTransaction(const uint256& txid,CTransaction& tx,uint256& hashFork,int32& nHeight) override;
    MvErr SendTransaction(CTransaction& tx) override;
    bool RemovePendingTx(const uint256& txid) override;
    /* Wallet */
    bool HaveKey(const crypto::CPubKey& pubkey) override;
    void GetPubKeys(std::set<crypto::CPubKey>& setPubKey) override;
    bool GetKeyStatus(const crypto::CPubKey& pubkey,uint32& nVersion,bool& fLocked,int64& nAutoLockTime) override;
    bool MakeNewKey(const crypto::CCryptoString& strPassphrase,crypto::CPubKey& pubkey) override;
    bool AddKey(const crypto::CKey& key) override;
    bool ImportKey(const std::vector<unsigned char>& vchKey,crypto::CPubKey& pubkey) override;
    bool ExportKey(const crypto::CPubKey& pubkey,std::vector<unsigned char>& vchKey) override;
    bool EncryptKey(const crypto::CPubKey& pubkey,const crypto::CCryptoString& strPassphrase,
                                                  const crypto::CCryptoString& strCurrentPassphrase) override;
    bool Lock(const crypto::CPubKey& pubkey) override;
    bool Unlock(const crypto::CPubKey& pubkey,const crypto::CCryptoString& strPassphrase,int64 nTimeout) override;
    bool SignSignature(const crypto::CPubKey& pubkey,const uint256& hash,std::vector<unsigned char>& vchSig) override;
    bool SignTransaction(CTransaction& tx,bool& fCompleted) override;
    bool HaveTemplate(const CTemplateId& tid) override;
    void GetTemplateIds(std::set<CTemplateId>& setTid) override;
    bool AddTemplate(CTemplatePtr& ptr) override;
    CTemplatePtr GetTemplate(const CTemplateId& tid) override;
    bool GetBalance(const CDestination& dest,const uint256& hashFork,CWalletBalance& balance) override;
    bool ListWalletTx(int nOffset,int nCount,std::vector<CWalletTx>& vWalletTx) override; 
    bool CreateTransaction(const uint256& hashFork,const CDestination& destFrom,
                           const CDestination& destSendTo,int64 nAmount,int64 nTxFee,
                           const std::vector<unsigned char>& vchData,CTransaction& txNew) override;
    bool SynchronizeWalletTx(const CDestination& destNew) override;
    bool ResynchronizeWalletTx() override;
    /* Mint */
    bool GetWork(std::vector<unsigned char>& vchWorkData, uint256& hashPrev, uint32& nPrevTime, int& nAlgo, int& nBits, CTemplateMintPtr& templMint) override;
    MvErr SubmitWork(const std::vector<unsigned char>& vchWorkData,CTemplateMintPtr& templMint,crypto::CKey& keyMint,uint256& hashBlock) override;
    /* Util */
protected:
    bool WalleveHandleInitialize() override;
    void WalleveHandleDeinitialize() override;
    bool WalleveHandleInvoke() override;
    void WalleveHandleHalt() override;
protected:
    ICoreProtocol* pCoreProtocol;
    IWorldLine* pWorldLine;
    ITxPool* pTxPool;
    IDispatcher* pDispatcher;
    IWallet* pWallet;
    CNetwork* pNetwork;
    walleve::IIOModule* pDbpSocket;
    IForkManager* pForkManager;
    mutable boost::shared_mutex rwForkStatus;
    std::map<uint256,CForkStatus> mapForkStatus;
};

} // namespace multiverse

#endif //MULTIVERSE_SERVICE_H

