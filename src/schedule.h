// Copyright (c) 2017-2018 The Multiverse developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#ifndef  MULTIVERSE_SCHEDULE_H
#define  MULTIVERSE_SCHEDULE_H

#include "mvproto.h"
#include "mvtype.h"
#include "block.h"
#include "transaction.h"

#include <boost/foreach.hpp>
#include <boost/variant.hpp>

namespace multiverse
{

class CInvPeer
{
    class CInvPeerState
    {
    public:
        std::list<uint256> listKnown;
        std::set<uint256> setAssigned;
    };
public:
    bool Empty(uint32 type) const
    {
        return GetKnownList(type).empty(); 
    }
    std::size_t GetCount(uint32 type) const
    { 
        return GetKnownList(type).size(); 
    }
    std::list<uint256>& GetKnownList(uint32 type)
    {
        return invKnown[type - network::CInv::MSG_TX].listKnown;
    }
    std::set<uint256>& GetAssigned(uint32 type)
    {
        return invKnown[type - network::CInv::MSG_TX].setAssigned;
    }
    const std::list<uint256>& GetKnownList(uint32 type) const
    {
        return invKnown[type - network::CInv::MSG_TX].listKnown;
    }
    const std::set<uint256>& GetAssigned(uint32 type) const
    {
        return invKnown[type - network::CInv::MSG_TX].setAssigned;
    }
    void GetKnownInv(std::vector<network::CInv>& vInv)
    {
        BOOST_FOREACH(const uint256& hash,invKnown[1].listKnown)
        {
            vInv.push_back(network::CInv(network::CInv::MSG_BLOCK,hash));
        }
        BOOST_FOREACH(const uint256& hash,invKnown[0].listKnown)
        {
            vInv.push_back(network::CInv(network::CInv::MSG_TX,hash));
        }
    }
    void AddNewInv(const network::CInv& inv)
    {
        std::list<uint256>& listKnown = GetKnownList(inv.nType);
        listKnown.remove(inv.nHash);
        listKnown.push_back(inv.nHash);
    }
    void RemoveInv(const network::CInv& inv)
    {
        GetKnownList(inv.nType).remove(inv.nHash);
        GetAssigned(inv.nType).erase(inv.nHash);
    }
    void Assign(const network::CInv& inv)
    {
        GetAssigned(inv.nType).insert(inv.nHash);
    }
    void Completed(const network::CInv& inv)
    {
        GetAssigned(inv.nType).erase(inv.nHash);
    }
    bool IsAssigned()
    {
        return (!invKnown[0].setAssigned.empty() || !invKnown[1].setAssigned.empty());
    }
public:
    CInvPeerState invKnown[2];
};

class COrphan
{
public:
    std::size_t GetSize();
    void AddNew(const uint256& prev,const uint256& hash);
    void Remove(const uint256& hash);
    void GetNext(const uint256& prev,std::vector<uint256>& vNext);
    void GetNext(const uint256& prev,std::vector<uint256>& vNext,std::set<uint256>& setHash);
    void RemoveNext(const uint256& prev);
    void RemoveBranch(const uint256& root,std::vector<uint256>& vBranch);
protected:
    std::multimap<uint256,uint256> mapOrphanByPrev;
};

class CSchedule
{
    typedef boost::variant<CNil,CBlock,CTransaction> CInvObject;
    class CInvState
    {
    public:
        CInvState() : nAssigned(0),objReceived(CNil()) {}
        bool IsReceived() {return (objReceived.type() != typeid(CNil));}
    public:
        uint64 nAssigned;
        CInvObject objReceived;
        std::set<uint64> setKnownPeer;
    };
public:
    bool Exists(const network::CInv& inv);
    void GetKnownPeer(const network::CInv& inv,std::set<uint64>& setKnownPeer);
    void RemovePeer(uint64 nPeerNonce,std::set<uint64>& setSchedPeer);
    void AddNewInv(const network::CInv& inv,uint64 nPeerNonce);
    void RemoveInv(const network::CInv& inv,std::set<uint64>& setKnownPeer);
    bool ReceiveBlock(uint64 nPeerNonce,const uint256& hash,const CBlock& block,std::set<uint64>& setSchedPeer);
    bool ReceiveTx(uint64 nPeerNonce,const uint256& txid,const CTransaction& tx,std::set<uint64>& setSchedPeer);
    CBlock* GetBlock(const uint256& hash,uint64& nNonceSender);
    CTransaction* GetTransaction(const uint256& txid,uint64& nNonceSender);
    void AddOrphanBlockPrev(const uint256& hash,const uint256& prev);
    void AddOrphanTxPrev(const uint256& txid,const uint256& prev);
    void GetNextBlock(const uint256& hash,std::vector<uint256>& vNext);
    void GetNextTx(const uint256& txid,std::vector<uint256>& vNext,std::set<uint256>& setTx);
    void InvalidateBlock(const uint256& hash,std::set<uint64>& setMisbehavePeer);
    void InvalidateTx(const uint256& txid,std::set<uint64>& setMisbehavePeer);
    bool ScheduleBlockInv(uint64 nPeerNonce,std::vector<network::CInv>& vInv,std::size_t nMaxCount,bool& fMissingPrev,bool& fEmpty);
    bool ScheduleTxInv(uint64 nPeerNonce,std::vector<network::CInv>& vInv,std::size_t nMaxCount);
    const std::map<uint64,CInvPeer>& GetPeers();
protected:
    void RemoveOrphan(const network::CInv& inv);
    bool ScheduleKnownInv(uint64 nPeerNonce,CInvPeer& peer,uint32 type,
                                            std::vector<network::CInv>& vInv,std::size_t nMaxCount,bool& fReceivedAll);
protected:
    enum {MAX_INV_COUNT = 409600,MAX_PEER_BLOCK_INV_COUNT = 256,MAX_PEER_TX_INV_COUNT = 8192};
    COrphan orphanBlock;
    COrphan orphanTx;
    std::map<uint64,CInvPeer> mapPeer;
    std::map<network::CInv,CInvState> mapState;
};

} // namespace multiverse

#endif //MULTIVERSE_SCHEDULE_H
