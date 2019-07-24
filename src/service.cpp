// Copyright (c) 2017-2019 The Multiverse developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "service.h"
#include "event.h"

using namespace std;
using namespace walleve;
using namespace multiverse;

extern void MvShutdown();

//////////////////////////////
// CService 

CService::CService()
: pCoreProtocol(NULL)
, pWorldLine(NULL)
, pTxPool(NULL)
, pDispatcher(NULL)
, pWallet(NULL)
, pNetwork(NULL)
, pDbpSocket(NULL)
{
}

CService::~CService()
{
}

bool CService::WalleveHandleInitialize()
{
    if (!WalleveGetObject("coreprotocol",pCoreProtocol))
    {
        WalleveError("Failed to request coreprotocol\n");
        return false;
    }

    if (!WalleveGetObject("worldline",pWorldLine))
    {
        WalleveError("Failed to request worldline\n");
        return false;
    }

    if (!WalleveGetObject("txpool",pTxPool))
    {
        WalleveError("Failed to request txpool\n");
        return false;
    }

    if (!WalleveGetObject("dispatcher",pDispatcher))
    {
        WalleveError("Failed to request dispatcher\n");
        return false;
    }

    if (!WalleveGetObject("wallet",pWallet))
    {
        WalleveError("Failed to request wallet\n");
        return false;
    }

    if (!WalleveGetObject("virtualpeernet",pNetwork))
    {
        WalleveError("Failed to request network\n");
        return false;
    }

    if (!WalleveGetObject("dbpservice", pDbpSocket))
    {
         WalleveError("Failed to request DbpSocket\n");
         return false;
    }

    if (!WalleveGetObject("forkmanager", pForkManager))
    {
         WalleveError("Failed to request forkmanager\n");
         return false;
    }

    return true;
}

void CService::WalleveHandleDeinitialize()
{
    pCoreProtocol = NULL;
    pWorldLine = NULL;
    pTxPool = NULL;
    pDispatcher = NULL;
    pWallet = NULL;
    pNetwork = NULL;
    pDbpSocket = NULL;
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

        CForkStatus &status = (*it).second;
        status.hashLastBlock = update.hashLastBlock;
        status.nLastBlockTime = update.nLastBlockTime;
        status.nLastBlockHeight = update.nLastBlockHeight;
        status.nMoneySupply = update.nMoneySupply;
    }

    uint64 nNonce;
    CMvEventDbpUpdateNewBlock *pUpdateNewBlockEvent = new CMvEventDbpUpdateNewBlock(nNonce, update.hashFork, 0);
    pUpdateNewBlockEvent->data = update.vBlockAddNew[update.vBlockAddNew.size() - 1];
    pDbpSocket->PostEvent(pUpdateNewBlockEvent);
}

void CService::NotifyNetworkPeerUpdate(const CNetworkPeerUpdate& update)
{
    (void)update;
}

