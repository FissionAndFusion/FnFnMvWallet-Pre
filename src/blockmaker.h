// Copyright (c) 2017-2019 The Multiverse developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef  MULTIVERSE_BLOCKMAKER_H
#define  MULTIVERSE_BLOCKMAKER_H

#include "mvbase.h"
#include "event.h"
#include "key.h"
namespace multiverse
{

class CBlockMakerHashAlgo
{
public: 
    CBlockMakerHashAlgo(const std::string& strAlgoIn,int64 nHashRateIn) : strAlgo(strAlgoIn),nHashRate(nHashRateIn) {}
    virtual ~CBlockMakerHashAlgo(){}
    const std::string strAlgo;
    int64 nHashRate;
public:
    virtual uint256 Hash(const std::vector<unsigned char>& vchData) = 0;
};

class CBlockMakerProfile
{
public:
    CBlockMakerProfile() {}
    CBlockMakerProfile(int nAlgoIn,const CDestination& dest,const uint256& nPrivKey) 
    : nAlgo(nAlgoIn),destMint(dest) 
    {
        keyMint.SetSecret(crypto::CCryptoKeyData(nPrivKey.begin(),nPrivKey.end()));
        BuildTemplate();
    }
    
    bool IsValid() const { return (templMint != NULL); }
    bool BuildTemplate();
    std::size_t GetSignatureSize() const
    {
        std::size_t size = templMint->GetTemplateDataSize() + 64;
        walleve::CVarInt var(size);
        return (size + walleve::GetSerializeSize(var));
    }
    const CDestination GetDestination() const { return (CDestination(templMint->GetTemplateId())); }
public:
    int nAlgo;
    CDestination destMint;
    crypto::CKey keyMint;
    CTemplatePtr templMint;
};

class CBlockMaker : public IBlockMaker, virtual public CMvBlockMakerEventListener
{
public:
    CBlockMaker();
    ~CBlockMaker();
    bool HandleEvent(CMvEventBlockMakerUpdate& eventUpdate) override;
protected:
    bool WalleveHandleInitialize() override;
    void WalleveHandleDeinitialize() override;
    bool WalleveHandleInvoke() override;
    void WalleveHandleHalt() override;
    bool Interrupted() { return (nMakerStatus != MakerStatus::MAKER_RUN); }
    bool Wait(long nSeconds);
    bool Wait(long nSeconds,const uint256& hashPrimaryBlock);
    void PrepareBlock(CBlock& block,const uint256& hashPrev,int64 nPrevTime,int nPrevHeight,const CBlockMakerAgreement& agreement);
    void ArrangeBlockTx(CBlock& block,const uint256& hashFork,const CBlockMakerProfile& profile);
    bool SignBlock(CBlock& block,const CBlockMakerProfile& profile);
    bool DispatchBlock(CBlock& block);
    bool CreateProofOfWorkBlock(CBlock& block);
    void ProcessDelegatedProofOfStake(CBlock& block,const CBlockMakerAgreement& agreement,int nPrevHeight);
    void ProcessExtended(const CBlockMakerAgreement& agreement,const uint256& hashPrimaryBlock,
                                                               int64 nPrimaryBlockTime,int nPrimaryBlockHeight);
    bool CreateDelegatedBlock(CBlock& block,const uint256& hashFork,const CBlockMakerProfile& profile,std::size_t nWeight);
    bool CreateProofOfWork(CBlock& block,CBlockMakerHashAlgo* pHashAlgo);
    void CreatePiggyback(const CBlockMakerProfile& profile,const CBlockMakerAgreement& agreement,const CBlock& refblock,int nPrevHeight); 
    void CreateExtended(const CBlockMakerProfile& profile,const CBlockMakerAgreement& agreement,const std::set<uint256>& setFork,int nPrimaryBlockHeight,int64 nTime); 
    bool GetAvailiableDelegatedProfile(const std::vector<CDestination>& vBallot,std::vector<CBlockMakerProfile*>& vProfile);
    bool GetAvailiableExtendedFork(std::set<uint256>& setFork);
private:
    enum class MakerStatus : int {MAKER_RUN=0,MAKER_RESET=1,MAKER_EXIT=2,MAKER_HOLD=3};
    void BlockMakerThreadFunc();
    void ExtendedMakerThreadFunc();
protected:
    mutable boost::shared_mutex rwAccess;
    walleve::CWalleveThread thrMaker;
    walleve::CWalleveThread thrExtendedMaker;
    boost::mutex mutex;
    boost::condition_variable cond;
    MakerStatus nMakerStatus;
    uint256 hashLastBlock;
    int64 nLastBlockTime;
    int nLastBlockHeight;
    uint256 nLastAgreement;
    std::size_t nLastWeight;
    CBlockMakerAgreement currentAgreement;
    std::map<int,CBlockMakerHashAlgo*> mapHashAlgo;
    std::map<int,CBlockMakerProfile> mapWorkProfile;
    std::map<CDestination,CBlockMakerProfile> mapDelegatedProfile;
    ICoreProtocol* pCoreProtocol;
    IWorldLine* pWorldLine;
    IForkManager* pForkManager;
    ITxPool* pTxPool;
    IDispatcher* pDispatcher;
    IConsensus* pConsensus;
};

// BlockMaker for forknode of supernode
class CForkBlockMaker : public IBlockMaker, virtual public CMvBlockMakerEventListener
{
public:
    CForkBlockMaker();
    ~CForkBlockMaker();
    bool HandleEvent(CMvEventBlockMakerUpdate& eventUpdate) override;
protected:
    bool WalleveHandleInitialize() override;
    void WalleveHandleDeinitialize() override;
    bool WalleveHandleInvoke() override;
    void WalleveHandleHalt() override;
    bool Interrupted() { return (nMakerStatus != ForkMakerStatus::MAKER_RUN); }
    bool Wait(long nSeconds);
    bool Wait(long nSeconds,const uint256& hashPrimaryBlock);
    void PrepareBlock(CBlock& block,const uint256& hashPrev,int64 nPrevTime,int nPrevHeight,const CBlockMakerAgreement& agreement);
    void ArrangeBlockTx(CBlock& block,const uint256& hashFork,const CBlockMakerProfile& profile);
    bool SignBlock(CBlock& block,const CBlockMakerProfile& profile);
    bool DispatchBlock(CBlock& block);
    bool CreateProofOfWorkBlock(CBlock& block);
    void ProcessDelegatedProofOfStake(CBlock& block,const CBlockMakerAgreement& agreement,int nPrevHeight);
    void ProcessExtended(const CBlockMakerAgreement& agreement,const uint256& hashPrimaryBlock,
                                                               int64 nPrimaryBlockTime,int nPrimaryBlockHeight);
    bool CreateDelegatedBlock(CBlock& block,const uint256& hashFork,const CBlockMakerProfile& profile,std::size_t nWeight);
    bool CreateProofOfWork(CBlock& block,CBlockMakerHashAlgo* pHashAlgo);
    void CreatePiggyback(const CBlockMakerProfile& profile,const CBlockMakerAgreement& agreement,const CBlock& refblock,int nPrevHeight); 
    void CreateExtended(const CBlockMakerProfile& profile,const CBlockMakerAgreement& agreement,const std::set<uint256>& setFork,int nPrimaryBlockHeight,int64 nTime); 
    bool GetAvailiableDelegatedProfile(const std::vector<CDestination>& vBallot,std::vector<CBlockMakerProfile*>& vProfile);
    bool GetAvailiableExtendedFork(std::set<uint256>& setFork);
private:
    enum class ForkMakerStatus : int {MAKER_RUN=0,MAKER_RESET=1,MAKER_EXIT=2,MAKER_HOLD=3};
    void BlockMakerThreadFunc();
    void ExtendedMakerThreadFunc();
protected:
    mutable boost::shared_mutex rwAccess;
    walleve::CWalleveThread thrMaker;
    walleve::CWalleveThread thrExtendedMaker;
    boost::mutex mutex;
    boost::condition_variable cond;
    ForkMakerStatus nMakerStatus;
    uint256 hashLastBlock;
    int64 nLastBlockTime;
    int nLastBlockHeight;
    uint256 nLastAgreement;
    std::size_t nLastWeight;
    CBlockMakerAgreement currentAgreement;
    std::map<int,CBlockMakerHashAlgo*> mapHashAlgo;
    std::map<int,CBlockMakerProfile> mapWorkProfile;
    std::map<CDestination,CBlockMakerProfile> mapDelegatedProfile;
    ICoreProtocol* pCoreProtocol;
    IWorldLine* pWorldLine;
    IForkManager* pForkManager;
    ITxPool* pTxPool;
    IDispatcher* pDispatcher;
    IConsensus* pConsensus;
};

} // namespace multiverse

#endif //MULTIVERSE_BLOCKMAKER_H

