// Copyright (c) 2017-2018 The Multiverse developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "dbpservice.h"

#include "dbputils.h"
#include <boost/assign/list_of.hpp>
#include <boost/lexical_cast.hpp>
#include <algorithm>

using namespace multiverse;

CDbpService::CDbpService()
{
    pService = NULL;
    pCoreProtocol = NULL;
    pWallet = NULL;
    pDbpServer = NULL;
    pNetChannel = NULL;
    pVirtualPeerNet = NULL;
    pForkManager = NULL;

    std::unordered_map<std::string, IdsType> temp_map = 
        boost::assign::map_list_of(ALL_BLOCK_TOPIC, std::set<std::string>())
                                  (ALL_TX_TOPIC,    std::set<std::string>())
                                  (SYS_CMD_TOPIC,   std::set<std::string>())
                                  (TX_CMD_TOPIC,    std::set<std::string>())
                                  (BLOCK_CMD_TOPIC, std::set<std::string>())
                                  (CHANGED_TOPIC,   std::set<std::string>())
                                  (REMOVED_TOPIC,   std::set<std::string>());

    mapTopicIds = temp_map;
    fIsForkNode = true;
}

CDbpService::~CDbpService()
{
}

bool CDbpService::WalleveHandleInitialize()
{
    if (!WalleveGetObject("coreprotocol", pCoreProtocol))
    {
        WalleveLog("Failed to request coreprotocol\n");
        return false;
    }

    if (!WalleveGetObject("service", pService))
    {
        WalleveLog("Failed to request service\n");
        return false;
    }

    if (!WalleveGetObject("wallet", pWallet))
    {
        WalleveLog("Failed to request wallet\n");
        return false;
    }

    if (!WalleveGetObject("dbpserver", pDbpServer))
    {
        WalleveLog("Failed to request dbpserver\n");
        return false;
    }

    if(!WalleveGetObject("dbpclient",pDbpClient))
    {
        WalleveLog("Failed to request dbpclient\n");
        return false;
    }

    if (!WalleveGetObject("netchannel",pNetChannel))
    {
        WalleveLog("Failed to request peer net datachannel\n");
        return false;
    }

    if (!WalleveGetObject("forkpseudopeernet",pVirtualPeerNet))
    {
        WalleveLog("Failed to request virtual peer net\n");
        return false;
    }
    
    if(!WalleveGetObject("forkmanager", pForkManager))
    {
        WalleveLog("Failed to request fork manager\n");
        return false;
    }

    return true;
}

void CDbpService::WalleveHandleDeinitialize()
{
    pDbpServer = NULL;
    pService = NULL;
    pCoreProtocol = NULL;
    pWallet = NULL;
    pNetChannel = NULL;
    pVirtualPeerNet = NULL;
    pForkManager = NULL;
}

bool CDbpService::HandleEvent(CMvEventDbpPong& event)
{
    (void)event;
    return true;
}

bool CDbpService::HandleEvent(CMvEventDbpBroken& event)
{
    mapSessionChildNodeForks.erase(event.strSessionId);
    return true;
}

bool CDbpService::HandleEvent(CMvEventDbpRemoveSession& event)
{
    RemoveSession(event.data.session);
    return true;
}