void CService::NotifyTransactionUpdate(const CTransactionUpdate& update)
{
    uint64 nNonce;
    CMvEventDbpUpdateNewTx *pUpdateNewTxEvent = new CMvEventDbpUpdateNewTx(nNonce, update.hashFork, update.nChange);
    pUpdateNewTxEvent->data = update.txUpdate;
    pDbpSocket->PostEvent(pUpdateNewTxEvent);
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

bool CService::HaveFork(const uint256& hashFork)
{
    boost::shared_lock<boost::shared_mutex> rlock(rwForkStatus);

    map<uint256,CForkStatus>::iterator it = mapForkStatus.find(hashFork);
    if (it != mapForkStatus.end())
    {
        return true;
    }

    return false;
}

int32 CService::GetForkHeight(const uint256& hashFork)
{
    boost::shared_lock<boost::shared_mutex> rlock(rwForkStatus);

    map<uint256,CForkStatus>::iterator it = mapForkStatus.find(hashFork);
    if (it != mapForkStatus.end())
    {
        return ((*it).second.nLastBlockHeight);
    }
    return 0;
}

void CService::ListFork(std::vector<std::pair<uint256,CProfile> >& vFork, bool fAll)
{
    boost::shared_lock<boost::shared_mutex> rlock(rwForkStatus);
    if (fAll)
    {
        vector<uint256> vForkHash;
        pForkManager->GetForkList(vForkHash);
        vFork.reserve(vForkHash.size());
        for (vector<uint256>::iterator it = vForkHash.begin(); it != vForkHash.end(); ++it)
        {
            CForkContext ctx;
            if (pWorldLine->GetForkContext(*it, ctx))
            {
                vFork.push_back(make_pair(*it, ctx.GetProfile()));
            }
        }
    }
    else
    {
        vFork.reserve(mapForkStatus.size());
        for (map<uint256,CForkStatus>::iterator it = mapForkStatus.begin();it != mapForkStatus.end();++it)
        {
            CProfile profile;
            if (pWorldLine->GetForkProfile((*it).first,profile))
            {
                vFork.push_back(make_pair((*it).first,profile));
            }
        }
    }
}

bool CService::GetForkGenealogy(const uint256& hashFork,vector<pair<uint256,int32> >& vAncestry,vector<pair<int32,uint256> >& vSubline)
{
    boost::shared_lock<boost::shared_mutex> rlock(rwForkStatus);
    
    uint256 hashParent, hashJoint;
    int32 nJointHeight;
    if (!pForkManager->GetJoint(hashFork, hashParent, hashJoint, nJointHeight))
    {
        return false;
    }
    
    while (hashParent != 0)
    {
        vAncestry.push_back(make_pair(hashParent, nJointHeight));
        pForkManager->GetJoint(hashParent, hashParent, hashJoint, nJointHeight);
    }

    pForkManager->GetSubline(hashFork, vSubline);
    return true;
}

bool CService::GetBlockLocation(const uint256& hashBlock,uint256& hashFork,int32& nHeight)
{
    return pWorldLine->GetBlockLocation(hashBlock,hashFork,nHeight);
}

int CService::GetBlockCount(const uint256& hashFork)
{
    if (hashFork == pCoreProtocol->GetGenesisBlockHash())
    {
        return (GetForkHeight(hashFork) + 1);
    }
    return pWorldLine->GetBlockCount(hashFork);
}

bool CService::GetBlockHash(const uint256& hashFork,const int32 nHeight,uint256& hashBlock)
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

bool CService::GetBlockHash(const uint256& hashFork,const int32 nHeight,vector<uint256>& vBlockHash)
{
    return pWorldLine->GetBlockHash(hashFork,nHeight,vBlockHash);
}

bool CService::GetBlock(const uint256& hashBlock,CBlock& block,uint256& hashFork,int32& nHeight)
{
    return pWorldLine->GetBlock(hashBlock,block) 
           && pWorldLine->GetBlockLocation(hashBlock,hashFork,nHeight);
}

bool CService::GetBlockEx(const uint256& hashBlock, CBlockEx& block, uint256& hashFork, int32& nHeight)
{
    return pWorldLine->GetBlockEx(hashBlock,block)
           && pWorldLine->GetBlockLocation(hashBlock,hashFork,nHeight);
}

void CService::GetTxPool(const uint256& hashFork,vector<pair<uint256,size_t> >& vTxPool)
{
    vTxPool.clear();
    pTxPool->ListTx(hashFork,vTxPool);
}

bool CService::GetTransaction(const uint256& txid,CTransaction& tx,uint256& hashFork,int32& nHeight)
{
    if (pTxPool->Get(txid,tx))
    {
        int32 nAnchorHeight;
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

bool CService::GetKeyStatus(const crypto::CPubKey& pubkey,uint32& nVersion,bool& fLocked,int64& nAutoLockTime)
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
    int32 nHeight;
    if (!pWorldLine->GetBlockLocation(tx.hashAnchor,hashFork,nHeight))
    {
        return false;
    }
    vector<CTxOutput> vUnspent;
    if (!pTxPool->FetchInputs(hashFork,tx,vUnspent) || vUnspent.empty())
    {
        return false;
    }

    const CDestination& destIn = vUnspent[0].destTo;
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

CTemplatePtr CService::GetTemplate(const CTemplateId& tid)
{
    return pWallet->GetTemplate(tid);
}

bool CService::GetBalance(const CDestination& dest,const uint256& hashFork,CWalletBalance& balance)
{
    int32 nForkHeight = GetForkHeight(hashFork);
    if (nForkHeight <= 0)
    {
        return false;
    }
    return pWallet->GetBalance(dest,hashFork,nForkHeight,balance);
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
    int32 nForkHeight = 0;
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
    txNew.nTimeStamp = WalleveGetNetTime();
    txNew.nLockUntil = 0;
    txNew.sendTo = destSendTo;
    txNew.nAmount = nAmount;
    txNew.nTxFee = nTxFee;
    txNew.vchData = vchData;

    return pWallet->ArrangeInputs(destFrom,hashFork,nForkHeight,txNew);
}

bool CService::SynchronizeWalletTx(const CDestination& destNew)
{
    return pWallet->SynchronizeWalletTx(destNew);
}

bool CService::ResynchronizeWalletTx()
{
    return pWallet->ResynchronizeWalletTx();
}

bool CService::GetWork(std::vector<unsigned char>& vchWorkData, uint256& hashPrev, uint32& nPrevTime, int& nAlgo, int& nBits, CTemplateMintPtr& templMint)
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
 
    nAlgo = CM_BLAKE512;
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
    proof.destMint = CDestination(templMint->GetTemplateId());
    proof.nNonce = 0;
    proof.Save(block.vchProof);
    
    block.GetSerializedProofOfWorkData(vchWorkData);
    return true;
}

MvErr CService::SubmitWork(const vector<unsigned char>& vchWorkData,CTemplateMintPtr& templMint,crypto::CKey& keyMint,uint256& hashBlock)
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
        if (proof.nAlgo != CM_BLAKE512)
        {
            return MV_ERR_BLOCK_PROOF_OF_WORK_INVALID;
        }
    }
    catch (exception& e)
    {
        StdError(__PRETTY_FUNCTION__, e.what());
        return MV_FAILED;
    }
    int nBits;
    int64 nReward;
    if (!pWorldLine->GetProofOfWorkTarget(block.hashPrev,proof.nAlgo,nBits,nReward))
    {
        return MV_FAILED;
    }

    CTransaction& txMint = block.txMint;
    txMint.nType         = CTransaction::TX_WORK;
    txMint.nTimeStamp    = block.nTimeStamp;
    txMint.hashAnchor    = block.hashPrev;
    txMint.sendTo        = CDestination(templMint->GetTemplateId());
    txMint.nAmount       = nReward;

    size_t nSigSize = templMint->GetTemplateData().size() + 64 + 2;
    size_t nMaxTxSize = MAX_BLOCK_SIZE - GetSerializeSize(block) - nSigSize;
    int64 nTotalTxFee = 0;
    pTxPool->ArrangeBlockTx(pCoreProtocol->GetGenesisBlockHash(),block.nTimeStamp,nMaxTxSize,block.vtx,nTotalTxFee);
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
