// Copyright (c) 2017-2018 The Multiverse developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef  MULTIVERSE_BASE_H
#define  MULTIVERSE_BASE_H

#include "param.h"
#include "error.h"
#include "uint256.h"
#include "mvtype.h"
#include "transaction.h"
#include "block.h"
#include "wallettx.h"
#include "destination.h"
#include "template.h"
#include "crypto.h"
#include "key.h"
#include "config.h"
#include "blockbase.h"
#include "mvpeer.h"

#include <walleve/walleve.h>
#include <boost/foreach.hpp>

namespace multiverse
{

class ICoreProtocol : public walleve::IWalleveBase
{
public:
    ICoreProtocol() : IWalleveBase("coreprotocol") {}
    virtual const uint256& GetGenesisBlockHash() = 0;
    virtual void GetGenesisBlock(CBlock& block) = 0;
    virtual MvErr ValidateTransaction(const CTransaction& tx) = 0;
    virtual MvErr ValidateBlock(CBlock& block) = 0;
    virtual MvErr VerifyBlock(CBlock& block,CBlockIndex* pIndexPrev) = 0;
    virtual MvErr VerifyBlockTx(CTransaction& tx,CTxContxt& txContxt,CBlockIndex* pIndexPrev) = 0;
    virtual MvErr VerifyTransaction(CTransaction& tx,const std::vector<CTxOutput>& vPrevOutput,int nForkHeight) = 0;
    virtual bool GetProofOfWorkTarget(CBlockIndex* pIndexPrev,int nAlgo,int& nBits,int64& nReward) = 0;
    virtual int GetProofOfWorkRunTimeBits(int nBits,int64 nTime,int64 nPrevTime) = 0;
};

class IWorldLine : public walleve::IWalleveBase
{
public:
    IWorldLine() : IWalleveBase("worldline") {}
    virtual void GetForkStatus(std::map<uint256,CForkStatus>& mapForkStatus) = 0;
    virtual bool GetBlockLocation(const uint256& hashBlock,uint256& hashFork,int& nHeight) = 0;
    virtual bool GetBlockHash(const uint256& hashFork,const int nHeight,uint256& hashBlock) = 0;
    virtual bool GetLastBlock(const uint256& hashFork,uint256& hashBlock,int& nHeight,int64& nTime) = 0;
    virtual bool GetBlock(const uint256& hashBlock,CBlock& block) = 0;
    virtual bool Exists(const uint256& hashBlock) = 0;
    virtual bool GetTransaction(const uint256& txid,CTransaction& tx) = 0;
    virtual bool GetTxLocation(const uint256& txid,uint256& hashFork,int& nHeight) = 0;
    virtual bool GetTxUnspent(const uint256& hashFork,const std::vector<CTxIn>& vInput,
                                                      std::vector<CTxOutput>& vOutput) = 0;
    virtual bool ExistsTx(const uint256& txid) = 0;
    virtual bool FilterTx(CTxFilter& filter) = 0;
    virtual MvErr AddNewBlock(CBlock& block,CWorldLineUpdate& update) = 0;    
    virtual bool GetProofOfWorkTarget(const uint256& hashPrev,int nAlgo,int& nBits,int64& nReward) = 0;
    virtual bool GetBlockLocator(const uint256& hashFork,CBlockLocator& locator) = 0;
    virtual bool GetBlockInv(const uint256& hashFork,const CBlockLocator& locator,std::vector<uint256>& vBlockHash,std::size_t nMaxCount) = 0;

