// Copyright (c) 2017-2018 The Multiverse developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "service.h"

using namespace std;
using namespace walleve;
using namespace multiverse;

extern void MvShutdown();

//////////////////////////////
// CServiceWalletTxFilter
class CServiceWalletTxFilter : public CTxFilter
{
public:
    CServiceWalletTxFilter(IWallet* pWalletIn,const CDestination& destNew)
    : CTxFilter(destNew,destNew),pWallet(pWalletIn)
    {
    }
    bool FoundTx(const uint256& hashFork,const CAssembledTx& tx)
    {
        return pWallet->UpdateTx(hashFork,tx);
    }
public:
    IWallet* pWallet;
};

//////////////////////////////
// CService 

CService::CService()
{
    pCoreProtocol = NULL;
    pWorldLine = NULL;
    pWallet = NULL;
    pNetwork = NULL;
}

CService::~CService()
{
}

bool CService::WalleveHandleInitialize()
{
    if (!WalleveGetObject("coreprotocol",pCoreProtocol))
    {
        WalleveLog("Failed to request coreprotocol\n");
        return false;
    }

    if (!WalleveGetObject("worldline",pWorldLine))
    {
        WalleveLog("Failed to request worldline\n");
        return false;
    }

    if (!WalleveGetObject("txpool",pTxPool))
    {
        WalleveLog("Failed to request txpool\n");
        return false;
    }

    if (!WalleveGetObject("dispatcher",pDispatcher))
    {
        WalleveLog("Failed to request dispatcher\n");
        return false;
    }

    if (!WalleveGetObject("wallet",pWallet))
    {
        WalleveLog("Failed to request wallet\n");
        return false;
    }

    if (!WalleveGetObject("peernet",pNetwork))
    {
        WalleveLog("Failed to request network\n");
        return false;
    }

    return true;
}

void CService::WalleveHandleDeinitialize()
{
    pCoreProtocol = NULL;
    pWorldLine = NULL;
    pWallet = NULL;
    pNetwork = NULL;
}

bool CService::WalleveHandleInvoke()
{
    {
        boost::unique_lock<boost::shared_mutex> wlock(rwForkStatus);
        pWorldLine->GetForkStatus(mapForkStatus);
    }
    return true;
}

void CService::WalleveHandleHalt()
{
    {
        boost::unique_lock<boost::shared_mutex> wlock(rwForkStatus);
        mapForkStatus.clear();
    }
}

void CService::NotifyWorldLineUpdate(const CWorldLineUpdate& update)
{
    boost::unique_lock<boost::shared_mutex> wlock(rwForkStatus);
    map<uint256,CForkStatus>::iterator it = mapForkStatus.find(update.hashFork);
    if (it == mapForkStatus.end())
    {
        it = mapForkStatus.insert(make_pair(update.hashFork,CForkStatus(update.hashFork,update.hashParent,update.nOriginHeight))).first;
        if (update.hashParent != 0)
        {
            mapForkStatus[update.hashParent].mapSubline.insert(make_pair(update.nOriginHeight,update.hashFork));
        }
    }
    
    CForkStatus& status = (*it).second;
    status.hashLastBlock = update.hashLastBlock;
    status.nLastBlockTime = update.nLastBlockTime;
    status.nLastBlockHeight = update.nLastBlockHeight;
    status.nMoneySupply = update.nMoneySupply;
}

void CService::NotifyNetworkPeerUpdate(const CNetworkPeerUpdate& update)
{
}

void CService::Shutdown()
{
    MvShutdown();
}

int CService::GetPeerCount()
{
    CWalleveEventPeerNetGetCount eventGetPeerCount(0);
    if (pNetwork->DispatchEvent(&eventGetPeerCount))
    {
        return eventGetPeerCount.result;
    }
    return 0;
}

void CService::GetPeers(vector<network::CMvPeerInfo>& vPeerInfo)
{
    vPeerInfo.clear();
    CWalleveEventPeerNetGetPeers eventGetPeers(0);
    if (pNetwork->DispatchEvent(&eventGetPeers))
    {
        vPeerInfo.reserve(eventGetPeers.result.size());
        for (unsigned int i = 0;i < eventGetPeers.result.size();i++)
        {
            vPeerInfo.push_back(static_cast<network::CMvPeerInfo&>(eventGetPeers.result[i]));
        }
    }
}

bool CService::AddNode(const CNetHost& node)
{
    CWalleveEventPeerNetAddNode eventAddNode(0);
    eventAddNode.data = node;
    return pNetwork->DispatchEvent(&eventAddNode);
}