static std::string GetHex(std::string data)
{
    int n = 2 * data.length() + 1;
    std::string ret;
    const char c_map[16] = {'0', '1', '2', '3', '4', '5', '6', '7',
                            '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

    ret.reserve(n);
    for (const unsigned char &c : data)
    {
        ret.push_back(c_map[c >> 4]);
        ret.push_back(c_map[c & 15]);
    }

    return ret;
}

static void print_block(const CMvDbpBlock &block)
{
    uint256 hash(std::vector<unsigned char>(block.hash.begin(),block.hash.end()));
    uint256 prevHash(std::vector<unsigned char>(block.hashPrev.begin(),block.hashPrev.end()));
    uint256 forkHash(std::vector<unsigned char>(block.fork.begin(),block.fork.end()));
    
    std::cout << "[<]recived block" << std::endl;
    std::cout << "   hash: " << hash.ToString() << std::endl;
    std::cout << "   height: " << block.nHeight << std::endl;
    std::cout << "   prev hash: " << prevHash.ToString() << std::endl;
    std::cout << "   fork hash: " << forkHash.ToString() << std::endl;

}

static void print_tx(const CMvDbpTransaction &tx)
{
    uint256 hash(std::vector<unsigned char>(tx.hash.begin(),tx.hash.end()));
    uint256 sig(std::vector<unsigned char>(tx.vchSig.begin(),tx.vchSig.end()));
    uint256 forkHash(std::vector<unsigned char>(tx.fork.begin(),tx.fork.end()));

    std::cout << "[<]recived transaction" << std::endl;
    std::cout << "   hash:" << hash.ToString() << std::endl;
    std::cout << "   sig:" << sig.ToString() << std::endl;
    std::cout << "   fork hash:" << forkHash.ToString() << std::endl;
}

static void print_syscmd(const CMvDbpSysCmd &cmd)
{
    uint256 forkHash(std::vector<unsigned char>(cmd.fork.begin(),cmd.fork.end()));

    std::cout << "[<]recived sys cmd" << std::endl;
    std::cout << "   fork hash:" << forkHash.ToString() << std::endl;
}

static void print_blockcmd(const CMvDbpBlockCmd &cmd)
{
    uint256 hash(std::vector<unsigned char>(cmd.hash.begin(),cmd.hash.end()));
    uint256 forkHash(std::vector<unsigned char>(cmd.fork.begin(),cmd.fork.end()));

    std::cout << "[<]recived block cmd" << std::endl;
    std::cout << "   hash:" << hash.ToString() << std::endl;
    std::cout << "   fork hash:" << forkHash.ToString() << std::endl;
}

static void print_txcmd(const CMvDbpTxCmd &cmd)
{
    uint256 hash(std::vector<unsigned char>(cmd.hash.begin(),cmd.hash.end()));
    uint256 forkHash(std::vector<unsigned char>(cmd.fork.begin(),cmd.fork.end()));

    std::cout << "[<]recived tx cmd" << std::endl;
    std::cout << "   hash:" << hash.ToString() << std::endl;
    std::cout << "   fork hash:" << forkHash.ToString() << std::endl;
}

void CDbpService::UpdateThisNodeForkToplogy()
{
    for(const auto& kv : mapThisNodeForkStates)
    {
        std::string forkid = kv.first;
        uint256 thisNodeForkid;
        thisNodeForkid.SetHex(forkid);
        mapThisNodeForkToplogy[forkid] = CalcForkToplogy(thisNodeForkid);
    }
}

void CDbpService::UpdateChildNodesForkToplogy()
{
    for(const auto& kv : mapChildNodeForksStates)
    {
        std::string forkid = kv.first;
        uint256 childNodeForkid;
        childNodeForkid.SetHex(forkid);
        mapChildNodesForkToplogy[forkid] = CalcForkToplogy(childNodeForkid);
    }
}

void CDbpService::HandleAddedBlock(const CMvDbpBlock& block)
{
    uint256 forkHash(std::vector<unsigned char>(block.fork.begin(),block.fork.end()));
    uint256 blockHash(std::vector<unsigned char>(block.hash.begin(), block.hash.end()));
    uint256 blockPreHash(std::vector<unsigned char>(block.hashPrev.begin(), block.hashPrev.end()));
    int blockHeight = block.nHeight;

    if(IsMainFork(forkHash))
    {
       UpdateThisNodeForkToplogy();
       UpdateChildNodesForkToplogy();
    }

    if(IsMyFork(forkHash) || IsMainFork(forkHash) || IsInMyForkPath(forkHash, blockHeight))
    {
        // THIS FORK NODE Handle this TODO
        print_block(block);


        if(!IsBlockExist(blockHash) && IsBlockExist(blockPreHash))
        {
            // process normal
            pVirtualPeerNet->DispatchEvent(NULL);
        }
        else
        {
            UpdateThisNodeForkState(forkHash);
            UpdateChildNodeForksStatesToParent();
        }
    }

    if(IsChildNodeFork(forkHash) || IsMainFork(forkHash) || IsInChildNodeForkPath(forkHash, blockHeight))
    {
        PushBlock(forkHash.ToString(), block);
    }

}

void CDbpService::HandleAddedTx(const CMvDbpTransaction& tx)
{
    uint256 forkHash(std::vector<unsigned char>(tx.fork.begin(),tx.fork.end()));

    if(IsMyFork(forkHash) || IsMainFork(forkHash))
    {
        // THIS FORK NODE Handle this TODO
        print_tx(tx);

        pVirtualPeerNet->DispatchEvent(NULL);

        if(IsMainFork(forkHash))
        {
            PushTx(forkHash.ToString(), tx);
        }
    }
    else
    {
        // Dispatch Tx to child fork node
        PushTx(forkHash.ToString(),tx);
    }
}

void CDbpService::HandleAddedSysCmd(const CMvDbpSysCmd& cmd)
{
    uint256 forkHash(std::vector<unsigned char>(cmd.fork.begin(),cmd.fork.end()));

    if(IsMyFork(forkHash))
    {
        // RESERVED sys-cmd nothing to do
        print_syscmd(cmd);
    }
    else
    {
        // Dispatch SysCmd to child fork node
        PushSysCmd(forkHash.ToString(),cmd);
    }
}

void CDbpService::HandleAddedBlockCmd(const CMvDbpBlockCmd& cmd)
{
    uint256 forkHash(std::vector<unsigned char>(cmd.fork.begin(),cmd.fork.end()));
    uint256 hash(std::vector<unsigned char>(cmd.hash.begin(), cmd.hash.end()));

    if(IsMyFork(forkHash))
    {
        print_blockcmd(cmd);
     
        CBlockEx block;
        uint256 tempForkHash;
        int nHeight = 0;
        if(pService->GetBlockEx(hash, block, tempForkHash, nHeight))
        {
            CMvDbpBlock dbpBlock;
            CDbpUtils::RawToDbpBlock(block, tempForkHash, nHeight, dbpBlock);
            SendBlockToParent(cmd.id, dbpBlock);
        }
        
    }
    else
    {
        // Dispatch BlockCmd to child fork node
        PushBlockCmd(forkHash.ToString(),cmd);
    }
}

void CDbpService::HandleAddedTxCmd(const CMvDbpTxCmd& cmd)
{
    uint256 forkHash(std::vector<unsigned char>(cmd.fork.begin(),cmd.fork.end()));
    uint256 hash(std::vector<unsigned char>(cmd.hash.begin(),cmd.hash.end()));

    if(IsMyFork(forkHash))
    {
        print_txcmd(cmd);

        CTransaction tx;
        uint256 tempForkHash;
        int blockHeight;
        if (pService->GetTransaction(hash, tx, tempForkHash, blockHeight))
        {
            CMvDbpTransaction dbpTx;
            CDbpUtils::RawToDbpTransaction(tx, tempForkHash, 0, dbpTx);
            SendTxToParent(cmd.id, dbpTx);
        }
    }
    else
    {
        // Dispatch TxCmd to child fork node
        PushTxCmd(forkHash.ToString(),cmd);
    }
}

bool CDbpService::HandleEvent(CMvEventDbpAdded& event)
{
    if(event.data.name == ALL_BLOCK_TOPIC)
    {
        CMvDbpBlock block = boost::any_cast<CMvDbpBlock>(event.data.anyAddedObj);
        HandleAddedBlock(block);
    }
    else if(event.data.name == ALL_TX_TOPIC)
    {
        CMvDbpTransaction tx = boost::any_cast<CMvDbpTransaction>(event.data.anyAddedObj);
        HandleAddedTx(tx);
    }
    else if(event.data.name == SYS_CMD_TOPIC)
    {
        CMvDbpSysCmd cmd = boost::any_cast<CMvDbpSysCmd>(event.data.anyAddedObj);
        HandleAddedSysCmd(cmd);
    }
    else if(event.data.name == BLOCK_CMD_TOPIC)
    {
        CMvDbpBlockCmd cmd = boost::any_cast<CMvDbpBlockCmd>(event.data.anyAddedObj);
        HandleAddedBlockCmd(cmd);
    }
    else if(event.data.name == TX_CMD_TOPIC)
    {
        CMvDbpTxCmd cmd = boost::any_cast<CMvDbpTxCmd>(event.data.anyAddedObj);
        HandleAddedTxCmd(cmd);
    }
    else
    {
        return false;
    }
    
    return true;
}

bool CDbpService::HandleEvent(CMvEventDbpConnect& event)
{
    bool isReconnect = event.data.isReconnect;
    
    if (isReconnect)
    {
        UpdateChildNodeForks(event.strSessionId,event.data.forks);
        RespondConnected(event);
    }
    else
    {
        if (event.data.version != 1)
        {
            RespondFailed(event);
        }
        else
        {
            UpdateChildNodeForks(event.strSessionId,event.data.forks);
            RespondConnected(event);
        }
    }

    return true;
}

bool CDbpService::HandleEvent(CMvEventDbpSub& event)
{
    std::string id = event.data.id;
    std::string topicName = event.data.name;

    if (!IsTopicExist(topicName))
    {
        RespondNoSub(event);
    }
    else
    {
        SubTopic(id, event.strSessionId, topicName);
        RespondReady(event);
    }

    return true;
}

bool CDbpService::HandleEvent(CMvEventDbpUnSub& event)
{
    UnSubTopic(event.data.id);
    return true;
}

void CDbpService::HandleGetTransaction(CMvEventDbpMethod& event)
{
    std::string id = event.data.id;
    std::string txid = boost::any_cast<std::string> 
     (event.data.params["hash"]);

    uint256 txHash(txid);
    CTransaction tx;
    uint256 forkHash;
    int blockHeight;

    if (pService->GetTransaction(txHash, tx, forkHash, blockHeight))
    {
        CMvDbpTransaction dbpTx;
        CDbpUtils::RawToDbpTransaction(tx, forkHash, 0, dbpTx);

        CMvEventDbpMethodResult eventResult(event.strSessionId);
        eventResult.data.id = id;
        eventResult.data.anyResultObjs.push_back(dbpTx);
        pDbpServer->DispatchEvent(&eventResult);
    }
    else
    {
        CMvEventDbpMethodResult eventResult(event.strSessionId);
        eventResult.data.id = id;
        eventResult.data.error = "404";
        pDbpServer->DispatchEvent(&eventResult);
    }
}

void CDbpService::HandleSendTransaction(CMvEventDbpMethod& event)
{
    std::string data = boost::any_cast 
        <std::string>(event.data.params["data"]);

    std::vector<unsigned char> txData(data.begin(), data.end());
    walleve::CWalleveBufStream ss;
    ss.Write((char *)&txData[0], txData.size());

    CTransaction rawTx;
    try
    {
        ss >> rawTx;
    }
    catch (const std::exception& e)
    {
        CMvEventDbpMethodResult eventResult(event.strSessionId);
        eventResult.data.id = event.data.id;
        eventResult.data.error = "400";
        pDbpServer->DispatchEvent(&eventResult);
        return;
    }

    MvErr err = pService->SendTransaction(rawTx);
    if (err == MV_OK)
    {
        CMvEventDbpMethodResult eventResult(event.strSessionId);
        eventResult.data.id = event.data.id;

        CMvDbpSendTransactionRet sendTxRet;
        sendTxRet.hash = data;
        sendTxRet.result = "succeed";
        eventResult.data.anyResultObjs.push_back(sendTxRet);

        pDbpServer->DispatchEvent(&eventResult);
    }
    else
    {
        CMvEventDbpMethodResult eventResult(event.strSessionId);
        eventResult.data.id = event.data.id;

        CMvDbpSendTransactionRet sendTxRet;
        sendTxRet.hash = data;
        sendTxRet.result = "failed";
        sendTxRet.reason = std::string(MvErrString(err));
        eventResult.data.anyResultObjs.push_back(sendTxRet);

        pDbpServer->DispatchEvent(&eventResult);
    }
}

bool CDbpService::IsTopicExist(const std::string& topic)
{
    return mapTopicIds.find(topic) != mapTopicIds.end();
}

void CDbpService::SubTopic(const std::string& id, const std::string& session, const std::string& topic)
{
    mapTopicIds[topic].insert(id);
    mapIdSubedSession.insert(std::make_pair(id, session));
}

void CDbpService::UnSubTopic(const std::string& id)
{
    for(auto& kv : mapTopicIds)
    {
        kv.second.erase(id);
    }
    mapIdSubedSession.erase(id);
}

void CDbpService::RemoveSession(const std::string& session)
{
    std::vector<std::string> vBeDeletedIds;
    for(const auto& kv : mapIdSubedSession)
    {
        std::string id = kv.first;
        std::string valueSession = kv.second;
        if(valueSession == session)
        {
            vBeDeletedIds.push_back(id);
        }
    }

    for(const auto& id : vBeDeletedIds)
    {
        UnSubTopic(id);
    }
}

bool CDbpService::IsEmpty(const uint256& hash)
{
    static const uint256 EMPTY_HASH;
    return hash == EMPTY_HASH;
}

bool CDbpService::IsForkHash(const uint256& hash)
{
    std::vector<std::pair<uint256,CProfile>> forks;
    pService->ListFork(forks);

    for(const auto& fork : forks)
    {
        if(fork.first == hash)
        {
            return true;
        }
    }

    return false;
}

CDbpService::ForkTopology CDbpService::CalcForkToplogy(const uint256& forkHash)
{
    const uint256 startForkHash = forkHash;
    uint256 tempForkHash = forkHash;
    uint256 parentForkHash;
    uint256 hashJoint;
    int parentJointHeight = 0;
    ForkTopology toplogy;
    while(pForkManager->GetJoint(tempForkHash,parentForkHash,hashJoint,parentJointHeight))
    {
        toplogy.push_back(std::make_tuple(parentForkHash.ToString(), hashJoint.ToString(), parentJointHeight));
        tempForkHash = parentForkHash;
    }

    return toplogy;
}

bool CDbpService::IsInMyForkPath(const uint256& forkHash, int blockHeight)
{
    for(const auto& kv : mapThisNodeForkToplogy)
    {
        const auto& vecToplogy = kv.second;
        for(const auto& tuple : vecToplogy)
        {
            std::string parentForkHash;
            std::string hashJoint;
            int parentJointHeight;
            std::tie(parentForkHash, hashJoint, parentJointHeight) = tuple;
            if(parentForkHash == forkHash.ToString() && blockHeight <= parentJointHeight)
            {
                return true;
            }
        }

    }
    
    return false;
}

bool CDbpService::IsInChildNodeForkPath(const uint256& forkHash, int blockHeight)
{
    for(const auto& kv : mapChildNodesForkToplogy)
    {
        const auto& vecToplogy = kv.second;
        for(const auto& tuple : vecToplogy)
        {
            std::string parentForkHash;
            std::string hashJoint;
            int parentJointHeight;
            std::tie(parentForkHash, hashJoint, parentJointHeight) = tuple;
            if(parentForkHash == forkHash.ToString() && blockHeight <= parentJointHeight)
            {
                return true;
            }
        }

    }
    
    return false;
}

bool CDbpService::IsBlockExist(const uint256& hash)
{
    uint256 forkHash;
    int nHeight = 0;
    return pService->GetBlockLocation(hash, forkHash, nHeight);
}

void CDbpService::GetForkState(const uint256& forkHash, int& lastHeight, uint256& lastBlockHash)
{
    lastHeight = pService->GetForkHeight(forkHash);
    if(!pService->GetBlockHash(forkHash, lastHeight,lastBlockHash))
    {
        lastHeight = 0;
        lastBlockHash = pCoreProtocol->GetGenesisBlockHash();
    }
}

void CDbpService::TrySwitchFork(const uint256& blockHash,uint256& forkHash)
{
    auto it = mapForkPoint.find(blockHash.ToString());
    if(it != mapForkPoint.end())
    {
        auto value = it->second; 
        forkHash = value.first;
    } 
}

bool CDbpService::CalcForkPoints(const uint256& forkHash)
{
    std::vector<std::pair<uint256,int>> vAncestors;
    std::vector<std::pair<int,uint256>> vSublines;
    std::vector<std::pair<uint256,uint256>> path;
    if(!pService->GetForkGenealogy(forkHash,vAncestors,vSublines))
    {
        return false;
    }

    std::vector<std::pair<uint256,uint256>> forkAncestors;
    for(int i = vAncestors.size() - 1; i >= 0; i--)
    {
        CBlock block;
        uint256 tempFork;
        int nHeight = 0;
        pService->GetBlock(vAncestors[i].first,block,tempFork,nHeight);
        forkAncestors.push_back(std::make_pair(vAncestors[i].first,block.hashPrev));
    }

    path = forkAncestors;
    CBlock block;
    uint256 tempFork;
    int nHeight = 0;
    pService->GetBlock(forkHash,block,tempFork,nHeight);
    path.push_back(std::make_pair(forkHash,block.hashPrev));

    for(const auto& fork : path)
    {
        mapForkPoint.insert(std::make_pair(fork.second.ToString(), 
            std::make_pair(fork.first,fork.second)));
    }
    
    return true;
}

bool CDbpService::GetLwsBlocks(const uint256& forkHash, const uint256& startHash, int32 n, std::vector<CMvDbpBlock>& blocks)
{
    uint256 connectForkHash = forkHash;
    uint256 blockHash = startHash;

    if (IsEmpty(connectForkHash))
    {
        connectForkHash = pCoreProtocol->GetGenesisBlockHash();
    }

    if(!IsForkHash(connectForkHash))
    {
        std::cerr << "connect fork hash is not a fork hash.\n";
        return false;
    }

    if (IsEmpty(blockHash))
    {
        blockHash = pCoreProtocol->GetGenesisBlockHash();
    }

    int blockHeight = 0;
    uint256 tempForkHash;
    if (!pService->GetBlockLocation(blockHash, tempForkHash, blockHeight))
    {
        std::cerr << "GetBlockLocation failed\n";
        return false;
    }

    if(!CalcForkPoints(connectForkHash))
    {
        std::cerr << "CalcForkPoint failed.\n";
        return false;
    }

    const std::size_t nonExtendBlockMaxNum = n;
    std::size_t nonExtendBlockCount = 0;
    
    pService->GetBlockLocation(blockHash, tempForkHash, blockHeight);
    
    std::vector<uint256> blocksHash;
    while (nonExtendBlockCount < nonExtendBlockMaxNum && 
            pService->GetBlockHash(tempForkHash, blockHeight, blocksHash))
    {  
        for(int i = 0; i < blocksHash.size(); ++i)
        {
            CBlockEx block;
            int height;
            pService->GetBlockEx(blocksHash[i], block, tempForkHash, height);
            if (block.nType != CBlock::BLOCK_EXTENDED)
            {
                nonExtendBlockCount++;
            }

            CMvDbpBlock DbpBlock;
            CDbpUtils::RawToDbpBlock(block, tempForkHash, height, DbpBlock);
            blocks.push_back(DbpBlock);
        }
        
        TrySwitchFork(blocksHash[0],tempForkHash);
        blockHeight++;
        blocksHash.clear(); blocksHash.shrink_to_fit();
       
    }

    return true;
}

bool CDbpService::GetSnBlocks(const uint256& forkHash, const uint256& startHash, int32 n, std::vector<CMvDbpBlock>& blocks)
{
    uint256 blockHash = startHash;

    if(!IsForkHash(forkHash))
    {
        std::cerr << "invalid fork hash.\n";
        return false;
    }

    if (IsEmpty(blockHash))
    {
        std::cerr << "start hash is invalid.\n";
        return false;
    }

    int blockHeight = 0;
    uint256 tempForkHash;
    if (!pService->GetBlockLocation(blockHash, tempForkHash, blockHeight))
    {
        return false;
    }

    const std::size_t nonExtendBlockMaxNum = n;
    std::size_t nonExtendBlockCount = 0;
    
    pService->GetBlockLocation(blockHash, tempForkHash, blockHeight);
    
    std::vector<uint256> blocksHash;
    while (nonExtendBlockCount < nonExtendBlockMaxNum && 
            pService->GetBlockHash(tempForkHash, blockHeight, blocksHash))
    {
        for(int i = 0; i < blocksHash.size(); ++i)
        {
            CBlockEx block;
            int height;
            pService->GetBlockEx(blocksHash[i], block, tempForkHash, height);
            if (block.nType != CBlock::BLOCK_EXTENDED)
            {
                nonExtendBlockCount++;
            }

            CMvDbpBlock DbpBlock;
            CDbpUtils::RawToDbpBlock(block, tempForkHash, height, DbpBlock);
            blocks.push_back(DbpBlock);
        }
        
        blockHeight++;
        blocksHash.clear(); blocksHash.shrink_to_fit();
    }

    return true;

}

void CDbpService::HandleGetBlocks(CMvEventDbpMethod& event)
{
    std::string forkid = boost::any_cast<std::string>(event.data.params["forkid"]);
    std::string blockHash = boost::any_cast<std::string>(event.data.params["hash"]);
    std::string num = boost::any_cast<std::string>(event.data.params["number"]);
    int32 blockNum = boost::lexical_cast<int32>(num);
    
    uint256 startBlockHash(std::vector<unsigned char>(blockHash.begin(), blockHash.end()));
    uint256 forkHash;
    forkHash.SetHex(forkid);
    std::vector<CMvDbpBlock> blocks;
    if (GetLwsBlocks(forkHash, startBlockHash, blockNum, blocks))
    {
        CMvEventDbpMethodResult eventResult(event.strSessionId);
        eventResult.data.id = event.data.id;

        for (auto& block : blocks)
        {
            eventResult.data.anyResultObjs.push_back(block);
        }

        pDbpServer->DispatchEvent(&eventResult);
    }
    else
    {
        CMvEventDbpMethodResult eventResult(event.strSessionId);
        eventResult.data.id = event.data.id;
        eventResult.data.error = "400";
        pDbpServer->DispatchEvent(&eventResult);
    }
}

bool CDbpService::IsForkNode()
{
    return fIsForkNode;
}

bool CDbpService::IsMainFork(const uint256& hash)
{
    return pCoreProtocol->GetGenesisBlockHash().ToString() == hash.ToString();
}

bool CDbpService::IsMyFork(const uint256& hash)
{
    return mapThisNodeForkStates.find(hash.ToString()) != mapThisNodeForkStates.end();
}

bool CDbpService::IsChildNodeFork(const uint256& hash)
{
    return mapChildNodeForksStates.find(hash.ToString()) != mapChildNodeForksStates.end();
}

bool CDbpService::HandleEvent(CMvEventDbpRegisterForkID& event)
{
    std::string& forkid = event.data.forkid;
    if(!forkid.empty())
    {
        mapThisNodeForkStates[forkid] = std::make_tuple(-1,"");
    }
    else
    {
        pNetChannel->SetForkFilterInfo(IsForkNode(), mapThisNodeForkStates);
        
        UpdateChildNodeForksToParent();
    }
    
    return true;
}

void CDbpService::SetIsForkNode(bool isForkNode)
{
    int nNonce = 0;
    uint32_t ramdom32 = CDbpUtils::RandomBits();
    std::memcpy(&nNonce, &ramdom32, 4);
    CFkEventNodeIsForkNode eventIsForkNode(nNonce);
    eventIsForkNode.fIsForkNode = isForkNode;
    pVirtualPeerNet->DispatchEvent(&eventIsForkNode);

    pNetChannel->SetForkFilterInfo(isForkNode, mapThisNodeForkStates);

    CMvEventDbpIsForkNode eventDbpIsForkNode("");
    eventDbpIsForkNode.data.IsForkNode = isForkNode;
    pDbpClient->DispatchEvent(&eventDbpIsForkNode);
}

bool CDbpService::HandleEvent(CMvEventDbpUpdateForkState& event)
{
    std::string& forkid = event.data.forkid;
    if(!forkid.empty())
    {
        uint256 forkHash, lastBlockHash;
        forkHash.SetHex(forkid);
        UpdateThisNodeForkState(forkHash);
    }
    else
    {
        uint256 mainForkHash = pCoreProtocol->GetGenesisBlockHash();
        UpdateThisNodeForkState(mainForkHash);
        UpdateChildNodeForksStatesToParent();
    }
    
    return true;
}

void CDbpService::HandleRegisterFork(CMvEventDbpMethod& event)
{
    std::string forkid = boost::any_cast<std::string>(event.data.params["forkid"]);
    UpdateChildNodeForks(event.strSessionId,forkid);

    uint256 forkHashRet;
    forkHashRet.SetHex(forkid);
    std::vector<uint8> forkHashBin;
    walleve::CWalleveODataStream forkHashSs(forkHashBin);
    forkHashRet.ToDataStream(forkHashSs);
    
    CMvEventDbpMethodResult eventResult(event.strSessionId);
    eventResult.data.id = event.data.id;
    CMvDbpRegisterForkIDRet ret;
    ret.forkid = std::string(forkHashBin.begin(), forkHashBin.end());
    eventResult.data.anyResultObjs.push_back(ret);
    pDbpServer->DispatchEvent(&eventResult);

    if(IsForkNode())
    {
        UpdateChildNodeForksToParent();
    }
}

void CDbpService::HandleSendBlock(CMvEventDbpMethod& event)
{
    std::string id = boost::any_cast<std::string>(event.data.params["id"]);
    CMvDbpBlock block = boost::any_cast<CMvDbpBlock>(event.data.params["data"]);
    
    CMvEventDbpMethodResult eventResult(event.strSessionId);
    eventResult.data.id = event.data.id;
    CMvDbpSendBlockRet ret;
    ret.hash = std::string(block.hash.begin(), block.hash.end()); 
    eventResult.data.anyResultObjs.push_back(ret);
    pDbpServer->DispatchEvent(&eventResult);

    if(!IsForkNode())
    {
        int nNonce = 0;
        uint32_t ramdom32 = CDbpUtils::RandomBits();
        std::memcpy(&nNonce, &ramdom32, 4);
        uint256 forkHash(block.fork);
        CFkEventNodeSendBlock eventBlock(nNonce, forkHash);
        CDbpUtils::DbpToRawBlock(block, eventBlock.data);
        pVirtualPeerNet->DispatchEvent(&eventBlock);
    }
    else
    {
        SendBlockToParent(id, block);
    }
}

void CDbpService::HandleSendTx(CMvEventDbpMethod& event)
{
    std::string id = boost::any_cast<std::string>(event.data.params["id"]);
    CMvDbpTransaction tx = boost::any_cast<CMvDbpTransaction>(event.data.params["data"]);

    CMvEventDbpMethodResult eventResult(event.strSessionId);
    eventResult.data.id = event.data.id;
    CMvDbpSendTxRet ret;
    ret.hash = std::string(tx.hash.begin(), tx.hash.end()); 
    eventResult.data.anyResultObjs.push_back(ret);
    pDbpServer->DispatchEvent(&eventResult);

    if(!IsForkNode())
    {
        int nNonce = 0;
        uint32_t ramdom32 = CDbpUtils::RandomBits();
        std::memcpy(&nNonce, &ramdom32, 4);
        uint256 forkHash(tx.fork);
        CFkEventNodeSendTx eventTx(nNonce, forkHash);
        CDbpUtils::DbpToRawTransaction(tx,eventTx.data);
        pVirtualPeerNet->DispatchEvent(&eventTx);
    }
    else
    { 
        SendTxToParent(id, tx);
    }
    
}

void CDbpService::HandleSendBlockNotice(CMvEventDbpMethod& event)
{
    std::string forkid = boost::any_cast<std::string>(event.data.params["forkid"]);
    std::string height = boost::any_cast<std::string>(event.data.params["height"]);
    std::string hash = boost::any_cast<std::string>(event.data.params["hash"]);
    
    CMvEventDbpMethodResult eventResult(event.strSessionId);
    eventResult.data.id = event.data.id;
    CMvDbpSendBlockNoticeRet ret;
    ret.hash = hash;
    eventResult.data.anyResultObjs.push_back(ret);
    pDbpServer->DispatchEvent(&eventResult);

    if(!IsForkNode())
    {
        int nNonce = 0;
        uint32_t ramdom32 = CDbpUtils::RandomBits();
        std::memcpy(&nNonce, &ramdom32, 4);
        uint256 forkHash(std::vector<unsigned char>(forkid.begin(), forkid.end()));
        uint256 blockHash(std::vector<unsigned char>(hash.begin(), hash.end()));
        CFkEventNodeSendBlockNotice eventBlockNotice(nNonce, forkHash);
        eventBlockNotice.hashFork = forkHash;
        eventBlockNotice.hashBlock = blockHash;
        pVirtualPeerNet->DispatchEvent(&eventBlockNotice);
    }
    else
    {
        uint256 forkid256(std::vector<unsigned char>(forkid.begin(), forkid.end()));
        uint256 hash256(std::vector<unsigned char>(hash.begin(), hash.end()));
        SendBlockNoticeToParent(forkid256.ToString(),height,hash256.ToString());
    }
}

void CDbpService::HandleSendTxNotice(CMvEventDbpMethod& event)
{
    std::string forkid = boost::any_cast<std::string>(event.data.params["forkid"]);
    std::string hash = boost::any_cast<std::string>(event.data.params["hash"]);

    CMvEventDbpMethodResult eventResult(event.strSessionId);
    eventResult.data.id = event.data.id;
    CMvDbpSendTxNoticeRet ret;
    ret.hash = hash;
    eventResult.data.anyResultObjs.push_back(ret);
    pDbpServer->DispatchEvent(&eventResult);

    if(!IsForkNode())
    {
        int nNonce = 0;
        uint32_t ramdom32 = CDbpUtils::RandomBits();
        std::memcpy(&nNonce, &ramdom32, 4);
        uint256 forkHash(std::vector<unsigned char>(forkid.begin(), forkid.end()));
        uint256 txHash(std::vector<unsigned char>(hash.begin(), hash.end()));
        CFkEventNodeSendTxNotice eventTxNotice(nNonce, forkHash);
        eventTxNotice.hashFork = forkHash;
        eventTxNotice.hashTx = txHash;
        pVirtualPeerNet->DispatchEvent(&eventTxNotice);
    }
    else
    {
        uint256 forkid256(std::vector<unsigned char>(forkid.begin(), forkid.end()));
        uint256 hash256(std::vector<unsigned char>(hash.begin(), hash.end()));
        SendTxNoticeToParent(forkid256.ToString(), hash256.ToString());
    }
}

void CDbpService::HandleGetSNBlocks(CMvEventDbpMethod& event)
{
    std::string forkid = boost::any_cast<std::string>(event.data.params["forkid"]);
    std::string hash = boost::any_cast<std::string>(event.data.params["hash"]);
    std::string number = boost::any_cast<std::string>(event.data.params["number"]);
    int32 blockNum = boost::lexical_cast<int32>(number);

    CMvEventDbpMethodResult eventResult(event.strSessionId);
    eventResult.data.id = event.data.id;
    CMvDbpGetBlocksRet ret;
    ret.hash = hash;
    eventResult.data.anyResultObjs.push_back(ret);
    pDbpServer->DispatchEvent(&eventResult);

    if(IsForkNode())
    {
        GetBlocksToParent(forkid,hash,blockNum);
    }
}

void CDbpService::HandleUpdateForkState(CMvEventDbpMethod& event)
{
    std::string forkid = boost::any_cast<std::string>(event.data.params["forkid"]);
    std::string lastBlockHash = boost::any_cast<std::string>(event.data.params["lastblockhash"]);
    std::string currentHeight = boost::any_cast<std::string>(event.data.params["currentheight"]);
    int nCurrentHeight = boost::lexical_cast<int>(currentHeight);

    CMvEventDbpMethodResult eventResult(event.strSessionId);
    eventResult.data.id = event.data.id;
    CMvDbpUpdateForkStateRet ret;
    ret.forkid = forkid;
    eventResult.data.anyResultObjs.push_back(ret);
    pDbpServer->DispatchEvent(&eventResult);

    uint256 forkHash(std::vector<unsigned char>(forkid.begin(), forkid.end())); 
    uint256 blockHash(std::vector<unsigned char>(lastBlockHash.begin(), lastBlockHash.end()));

    UpdateChildNodeForksStates(forkHash.ToString(), nCurrentHeight, lastBlockHash);
    
    if(IsMainFork(forkHash))
    {
        if(nCurrentHeight < pService->GetForkHeight(forkHash))
        {
            int num = pService->GetForkHeight(forkHash) - nCurrentHeight;
            std::vector<CMvDbpBlock> blocks;
            if(GetSnBlocks(forkHash, blockHash, num + 1, blocks))
            {
                for(const auto& block : blocks)
                {
                    PushBlock(forkHash.ToString(), block);
                }
            }
            else
            {
                UpdateChildNodeForksStatesToParent();      
            }
        }
        else
        {
            UpdateChildNodeForksStatesToParent();    
        }
    }
    else
    {
        if(!IsForkNode())
        {
            int nNonce = 0;
            uint32_t ramdom32 = CDbpUtils::RandomBits();
            std::memcpy(&nNonce, &ramdom32, 4);
            CFkEventNodeUpdateForkState eventForkState(nNonce, forkHash);
            eventForkState.hashFork = forkHash;
            eventForkState.hashBlock = blockHash;
            eventForkState.height = nCurrentHeight;
            pVirtualPeerNet->DispatchEvent(&eventForkState);
        }
        else
        {
            UpdateChildNodeForksStatesToParent();
        }
    }
}

bool CDbpService::HandleEvent(CMvEventDbpMethod& event)
{
    if (event.data.method == CMvDbpMethod::LwsMethod::GET_BLOCKS)
    {
        HandleGetBlocks(event);
    }
    else if (event.data.method == CMvDbpMethod::LwsMethod::GET_TRANSACTION)
    {
        HandleGetTransaction(event);
    }
    else if (event.data.method == CMvDbpMethod::LwsMethod::SEND_TRANSACTION)
    {
        HandleSendTransaction(event);
    }
    else if(event.data.method == CMvDbpMethod::SNMethod::REGISTER_FORK)
    {
        HandleRegisterFork(event);
    }
    else if(event.data.method == CMvDbpMethod::SNMethod::SEND_BLOCK)
    {
        HandleSendBlock(event);
    }
    else if(event.data.method == CMvDbpMethod::SNMethod::SEND_TX)
    {
        HandleSendTx(event);
    }
    else if(event.data.method == CMvDbpMethod::SNMethod::SEND_BLOCK_NOTICE)
    {
        HandleSendBlockNotice(event);
    }
    else if(event.data.method == CMvDbpMethod::SNMethod::SEND_TX_NOTICE)
    {
        HandleSendTxNotice(event);
    }
    else if(event.data.method == CMvDbpMethod::SNMethod::GET_BLOCKS_SN)
    {
        HandleGetSNBlocks(event);
    }
    else if(event.data.method == CMvDbpMethod::SNMethod::UPDATE_FORK_STATE)
    {
        HandleUpdateForkState(event);
    }
    else
    {
        return false;
    }

    return true;
}

void CDbpService::PushBlock(const std::string& forkid, const CMvDbpBlock& block)
{
    const auto& allBlockIds = mapTopicIds[ALL_BLOCK_TOPIC];   
    for (const auto& kv : mapIdSubedSession)
    {
        std::string id = kv.first;
        std::string session = kv.second;

        if (allBlockIds.find(id) != allBlockIds.end())
        {
            CMvEventDbpAdded eventAdded(session);
            eventAdded.data.id = id;
            eventAdded.data.forkid = forkid;
            eventAdded.data.name = ALL_BLOCK_TOPIC;
            eventAdded.data.anyAddedObj = block;
            pDbpServer->DispatchEvent(&eventAdded);
        }
    }
}

void CDbpService::PushTx(const std::string& forkid, const CMvDbpTransaction& dbptx)
{
    const auto& allTxIds = mapTopicIds[ALL_TX_TOPIC];
    for (const auto& kv : mapIdSubedSession)
    {
        std::string id = kv.first;
        std::string session = kv.second;

        if (allTxIds.find(id) != allTxIds.end())
        {
            CMvEventDbpAdded eventAdded(session);
            eventAdded.data.id = id;
            eventAdded.data.forkid = forkid;
            eventAdded.data.name = ALL_TX_TOPIC;
            eventAdded.data.anyAddedObj = dbptx;
            pDbpServer->DispatchEvent(&eventAdded);
        }
    }
}

void CDbpService::PushSysCmd(const std::string& forkid, const CMvDbpSysCmd& syscmd)
{
    const auto& sysCmdIds = mapTopicIds[SYS_CMD_TOPIC];
    for (const auto& kv : mapIdSubedSession)
    {
        std::string id = kv.first;
        std::string session = kv.second;

        if (sysCmdIds.find(id) != sysCmdIds.end())
        {
            CMvEventDbpAdded eventAdded(session);
            eventAdded.data.id = id;
            eventAdded.data.forkid = forkid;
            eventAdded.data.name = SYS_CMD_TOPIC;
            eventAdded.data.anyAddedObj = syscmd;
            pDbpServer->DispatchEvent(&eventAdded);
        }
    }
}

void CDbpService::PushTxCmd(const std::string& forkid, const CMvDbpTxCmd& txcmd)
{
    const auto& txCmdIds = mapTopicIds[TX_CMD_TOPIC];
    for (const auto& kv : mapIdSubedSession)
    {
        std::string id = kv.first;
        std::string session = kv.second;

        if (txCmdIds.find(id) != txCmdIds.end())
        {
            CMvEventDbpAdded eventAdded(session);
            eventAdded.data.id = id;
            eventAdded.data.forkid = forkid;
            eventAdded.data.name = TX_CMD_TOPIC;
            eventAdded.data.anyAddedObj = txcmd;
            pDbpServer->DispatchEvent(&eventAdded);
        }
    }
}

void CDbpService::PushBlockCmd(const std::string& forkid, const CMvDbpBlockCmd& blockcmd)
{
    const auto& blockCmdIds = mapTopicIds[BLOCK_CMD_TOPIC];
    for (const auto& kv : mapIdSubedSession)
    {
        std::string id = kv.first;
        std::string session = kv.second;

        if (blockCmdIds.find(id) != blockCmdIds.end())
        {
            CMvEventDbpAdded eventAdded(session);
            eventAdded.data.id = id;
            eventAdded.data.forkid = forkid;
            eventAdded.data.name = BLOCK_CMD_TOPIC;
            eventAdded.data.anyAddedObj = blockcmd;
            pDbpServer->DispatchEvent(&eventAdded);
        }
    }
}

void CDbpService::RespondFailed(CMvEventDbpConnect& event)
{
    std::vector<int> versions{1};
    CMvEventDbpFailed eventFailed(event.strSessionId);
    eventFailed.data.reason = "001";
    eventFailed.data.versions = versions;
    eventFailed.data.session = event.data.session;
    pDbpServer->DispatchEvent(&eventFailed);
}

void CDbpService::RespondConnected(CMvEventDbpConnect& event)
{
    CMvEventDbpConnected eventConnected(event.strSessionId);
    eventConnected.data.session = event.data.session;
    pDbpServer->DispatchEvent(&eventConnected);
}

void CDbpService::RespondNoSub(CMvEventDbpSub& event)
{
    CMvEventDbpNoSub eventNoSub(event.strSessionId);
    eventNoSub.data.id = event.data.id;
    pDbpServer->DispatchEvent(&eventNoSub);
}

void CDbpService::RespondReady(CMvEventDbpSub& event)
{
    CMvEventDbpReady eventReady(event.strSessionId);
    eventReady.data.id = event.data.id;
    pDbpServer->DispatchEvent(&eventReady);
}

void CDbpService::UpdateChildNodeForks(const std::string& session, const std::string& forks)
{
    std::vector<std::string> vForks = CDbpUtils::Split(forks,';');
    ForksType setForks;
    for(const auto& fork : vForks)
    {
        setForks.insert(fork);
    }
    
    if(mapSessionChildNodeForks.count(session) == 0)
    {
        mapSessionChildNodeForks[session] = setForks;
    }
    else
    {
        auto& forks = mapSessionChildNodeForks[session];
        forks.insert(setForks.begin(),setForks.end());
    }
}

void CDbpService::UpdateChildNodeForksStates(const std::string& forkid, int currentHeight, 
    const std::string& lastBlockHash)
{
    mapChildNodeForksStates[forkid] = std::make_tuple(currentHeight, lastBlockHash);
}

void CDbpService::UpdateThisNodeForkState(const uint256& forkHash)
{
    int lastHeight;
    uint256 lastBlockHash;
    GetForkState(forkHash, lastHeight, lastBlockHash);
    if(IsMainFork(forkHash))
    {
        tupleMainForkStates = std::make_tuple(lastHeight, lastBlockHash.ToString());
    }
    else
    {
        mapThisNodeForkStates[forkHash.ToString()] =
            std::make_tuple(lastHeight, lastBlockHash.ToString());
    }
}

void CDbpService::UpdateChildNodeForksToParent()
{
    for(const auto& kv : mapSessionChildNodeForks)
    {
        std::string session = kv.first;
        auto forks = kv.second;
        for(const std::string& forkid : forks)
        {
            CMvEventDbpRegisterForkID eventRegister("");
            eventRegister.data.forkid = forkid;
            pDbpClient->DispatchEvent(&eventRegister);
        }
    }

    for(const auto& kv : mapThisNodeForkStates)
    {
        std::string forkid = kv.first;
        CMvEventDbpRegisterForkID eventRegister("");
        eventRegister.data.forkid = forkid;
        pDbpClient->DispatchEvent(&eventRegister);
    }
}

void CDbpService::UpdateChildNodeForksStatesToParent()
{
    for(const auto& kv : mapThisNodeForkStates)
    {
        std::string forkid = kv.first;
        int currentHeight;
        std::string lastBlockHash;
        const auto& tupleStates = kv.second;
        std::tie(currentHeight, lastBlockHash) = tupleStates;
        CMvEventDbpUpdateForkState eventUpdate("");
        eventUpdate.data.forkid = forkid;
        eventUpdate.data.currentHeight = std::to_string(currentHeight);
        eventUpdate.data.lastBlockHash = lastBlockHash;
        pDbpClient->DispatchEvent(&eventUpdate);
    }

    for(const auto& kv : mapChildNodeForksStates)
    {
        std::string forkid = kv.first;
        int currentHeight;
        std::string lastBlockHash;
        const auto& tupleStates = kv.second;
        std::tie(currentHeight, lastBlockHash) = tupleStates;
        CMvEventDbpUpdateForkState eventUpdate("");
        eventUpdate.data.forkid = forkid;
        eventUpdate.data.currentHeight = std::to_string(currentHeight);
        eventUpdate.data.lastBlockHash = lastBlockHash;
        pDbpClient->DispatchEvent(&eventUpdate);
    }

}

void CDbpService::SendBlockToParent(const std::string& id, const CMvDbpBlock& block)
{
    CMvEventDbpSendBlock eventSendBlock("");
    eventSendBlock.data.id = id;
    eventSendBlock.data.block = block;
    pDbpClient->DispatchEvent(&eventSendBlock);
}

void CDbpService::SendTxToParent(const std::string& id, const CMvDbpTransaction& tx)
{
    CMvEventDbpSendTx eventSendTx("");
    eventSendTx.data.id = id;
    eventSendTx.data.tx = tx;
    pDbpClient->DispatchEvent(&eventSendTx);
}

void CDbpService::SendBlockNoticeToParent(const std::string& forkid, const std::string& height, const std::string& hash)
{ 
    CMvEventDbpSendBlockNotice eventSendBlockNotice("");
    eventSendBlockNotice.data.forkid = forkid;
    eventSendBlockNotice.data.height = height;
    eventSendBlockNotice.data.hash = hash;
    pDbpClient->DispatchEvent(&eventSendBlockNotice);
}

void CDbpService::SendTxNoticeToParent(const std::string& forkid, const std::string& hash)
{
    CMvEventDbpSendTxNotice eventSendTxNotice("");
    eventSendTxNotice.data.forkid = forkid;
    eventSendTxNotice.data.hash = hash;
    pDbpClient->DispatchEvent(&eventSendTxNotice);
}

void CDbpService::GetBlocksToParent(const std::string& forkid, const std::string& hash, int32 num)
{
    uint256 forkid256(std::vector<uint8>(forkid.begin(), forkid.end()));
    uint256 hash256(std::vector<uint8>(hash.begin(), hash.end()));
    
    CMvEventDbpGetBlocks eventGetBlocks("");
    eventGetBlocks.data.forkid = forkid256.ToString();
    eventGetBlocks.data.hash = hash256.ToString();
    eventGetBlocks.data.number = num;
    pDbpClient->DispatchEvent(&eventGetBlocks);
}


bool CDbpService::HandleEvent(CMvEventDbpUpdateNewBlock& event)
{
    // get details about new block
    CBlockEx& newBlock = event.data;
    uint256 forkHash;
    int blockHeight = 0;
    if (pService->GetBlockLocation(newBlock.GetHash(),forkHash,blockHeight))
    {
        CMvDbpBlock block;
        CDbpUtils::RawToDbpBlock(newBlock, forkHash, blockHeight, block);
        PushBlock(forkHash.ToString(),block);
        SendBlockNoticeToParent(forkHash.ToString(), std::to_string(blockHeight), 
            newBlock.GetHash().ToString());
         

        UpdateThisNodeForkState(forkHash);
        UpdateChildNodeForksStatesToParent();
    }

    return true;
}

bool CDbpService::HandleEvent(CMvEventDbpUpdateNewTx& event)
{
    decltype(event.data)& newtx = event.data;
    uint256& hashFork = event.hashFork;
    int64& change = event.nChange;

    CMvDbpTransaction dbpTx;
    CDbpUtils::RawToDbpTransaction(newtx, hashFork, change, dbpTx);
    PushTx(hashFork.ToString(),dbpTx);
    SendTxNoticeToParent(hashFork.ToString(), newtx.GetHash().ToString());

    return true;
}

bool CDbpService::HandleEvent(CFkEventNodeBlockArrive& event)
{
    if(!IsForkNode())
    {
        CMvDbpBlock block;
        CBlockEx blockEx(event.data);
        CDbpUtils::RawToDbpBlock(blockEx, event.hashFork, event.height, block);
        PushBlock(event.hashFork.ToString(), block);
    }

    return true;
}

bool CDbpService::HandleEvent(CFkEventNodeTxArrive& event)
{
    if(!IsForkNode())
    {
        CMvDbpTransaction tx;
        CDbpUtils::RawToDbpTransaction(event.data, event.hashFork, 0, tx);
        PushTx(event.hashFork.ToString(), tx);
    }
    
    return true;
}