    const CMvBasicConfig * WalleveConfig()
    {
        return dynamic_cast<const CMvBasicConfig *>(walleve::IWalleveBase::WalleveConfig());
    }
    const CMvStorageConfig * StorageConfig()
    {
        return dynamic_cast<const CMvStorageConfig *>(walleve::IWalleveBase::WalleveConfig());
    }
};

class ITxPool : public walleve::IWalleveBase
{
public:
    ITxPool() : IWalleveBase("txpool") {}
    virtual bool Exists(const uint256& txid) = 0;
    virtual void Clear() = 0;
    virtual std::size_t Count(const uint256& fork) const = 0;
    virtual MvErr Push(CTransaction& tx,uint256& hashFork,CDestination& destIn,int64& nValueIn) = 0;
    virtual void Pop(const uint256& txid) = 0;
    virtual bool Get(const uint256& txid,CTransaction& tx) const = 0;
    virtual void ListTx(const uint256& hashFork,std::vector<std::pair<uint256,std::size_t> >& vTxPool) = 0;
    virtual void ListTx(const uint256& hashFork,std::vector<uint256>& vTxPool) = 0;
    virtual bool FilterTx(CTxFilter& filter) = 0;
    virtual void ArrangeBlockTx(const uint256& hashFork,std::size_t nMaxSize,std::vector<CTransaction>& vtx,int64& nTotalTxFee) = 0;
    virtual bool FetchInputs(const uint256& hashFork,const CTransaction& tx,std::vector<CTxOutput>& vUnspent) = 0;
    virtual bool SynchronizeWorldLine(CWorldLineUpdate& update,CTxSetChange& change) = 0;
    const CMvStorageConfig * StorageConfig()
    {
        return dynamic_cast<const CMvStorageConfig *>(walleve::IWalleveBase::WalleveConfig());
    }
};

class IBlockMaker : public walleve::CWalleveEventProc
{
public:
    IBlockMaker() : CWalleveEventProc("blockmaker") {}
    const CMvMintConfig * MintConfig()
    {
        return dynamic_cast<const CMvMintConfig *>(walleve::IWalleveBase::WalleveConfig());
    }
};

class IWallet : public walleve::IWalleveBase
{
public:
    IWallet() : IWalleveBase("wallet") {}
    /* Key store */
    virtual bool AddKey(const crypto::CKey& key) = 0;
    virtual void GetPubKeys(std::set<crypto::CPubKey>& setPubKey) const = 0;
    virtual bool Have(const crypto::CPubKey& pubkey) const = 0;
    virtual bool Export(const crypto::CPubKey& pubkey,std::vector<unsigned char>& vchKey) const = 0;
    virtual bool Import(const std::vector<unsigned char>& vchKey,crypto::CPubKey& pubkey) = 0;
    virtual bool Encrypt(const crypto::CPubKey& pubkey,const crypto::CCryptoString& strPassphrase,
                                               const crypto::CCryptoString& strCurrentPassphrase) = 0;
    virtual bool GetKeyStatus(const crypto::CPubKey& pubkey,int& nVersion,bool& fLocked,int64& nAutoLockTime) const = 0;
    virtual bool IsLocked(const crypto::CPubKey& pubkey) const = 0;
    virtual bool Lock(const crypto::CPubKey& pubkey) = 0;
    virtual bool Unlock(const crypto::CPubKey& pubkey,const crypto::CCryptoString& strPassphrase,int64 nTimeout) = 0;
    virtual bool Sign(const crypto::CPubKey& pubkey,const uint256& hash,std::vector<uint8>& vchSig) const = 0;
    /* Template */
    virtual void GetTemplateIds(std::set<CTemplateId>& setTemplateId) const = 0;
    virtual bool Have(const CTemplateId& tid) const = 0;
    virtual bool AddTemplate(CTemplatePtr& ptr) = 0;
    virtual bool GetTemplate(const CTemplateId& tid,CTemplatePtr& ptr) = 0;
    /* Wallet Tx */
    virtual std::size_t GetTxCount() = 0;
    virtual bool ListTx(int nOffset,int nCount,std::vector<CWalletTx>& vWalletTx) = 0;
    virtual bool GetBalance(const CDestination& dest,const uint256& hashFork,int nForkHeight,CWalletBalance& balance) = 0;
    virtual bool SignTransaction(const CDestination& destIn,CTransaction& tx,bool& fCompleted) const = 0;
    virtual bool ArrangeInputs(const CDestination& destIn,const uint256& hashFork,int nForkHeight,CTransaction& tx) = 0; 
    /* Update */
    virtual bool SynchronizeTxSet(CTxSetChange& change) = 0;
    virtual bool UpdateTx(const uint256& hashFork,const CAssembledTx& tx) = 0;
    virtual bool ClearTx() = 0;
    const CMvBasicConfig * WalleveConfig()
    {
        return dynamic_cast<const CMvBasicConfig *>(walleve::IWalleveBase::WalleveConfig());
    }
    const CMvStorageConfig * StorageConfig()
    {
        return dynamic_cast<const CMvStorageConfig *>(walleve::IWalleveBase::WalleveConfig());
    }
};

class IDispatcher : public walleve::IWalleveBase
{
public:
    IDispatcher() : IWalleveBase("dispatcher") {}
    virtual MvErr AddNewBlock(CBlock& block,uint64 nNonce=0) = 0;
    virtual MvErr AddNewTx(CTransaction& tx,uint64 nNonce=0) = 0;
};

class IService : public walleve::IWalleveBase
{
public:
    IService() : IWalleveBase("service") {}
    /* Notify */
    virtual void NotifyWorldLineUpdate(const CWorldLineUpdate& update) = 0;
    virtual void NotifyNetworkPeerUpdate(const CNetworkPeerUpdate& update) = 0;
    /* System */
    virtual void Shutdown() = 0;
    /* Network */ 
    virtual int GetPeerCount() = 0;
    virtual void GetPeers(std::vector<network::CMvPeerInfo>& vPeerInfo) = 0;
    virtual bool AddNode(const walleve::CNetHost& node) = 0;
    virtual bool RemoveNode(const walleve::CNetHost& node) = 0;
    /* Worldline & Tx Pool*/
    virtual int  GetForkCount() = 0;
    virtual bool GetForkGenealogy(const uint256& hashFork,std::vector<std::pair<uint256,int> >& vAncestry,
                                                          std::vector<std::pair<int,uint256> >& vSubline) = 0;
    virtual bool GetBlockLocation(const uint256& hashBlock,uint256& hashFork,int& nHeight) = 0;
    virtual int  GetBlockCount(const uint256& hashFork) = 0;
    virtual bool GetBlockHash(const uint256& hashFork,const int nHeight,uint256& hashBlock) = 0;
    virtual bool GetBlock(const uint256& hashBlock,CBlock& block,uint256& hashFork,int& nHeight) = 0;
    virtual void GetTxPool(const uint256& hashFork,std::vector<std::pair<uint256,std::size_t> >& vTxPool) = 0;
    virtual bool GetTransaction(const uint256& txid,CTransaction& tx,uint256& hashFork,int& nHeight) = 0;
    virtual MvErr SendTransaction(CTransaction& tx) = 0;
    virtual bool RemovePendingTx(const uint256& txid) = 0;
    /* Wallet */
    virtual bool HaveKey(const crypto::CPubKey& pubkey) = 0;
    virtual void GetPubKeys(std::set<crypto::CPubKey>& setPubKey) = 0;
    virtual bool GetKeyStatus(const crypto::CPubKey& pubkey,int& nVersion,bool& fLocked,int64& nAutoLockTime) = 0;
    virtual bool MakeNewKey(const crypto::CCryptoString& strPassphrase,crypto::CPubKey& pubkey) = 0;
    virtual bool AddKey(const crypto::CKey& key) = 0;
    virtual bool ImportKey(const std::vector<unsigned char>& vchKey,crypto::CPubKey& pubkey) = 0;
    virtual bool ExportKey(const crypto::CPubKey& pubkey,std::vector<unsigned char>& vchKey) = 0;
    virtual bool EncryptKey(const crypto::CPubKey& pubkey,const crypto::CCryptoString& strPassphrase,
                                                          const crypto::CCryptoString& strCurrentPassphrase) = 0;
    virtual bool Lock(const crypto::CPubKey& pubkey) = 0;
    virtual bool Unlock(const crypto::CPubKey& pubkey,const crypto::CCryptoString& strPassphrase,int64 nTimeout) = 0;
    virtual bool SignSignature(const crypto::CPubKey& pubkey,const uint256& hash,std::vector<unsigned char>& vchSig) = 0;
    virtual bool SignTransaction(CTransaction& tx,bool& fCompleted) = 0; 
    virtual bool HaveTemplate(const CTemplateId& tid) = 0;
    virtual void GetTemplateIds(std::set<CTemplateId>& setTid) = 0;
    virtual bool AddTemplate(CTemplatePtr& ptr) = 0;
    virtual bool GetTemplate(const CTemplateId& tid,CTemplatePtr& ptr) = 0;
    virtual bool GetBalance(const CDestination& dest,const uint256& hashFork,CWalletBalance& balance) = 0;
    virtual bool ListWalletTx(int nOffset,int nCount,std::vector<CWalletTx>& vWalletTx) = 0;
    virtual bool CreateTransaction(const uint256& hashFork,const CDestination& destFrom,
                                   const CDestination& destSendTo,int64 nAmount,int64 nTxFee,
                                   const std::vector<unsigned char>& vchData,CTransaction& txNew) = 0;
    virtual bool SynchronizeWalletTx(const CDestination& destNew) = 0;
    virtual bool ResynchronizeWalletTx() = 0;
    /* Mint */
    virtual bool GetWork(std::vector<unsigned char>& vchWorkData,uint256& hashPrev,uint32& nPrevTime,int& nAlgo,int& nBits) = 0;
    virtual MvErr SubmitWork(const std::vector<unsigned char>& vchWorkData,CTemplatePtr& templMint,crypto::CKey& keyMint,uint256& hashBlock) = 0;
};

} // namespace multiverse

#endif //MULTIVERSE_BASE_H