bool CService::RemoveNode(const CNetHost& node)
{
    CWalleveEventPeerNetRemoveNode eventRemoveNode(0);
    eventRemoveNode.data = node;
    return pNetwork->DispatchEvent(&eventRemoveNode);
}

int  CService::GetForkCount()
{
    return mapForkStatus.size();
}

bool CService::GetForkGenealogy(const uint256& hashFork,vector<pair<uint256,int> >& vAncestry,vector<pair<int,uint256> >& vSubline)
{
    boost::shared_lock<boost::shared_mutex> rlock(rwForkStatus);
    
    map<uint256,CForkStatus>::iterator it = mapForkStatus.find(hashFork);
    if (it == mapForkStatus.end())
    {
        return false;
    }
    
    CForkStatus* pAncestry = &(*it).second;
    while (pAncestry->hashParent != 0)
    {
        vAncestry.push_back(make_pair(pAncestry->hashParent,pAncestry->nOriginHeight));
        pAncestry = &mapForkStatus[pAncestry->hashParent];
    }

    vSubline.assign((*it).second.mapSubline.begin(),(*it).second.mapSubline.end());
    return true;
}

bool CService::GetBlockLocation(const uint256& hashBlock,uint256& hashFork,int& nHeight)
{
    return pWorldLine->GetBlockLocation(hashBlock,hashFork,nHeight);
}

int CService::GetBlockCount(const uint256& hashFork)
{
    boost::shared_lock<boost::shared_mutex> rlock(rwForkStatus);

    map<uint256,CForkStatus>::iterator it = mapForkStatus.find(hashFork);
    if (it != mapForkStatus.end())
    {
        return ((*it).second.nLastBlockHeight + 1);
    }
    return 0;
}

bool CService::GetBlockHash(const uint256& hashFork,const int nHeight,uint256& hashBlock)
{
    if (nHeight < 0)
    {
        boost::shared_lock<boost::shared_mutex> rlock(rwForkStatus);
        map<uint256,CForkStatus>::iterator it = mapForkStatus.find(hashFork);
        if (it == mapForkStatus.end())
        {
            return false;
        }
        hashBlock = (*it).second.hashLastBlock;
        return true;
    }
    return pWorldLine->GetBlockHash(hashFork,nHeight,hashBlock);
}

bool CService::GetBlock(const uint256& hashBlock,CBlock& block,uint256& hashFork,int& nHeight)
{
    return pWorldLine->GetBlock(hashBlock,block) 
           && pWorldLine->GetBlockLocation(hashBlock,hashFork,nHeight);
}

void CService::GetTxPool(const uint256& hashFork,vector<pair<uint256,size_t> >& vTxPool)
{
    vTxPool.clear();
    pTxPool->ListTx(hashFork,vTxPool);
}

bool CService::GetTransaction(const uint256& txid,CTransaction& tx,uint256& hashFork,int& nHeight)
{
    if (pTxPool->Get(txid,tx))
    {
        int nAnchorHeight;
        if (!pWorldLine->GetBlockLocation(tx.hashAnchor,hashFork,nAnchorHeight))
        {
            return false;
        }
        nHeight = -1;
        return true; 
    }
    if (!pWorldLine->GetTransaction(txid,tx))
    {
        return false;
    }
    return pWorldLine->GetTxLocation(txid,hashFork,nHeight);
}

MvErr CService::SendTransaction(CTransaction& tx)
{
    return pDispatcher->AddNewTx(tx);
}

bool CService::RemovePendingTx(const uint256& txid)
{
    if (!pTxPool->Exists(txid))
    {
        return false;
    }
    pTxPool->Pop(txid);
    return true;
}

bool CService::HaveKey(const crypto::CPubKey& pubkey)
{
    return pWallet->Have(pubkey);
}

void CService::GetPubKeys(set<crypto::CPubKey>& setPubKey)
{
    pWallet->GetPubKeys(setPubKey);
}

bool CService::GetKeyStatus(const crypto::CPubKey& pubkey,int& nVersion,bool& fLocked,int64& nAutoLockTime)
{
    return pWallet->GetKeyStatus(pubkey,nVersion,fLocked,nAutoLockTime);
}

bool CService::MakeNewKey(const crypto::CCryptoString& strPassphrase,crypto::CPubKey& pubkey)
{
    crypto::CKey key;
    if (!key.Renew())
    {
        return false;
    }
    if (!strPassphrase.empty())
    {
        if (!key.Encrypt(strPassphrase))
        {
            return false;
        }
        key.Lock();
    }
    pubkey = key.GetPubKey();
    return pWallet->AddKey(key);
}

