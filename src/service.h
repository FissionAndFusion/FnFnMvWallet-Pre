// Copyright (c) 2017-2018 The Multiverse developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef  MULTIVERSE_SERVICE_H
#define  MULTIVERSE_SERVICE_H

#include "mvbase.h"
#include "network.h"

namespace multiverse
{

class CService : public IService
{
public:
    CService();
    ~CService();
    /* Notify */
    void NotifyWorldLineUpdate(const CWorldLineUpdate& update);
    void NotifyNetworkPeerUpdate(const CNetworkPeerUpdate& update);
    /* System */
    void Shutdown();
    /* Network */
    int GetPeerCount();
    void GetPeers(std::vector<network::CMvPeerInfo>& vPeerInfo);
    bool AddNode(const walleve::CNetHost& node);
    bool RemoveNode(const walleve::CNetHost& node);
    /* Worldline & Tx Pool*/
    int  GetForkCount();
    bool GetForkGenealogy(const uint256& hashFork,std::vector<std::pair<uint256,int> >& vAncestry,
                                                  std::vector<std::pair<int,uint256> >& vSubline);
    bool GetBlockLocation(const uint256& hashBlock,uint256& hashFork,int& nHeight);
    int  GetBlockCount(const uint256& hashFork);
    bool GetBlockHash(const uint256& hashFork,const int nHeight,uint256& hashBlock);
    bool GetBlock(const uint256& hashBlock,CBlock& block,uint256& hashFork,int& nHeight);
    void GetTxPool(const uint256& hashFork,std::vector<std::pair<uint256,std::size_t> >& vTxPool);
    bool GetTransaction(const uint256& txid,CTransaction& tx,uint256& hashFork,int& nHeight);
    MvErr SendTransaction(CTransaction& tx);
    bool RemovePendingTx(const uint256& txid);
    /* Wallet */
    bool HaveKey(const crypto::CPubKey& pubkey);
    void GetPubKeys(std::set<crypto::CPubKey>& setPubKey);
    bool GetKeyStatus(const crypto::CPubKey& pubkey,int& nVersion,bool& fLocked,int64& nAutoLockTime);
    bool MakeNewKey(const crypto::CCryptoString& strPassphrase,crypto::CPubKey& pubkey);
    bool AddKey(const crypto::CKey& key);
    bool ImportKey(const std::vector<unsigned char>& vchKey,crypto::CPubKey& pubkey);
    bool ExportKey(const crypto::CPubKey& pubkey,std::vector<unsigned char>& vchKey);
    bool EncryptKey(const crypto::CPubKey& pubkey,const crypto::CCryptoString& strPassphrase,
                                                  const crypto::CCryptoString& strCurrentPassphrase);
    bool Lock(const crypto::CPubKey& pubkey);
    bool Unlock(const crypto::CPubKey& pubkey,const crypto::CCryptoString& strPassphrase,int64 nTimeout);
    bool SignSignature(const crypto::CPubKey& pubkey,const uint256& hash,std::vector<unsigned char>& vchSig);
    bool SignTransaction(CTransaction& tx,bool& fCompleted);
    bool HaveTemplate(const CTemplateId& tid);
    void GetTemplateIds(std::set<CTemplateId>& setTid);
    bool AddTemplate(CTemplatePtr& ptr);
    bool GetTemplate(const CTemplateId& tid,CTemplatePtr& ptr);
    bool GetBalance(const CDestination& dest,const uint256& hashFork,CWalletBalance& balance);
    bool ListWalletTx(int nOffset,int nCount,std::vector<CWalletTx>& vWalletTx); 
    bool CreateTransaction(const uint256& hashFork,const CDestination& destFrom,
                           const CDestination& destSendTo,int64 nAmount,int64 nTxFee,
                           const std::vector<unsigned char>& vchData,CTransaction& txNew);
    bool SynchronizeWalletTx(const CDestination& destNew);
    bool ResynchronizeWalletTx();
    /* Mint */
    bool GetWork(std::vector<unsigned char>& vchWorkData,uint256& hashPrev,uint32& nPrevTime,int& nAlgo,int& nBits);
    MvErr SubmitWork(const std::vector<unsigned char>& vchWorkData,CTemplatePtr& templMint,crypto::CKey& keyMint,uint256& hashBlock);
    /* Util */
protected:
    bool WalleveHandleInitialize();
    void WalleveHandleDeinitialize();
    bool WalleveHandleInvoke();
    void WalleveHandleHalt();
protected:
    ICoreProtocol* pCoreProtocol;
    IWorldLine* pWorldLine;
    ITxPool* pTxPool;
    IDispatcher* pDispatcher;
    IWallet* pWallet;
    CNetwork* pNetwork;
    mutable boost::shared_mutex rwForkStatus;
    std::map<uint256,CForkStatus> mapForkStatus;
};

} // namespace multiverse

#endif //MULTIVERSE_SERVICE_H