bool CService::AddKey(const crypto::CKey& key)
{
    return pWallet->AddKey(key);
}

bool CService::ImportKey(const vector<unsigned char>& vchKey,crypto::CPubKey& pubkey)
{
    return pWallet->Import(vchKey,pubkey);
}

bool CService::ExportKey(const crypto::CPubKey& pubkey,vector<unsigned char>& vchKey)
{
    return pWallet->Export(pubkey,vchKey);
}

bool CService::EncryptKey(const crypto::CPubKey& pubkey,const crypto::CCryptoString& strPassphrase,
                                                        const crypto::CCryptoString& strCurrentPassphrase)
{
    return pWallet->Encrypt(pubkey,strPassphrase,strCurrentPassphrase);
}

bool CService::Lock(const crypto::CPubKey& pubkey)
{
    return pWallet->Lock(pubkey);
}

bool CService::Unlock(const crypto::CPubKey& pubkey,const crypto::CCryptoString& strPassphrase,int64 nTimeout)
{
    return pWallet->Unlock(pubkey,strPassphrase,nTimeout);
}

bool CService::SignSignature(const crypto::CPubKey& pubkey,const uint256& hash,vector<unsigned char>& vchSig)
{
    return pWallet->Sign(pubkey,hash,vchSig);
}

bool CService::SignTransaction(CTransaction& tx,bool& fCompleted)
{
    uint256 hashFork;
    int nHeight;
    if (!pWorldLine->GetBlockLocation(tx.hashAnchor,hashFork,nHeight))
    {
        return false;
    }
    vector<CTxOutput> vUnspent;
    if (!pTxPool->FetchInputs(hashFork,tx,vUnspent) || vUnspent.empty())
    {
        return false;
    }

    CDestination destIn = vUnspent[0].destTo;
    if (!pWallet->SignTransaction(destIn,tx,fCompleted))
    {
        return false;
    }
    return (!fCompleted 
            || (pCoreProtocol->ValidateTransaction(tx) == MV_OK 
                && pCoreProtocol->VerifyTransaction(tx,vUnspent,nHeight) == MV_OK));
}

bool CService::HaveTemplate(const CTemplateId& tid)
{
    return pWallet->Have(tid);
}

void CService::GetTemplateIds(set<CTemplateId>& setTid)
{
    pWallet->GetTemplateIds(setTid);
}

bool CService::AddTemplate(CTemplatePtr& ptr)
{
    return pWallet->AddTemplate(ptr);
}

bool CService::GetTemplate(const CTemplateId& tid,CTemplatePtr& ptr)
{
    return pWallet->GetTemplate(tid,ptr);
}

bool CService::GetBalance(const CDestination& dest,const uint256& hashFork,CWalletBalance& balance)
{
    int nBlockCount = GetBlockCount(hashFork);
    if (nBlockCount <= 0)
    {
        return false;
    }
    return pWallet->GetBalance(dest,hashFork,nBlockCount - 1,balance);
}

bool CService::ListWalletTx(int nOffset,int nCount,vector<CWalletTx>& vWalletTx)
{
    if (nOffset < 0)
    {
        nOffset = pWallet->GetTxCount() - nCount;
        if (nOffset < 0)
        {
            nOffset = 0;
        }
    }
    return pWallet->ListTx(nOffset,nCount,vWalletTx);
}

bool CService::CreateTransaction(const uint256& hashFork,const CDestination& destFrom,
                                 const CDestination& destSendTo,int64 nAmount,int64 nTxFee,
                                 const vector<unsigned char>& vchData,CTransaction& txNew)
{
    int nForkHeight = 0;
    txNew.SetNull();
    {
        boost::shared_lock<boost::shared_mutex> rlock(rwForkStatus);
        map<uint256,CForkStatus>::iterator it = mapForkStatus.find(hashFork);
        if (it == mapForkStatus.end())
        {
            return false;
        }
        nForkHeight = (*it).second.nLastBlockHeight;
        txNew.hashAnchor = (*it).second.hashLastBlock;
    }
    txNew.nType = CTransaction::TX_TOKEN;
    txNew.nLockUntil = 0;
    txNew.sendTo = destSendTo;
    txNew.nAmount = nAmount;
    txNew.nTxFee = nTxFee;
    txNew.vchData = vchData;

    return pWallet->ArrangeInputs(destFrom,hashFork,nForkHeight,txNew);
}

bool CService::SynchronizeWalletTx(const CDestination& destNew)
{
    CServiceWalletTxFilter txFilter(pWallet,destNew);
    return (pWorldLine->FilterTx(txFilter) && pTxPool->FilterTx(txFilter));
}

bool CService::ResynchronizeWalletTx()
{
    if (!pWallet->ClearTx())
    {
        return false;
    }
    
    set<crypto::CPubKey> setPubKey;
    pWallet->GetPubKeys(setPubKey);
    BOOST_FOREACH(const crypto::CPubKey& pubkey,setPubKey)
    {
        if (!SynchronizeWalletTx(CDestination(pubkey)))
        {
            return false;
        }
    }
    set<CTemplateId> setTemplateId;
    pWallet->GetTemplateIds(setTemplateId);
    BOOST_FOREACH(const CTemplateId& tid,setTemplateId)
    {
        if (!SynchronizeWalletTx(CDestination(tid)))
        {
            return false;
        }
    }
    return true;
}

bool CService::GetWork(vector<unsigned char>& vchWorkData,uint256& hashPrev,uint32& nPrevTime,int& nAlgo,int& nBits)
{
    CBlock block;
    block.nType = CBlock::BLOCK_PRIMARY;
    
    {
        boost::shared_lock<boost::shared_mutex> rlock(rwForkStatus);
        map<uint256,CForkStatus>::iterator it = mapForkStatus.find(pCoreProtocol->GetGenesisBlockHash());
        if (it == mapForkStatus.end())
        {
            return false;
        }
        hashPrev = (*it).second.hashLastBlock;
        nPrevTime = (*it).second.nLastBlockTime;
        block.hashPrev = hashPrev;
        block.nTimeStamp = nPrevTime + BLOCK_TARGET_SPACING - 10;
    }
 
    nAlgo = POWA_BLAKE512;
    int64 nReward;
    if (!pWorldLine->GetProofOfWorkTarget(block.hashPrev,nAlgo,nBits,nReward))
    {
        return false;
    }

    CProofOfHashWork proof;
    proof.nWeight = 0;
    proof.nAgreement = 0;
    proof.nAlgo = nAlgo;
    proof.nBits = nBits;
    proof.nNonce = 0;
    proof.Save(block.vchProof);
    
    block.GetSerializedProofOfWorkData(vchWorkData);
    return true;
}

MvErr CService::SubmitWork(const vector<unsigned char>& vchWorkData,CTemplatePtr& templMint,crypto::CKey& keyMint,uint256& hashBlock)
{
    if (vchWorkData.empty())
    {
        return MV_FAILED;
    }
    CBlock block;
    CProofOfHashWorkCompact proof;
    CWalleveBufStream ss;
    ss.Write((const char*)&vchWorkData[0],vchWorkData.size());
    try
    {
        ss >> block.nVersion >> block.nType >> block.nTimeStamp >> block.hashPrev >> block.vchProof;
        proof.Load(block.vchProof);
        if (proof.nAlgo != POWA_BLAKE512)
        {
            return MV_ERR_BLOCK_PROOF_OF_WORK_INVALID;
        }
    }
    catch (...)
    {
        return MV_FAILED;
    }
    int nBits;
    int64 nReward;
    if (!pWorldLine->GetProofOfWorkTarget(block.hashPrev,proof.nAlgo,nBits,nReward))
    {
        return MV_FAILED;
    }

    CTransaction& txMint = block.txMint;
    txMint.nType = CTransaction::TX_WORK;
    txMint.hashAnchor = block.hashPrev;
    txMint.sendTo = CDestination(templMint->GetTemplateId());
    txMint.nAmount = nReward;

    size_t nSigSize = templMint->GetTemplateDataSize() + 64 + 2;
    size_t nMaxTxSize = MAX_BLOCK_SIZE - GetSerializeSize(block) - nSigSize;
    int64 nTotalTxFee = 0;
    pTxPool->ArrangeBlockTx(pCoreProtocol->GetGenesisBlockHash(),nMaxTxSize,block.vtx,nTotalTxFee);
    block.hashMerkle = block.CalcMerkleTreeRoot();
    block.txMint.nAmount += nTotalTxFee;

    hashBlock = block.GetHash();
    vector<unsigned char> vchMintSig;
    if (!keyMint.Sign(hashBlock,vchMintSig) 
        || !templMint->BuildBlockSignature(hashBlock,vchMintSig,block.vchSig))
    {
        return MV_ERR_BLOCK_SIGNATURE_INVALID;
    }

    MvErr err = pCoreProtocol->ValidateBlock(block);
    if (err != MV_OK)
    {
        return err;
    } 
    return pDispatcher->AddNewBlock(block);
}
