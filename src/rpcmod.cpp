// Copyright (c) 2017-2018 The Multiverse developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpcmod.h"
#include "address.h"
#include "template.h"
#include "version.h"
#include <boost/assign/list_of.hpp>
using namespace std;
using namespace multiverse;
using namespace walleve;
using namespace json_spirit;

///////////////////////////////
// CRPCMod

CRPCMod::CRPCMod()
: IIOModule("rpcmod")
{
    pHttpServer = NULL;
    pCoreProtocol = NULL;
    pService = NULL;
    mapRPCFunc = boost::assign::map_list_of
                 ("help",                  &CRPCMod::RPCHelp)
                 ("stop",                  &CRPCMod::RPCStop)
                 ("getpeercount",          &CRPCMod::RPCGetPeerCount)
                 ("listpeer",              &CRPCMod::RPCListPeer)
                 ("addnode",               &CRPCMod::RPCAddNode)
                 ("remvenode",             &CRPCMod::RPCRemoveNode)
                 ("getforkcount",          &CRPCMod::RPCGetForkCount)
                 ("getgenealogy",          &CRPCMod::RPCGetForkGenealogy)
                 ("getlocation",           &CRPCMod::RPCGetBlockLocation)
                 ("getblockcount",         &CRPCMod::RPCGetBlockCount)
                 ("getblockhash",          &CRPCMod::RPCGetBlockHash)
                 ("getblock",              &CRPCMod::RPCGetBlock)
                 ("gettxpool",             &CRPCMod::RPCGetTxPool)
                 ("removependingtx",       &CRPCMod::RPCRemovePendingTx)
                 ("gettransaction",        &CRPCMod::RPCGetTransaction)
                 ("sendtransaction",       &CRPCMod::RPCSendTransaction)
                 ("listkey",               &CRPCMod::RPCListKey)
                 ("getnewkey",             &CRPCMod::RPCGetNewKey)
                 ("encryptkey",            &CRPCMod::RPCEncryptKey)
                 ("lockkey",               &CRPCMod::RPCLockKey)
                 ("unlockkey",             &CRPCMod::RPCUnlockKey)
                 ("importprivkey",         &CRPCMod::RPCImportPrivKey)
                 ("importkey",             &CRPCMod::RPCImportKey)
                 ("exportkey",             &CRPCMod::RPCExportKey)
                 ("addnewtemplate",        &CRPCMod::RPCAddNewTemplate)
                 ("importtemplate",        &CRPCMod::RPCImportTemplate)
                 ("exporttemplate",        &CRPCMod::RPCExportTemplate)
                 ("validateaddress",       &CRPCMod::RPCValidateAddress)
                 ("resyncwallet",          &CRPCMod::RPCResyncWallet)
                 ("getbalance",            &CRPCMod::RPCGetBalance)
                 ("listtransaction",       &CRPCMod::RPCListTransaction)
                 ("sendfrom",              &CRPCMod::RPCSendFrom)
                 ("createtransaction",     &CRPCMod::RPCCreateTransaction)
                 ("signtransaction",       &CRPCMod::RPCSignTransaction)
                 ("signmessage",           &CRPCMod::RPCSignMessage)
                 ("verifymessage",         &CRPCMod::RPCVerifyMessage)
                 ("makekeypair",           &CRPCMod::RPCMakeKeyPair)
                 ("getpubkeyaddress",      &CRPCMod::RPCGetPubKeyAddress)
                 ("gettemplateaddress",    &CRPCMod::RPCGetTemplateAddress)
                 ("maketemplate",          &CRPCMod::RPCMakeTemplate)
                 ("decodetransaction",     &CRPCMod::RPCDecodeTransaction)
                 ("getwork",               &CRPCMod::RPCGetWork)
                 ("submitwork",            &CRPCMod::RPCSubmitWork)
                 ;
}

CRPCMod::~CRPCMod()
{
}

bool CRPCMod::WalleveHandleInitialize()
{
    if (!WalleveGetObject("httpserver",pHttpServer))
    {
        WalleveLog("Failed to request httpserver\n");
        return false;
    }

    if (!WalleveGetObject("coreprotocol",pCoreProtocol))
    {
        WalleveLog("Failed to request coreprotocol\n");
        return false;
    }
    
    if (!WalleveGetObject("service",pService))
    {
        WalleveLog("Failed to request service\n");
        return false;
    }

    return true;
}

void CRPCMod::WalleveHandleDeinitialize()
{
    pHttpServer = NULL;
    pCoreProtocol = NULL;
    pService = NULL;
}

bool CRPCMod::HandleEvent(CWalleveEventHttpReq& eventHttpReq)
{
    uint64 nNonce = eventHttpReq.nNonce;

    Value id = Value::null;
    try
    {
        // Parse request
        Value valRequest;
        if (!read_string(eventHttpReq.data.strContent, valRequest)
            || valRequest.type() != obj_type)
        {
            throw JSONRPCError(RPC_PARSE_ERROR, "Parse error");
        }
        const Object& request = valRequest.get_obj();

        // Parse id now so errors from here on will have the id
        id = find_value(request, "id");

        // Parse method
        Value valMethod = find_value(request, "method");
        if (valMethod.type() == null_type)
        {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Missing method");
        }
        if (valMethod.type() != str_type)
        {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Method must be a string");
        }
        string strMethod = valMethod.get_str();
        WalleveLog("RPC command : %s\n", strMethod.c_str());

        // Parse params
        Value valParams = find_value(request, "params");
        Array params;
        if (valParams.type() == array_type)
        {
            params = valParams.get_array();
        }
        else if (valParams.type() == null_type)
        {
            params = Array();
        }
        else
        {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Params must be an array");
        }

        map<string,RPCFunc>::iterator it = mapRPCFunc.find(strMethod);
        if (it == mapRPCFunc.end())
        {
            throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Method not found");
        }
        
        try
        {
            JsonReply(nNonce,(this->*(*it).second)(params,false),id);
        }
        catch (exception& e)
        {
            throw JSONRPCError(RPC_MISC_ERROR, e.what());
        }
    }
    catch (Object& objError)
    {
        JsonError(nNonce, objError, id);
    }
    catch (exception& e)
    {
        JsonError(nNonce,JSONRPCError(RPC_PARSE_ERROR,e.what()),id);
    }

    return true;
}

bool CRPCMod::HandleEvent(CWalleveEventHttpBroken& eventHttpBroken)
{
    return true;
}

void CRPCMod::JsonReply(uint64 nNonce,const Value& result, const Value& id)
{
    Object reply;
    reply.push_back(Pair("result", result));
    reply.push_back(Pair("error", Value::null));
    reply.push_back(Pair("id", id));

    CWalleveEventHttpRsp eventHttpRsp(nNonce);
    eventHttpRsp.data.nStatusCode = 200;
    eventHttpRsp.data.mapHeader["content-type"] = "application/json";
    eventHttpRsp.data.mapHeader["connection"] = "Keep-Alive";
    eventHttpRsp.data.mapHeader["server"] = "multivers-rpc";
    eventHttpRsp.data.strContent = write_string(Value(reply), false) + "\n";

    pHttpServer->DispatchEvent(&eventHttpRsp);
}

void CRPCMod::JsonError(uint64 nNonce,const Object& objError, const Value& id)
{
    int nStatus = 500;
    int code = find_value(objError, "code").get_int();
    if (code == RPC_INVALID_REQUEST) nStatus = 400;
    else if (code == RPC_METHOD_NOT_FOUND) nStatus = 404;

    Object reply;
    reply.push_back(Pair("result", Value::null));
    reply.push_back(Pair("error", objError));
    reply.push_back(Pair("id", id));

    CWalleveEventHttpRsp eventHttpRsp(nNonce);
    eventHttpRsp.data.nStatusCode = nStatus;
    eventHttpRsp.data.mapHeader["content-type"] = "application/json";
    eventHttpRsp.data.mapHeader["server"] = "multiverse-rpc";
    eventHttpRsp.data.strContent = write_string(Value(reply), false) + "\n";

    pHttpServer->DispatchEvent(&eventHttpRsp);
}

bool CRPCMod::CheckWalletError(MvErr err)
{
    switch (err)
    {
    case MV_ERR_WALLET_NOT_FOUND:
        throw JSONRPCError(RPC_INVALID_REQUEST,"Missing wallet");
        break;
    case MV_ERR_WALLET_IS_LOCKED:
        throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED,
                           "Wallet is locked,enter the wallet passphrase with walletpassphrase first.");
    case MV_ERR_WALLET_IS_UNLOCKED:
        throw JSONRPCError(RPC_WALLET_ALREADY_UNLOCKED,"Wallet is already unlocked");
        break;
    case MV_ERR_WALLET_IS_ENCRYPTED:
        throw JSONRPCError(RPC_WALLET_WRONG_ENC_STATE,"Running with an encrypted wallet, "
                                                      "but encryptwallet was called");
        break;
    case MV_ERR_WALLET_IS_UNENCRYPTED:
        throw JSONRPCError(RPC_WALLET_WRONG_ENC_STATE,"Running with an unencrypted wallet, "
                                                      "but walletpassphrasechange/walletlock was called.");
        break;
    default:
        break;
    }
    return (err == MV_OK);
}

crypto::CPubKey CRPCMod::GetPubKey(const Value& value)
{
    string str = value.get_str();
    crypto::CPubKey pubkey;
    CMvAddress address(str);
    if (!address.IsNull())
    {
        if (!address.GetPubKey(pubkey))
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid address, should be pubkey address");
        }
    }
    else
    {
        pubkey.SetHex(str);
    }
    return pubkey;
}

CTemplatePtr CRPCMod::MakeTemplate(const string& strType,const Object& obj)
{
    CTemplatePtr ptr;
    if (strType == "weighted")
    {
        const Value& req = find_value(obj,"required");
        if (req.type() != int_type)
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing required");
        }
        int nRequired = req.get_int();
        const Value& pubkeys = find_value(obj,"pubkeys");
        const Object& o = pubkeys.get_obj();
        vector<pair<crypto::CPubKey,unsigned char> > vPubKey;
        BOOST_FOREACH(const Pair& s,o)
        {
            crypto::CPubKey pubkey = GetPubKey(s.name_);
            int nWeight = s.value_.get_int();
            if (nWeight <= 0 || nWeight >= 256)
            {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing weight");
            }
            vPubKey.push_back(make_pair(pubkey,(unsigned char)nWeight));
        }
        ptr = CTemplatePtr(new CTemplateWeighted(vPubKey,nRequired));
    }
    else if (strType == "multisig")
    {
        const Value& req = find_value(obj,"required");
        if (req.type() != int_type)
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing required");
        }
        int nRequired = req.get_int();
        const Value& pubkeys = find_value(obj,"pubkeys");
        const Array& ks = pubkeys.get_array();
        vector<crypto::CPubKey> vPubKey;
        for (int i = 0;i < ks.size();i++)
        {
            vPubKey.push_back(GetPubKey(ks[i]));
        } 
        ptr = CTemplatePtr(new CTemplateMultiSig(vPubKey,nRequired));
    }
    else if (strType == "fork")
    {
        const Value& redeem = find_value(obj,"redeem");
        const Value& fork = find_value(obj,"fork");
        CMvAddress address(redeem.get_str());
        if (address.IsNull())
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing redeem address");
        }
        uint256 hashFork(fork.get_str());
        ptr = CTemplatePtr(new CTemplateFork(static_cast<CDestination&>(address),hashFork));
    }
    else if (strType == "mint")
    {
        const Value& mint = find_value(obj,"mint");
        const Value& spent = find_value(obj,"spent");
        crypto::CPubKey keyMint = GetPubKey(mint);
        CMvAddress addrSpent(spent.get_str());
        if (addrSpent.IsNull())
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing spent address");
        }
        ptr = CTemplatePtr(new CTemplateMint(keyMint,static_cast<CDestination&>(addrSpent)));
    }
    else if (strType == "delegate")
    {
        const Value& delegate = find_value(obj,"delegate");
        const Value& owner = find_value(obj,"owner");
        crypto::CPubKey keyDelegate = GetPubKey(delegate);
        CMvAddress addrOwner(owner.get_str());
        if (addrOwner.IsNull())
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing owner address");
        }
        ptr = CTemplatePtr(new CTemplateDelegate(keyDelegate,static_cast<CDestination&>(addrOwner)));
    }
    return ptr;
}

void CRPCMod::ListDestination(vector<CDestination>& vDestination)
{
    set<crypto::CPubKey> setPubKey;
    set<CTemplateId> setTid;
    pService->GetPubKeys(setPubKey);
    pService->GetTemplateIds(setTid);

    vDestination.clear();
    BOOST_FOREACH(const crypto::CPubKey& pubkey,setPubKey)
    {
        vDestination.push_back(CDestination(pubkey));
    }
    BOOST_FOREACH(const CTemplateId& tid,setTid)
    {
        vDestination.push_back(CDestination(tid));
    }
}

Value CRPCMod::RPCHelp(const Array& params,bool fHelp)
{
    if (fHelp || params.size() > 1)
    {
        throw runtime_error(
            "help [command]\n"
            "List commands, or get help for a command.");
    }
    map<string,RPCFunc>::iterator it;
    if (params.size() > 0)
    {
        it = mapRPCFunc.find(params[0].get_str());
        if (it != mapRPCFunc.end())
        {
            try
            {
                Array params;
                (this->*(*it).second)(params,true);
            }
            catch (exception& e)
            {
                return string(e.what());
            }
        }
        else
        {
            return (string("help: unknown command: ") + params[0].get_str());
        }
    }
    else
    {
        string strRet;
        for (it = mapRPCFunc.begin();it != mapRPCFunc.end();++it)
        {
            try
            {
                Array params;
                (this->*(*it).second)(params,true);
            }
            catch (exception& e)
            {
                string strHelp = string(e.what());
                strRet += strHelp.substr(0, strHelp.find('\n')) + "\n";
            }
        }
        return strRet.substr(0,strRet.size() - 1);
    }

    return Value::null;
}

Value CRPCMod::RPCStop(const Array& params,bool fHelp)
{
    if (fHelp || params.size() != 0)
    {
        throw runtime_error(
            "stop\n"
            "Stop multiverse server.");
    }
    pService->Shutdown();
    return "multiverse server stopping";
}

Value CRPCMod::RPCGetPeerCount(const Array& params,bool fHelp)
{
    if (fHelp || params.size() != 0)
    {
        throw runtime_error(
            "getpeercount\n"
            "Returns the number of connections to other nodes.");
    }
    return pService->GetPeerCount();
}

Value CRPCMod::RPCListPeer(const Array& params,bool fHelp)
{
    if (fHelp || params.size() != 0)
    {
        throw runtime_error(
            "listpeer\n"
            "Returns data about each connected network node.");
    }
    vector<network::CMvPeerInfo> vPeerInfo;
    pService->GetPeers(vPeerInfo);

    Array ret;
    BOOST_FOREACH(const network::CMvPeerInfo& info,vPeerInfo)
    {
        Object obj;
        obj.push_back(Pair("address", info.strAddress));
        obj.push_back(Pair("services", UIntToHexString(info.nService)));
        obj.push_back(Pair("lastsend", (boost::int64_t)info.nLastSend));
        obj.push_back(Pair("lastrecv", (boost::int64_t)info.nLastRecv));
        obj.push_back(Pair("conntime", (boost::int64_t)info.nActive));
        obj.push_back(Pair("version", FormatVersion(info.nVersion)));
        obj.push_back(Pair("subver", info.strSubVer));
        obj.push_back(Pair("inbound", info.fInBound));
        obj.push_back(Pair("height", info.nStartingHeight));
        obj.push_back(Pair("banscore", info.nScore));
        
        ret.push_back(obj);
    }

    return ret;
}

Value CRPCMod::RPCAddNode(const Array& params,bool fHelp)
{
    if (fHelp || params.size() != 1)
    {
        throw runtime_error(
            "addnode <node>\n"
            "Attempts add a node into the addnode list.\n");
    }
    string strNode = params[0].get_str();

    if (!pService->AddNode(CNetHost(strNode,WalleveConfig()->nPort)))
    {
        throw JSONRPCError(RPC_CLIENT_INVALID_IP_OR_SUBNET,"Failed to add node.");
    }

    return Value::null;
}

Value CRPCMod::RPCRemoveNode(const Array& params,bool fHelp)
{
    if (fHelp || params.size() != 1)
    {
        throw runtime_error(
            "removenode <node>\n"
            "Attempts remove a node from the addnode list.\n");
    }
    string strNode = params[0].get_str();

    if (!pService->RemoveNode(CNetHost(strNode,WalleveConfig()->nPort)))
    {
        throw JSONRPCError(RPC_CLIENT_INVALID_IP_OR_SUBNET,"Failed to remove node.");
    }

    return Value::null;
}

Value CRPCMod::RPCGetForkCount(const Array& params,bool fHelp)
{
    if (fHelp || params.size() != 0)
    {
        throw runtime_error(
            "getforkcount\n"
            "Returns the number of forks.");
    }
    return pService->GetForkCount();
}

Value CRPCMod::RPCGetForkGenealogy(const Array& params,bool fHelp)
{
    if (fHelp || params.size() > 1)
    {
        throw runtime_error(
            "getgenealogy [fork=0]\n"
            "Returns the list of ancestry and subline.");
    }

    uint256 fork = GetForkHash(params,0);
    vector<pair<uint256,int> > vAncestry;
    vector<pair<int,uint256> > vSubline;

    if (!pService->GetForkGenealogy(fork,vAncestry,vSubline))
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Unknown fork");
    }

    Object ancestry;
    for (int i = vAncestry.size();i > 0;i--)
    {
        ancestry.push_back(Pair(vAncestry[i - 1].first.GetHex(),vAncestry[i - 1].second));
    }

    Object subline;
    for (int i = 0;i > vSubline.size();i++)
    {
        subline.push_back(Pair(vSubline[i].second.GetHex(),vSubline[i].first));
    }

    Object ret;
    ret.push_back(Pair("ancestry",ancestry));
    ret.push_back(Pair("subline",subline));
    return ret;
}

Value CRPCMod::RPCGetBlockLocation(const Array& params,bool fHelp)
{
    if (fHelp || params.size() != 1)
    {
        throw runtime_error(
            "getlocation <blockhash>\n"
            "Returns the location with given block.");
    }
    uint256 hash(params[0].get_str());
    uint256 fork;
    int height;
    if (!pService->GetBlockLocation(hash,fork,height))
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Unknown block");
    }
    Object ret;
    ret.push_back(Pair(fork.GetHex(),height));
    return ret;
}

Value CRPCMod::RPCGetBlockCount(const Array& params,bool fHelp)
{
    if (fHelp || params.size() > 1)
    {
        throw runtime_error(
            "getblockcount [fork=0]\n"
            "Returns the number of blocks in the given fork.");
    }
    uint256 fork = GetForkHash(params,0);
    return pService->GetBlockCount(fork);
}

Value CRPCMod::RPCGetBlockHash(const Array& params,bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
    {
        throw runtime_error(
            "getblockhash <index> [fork=0]\n"
            "Returns hash of block in fork at <index>.");
    }
    int nHeight = params[0].get_int();
    uint256 fork = GetForkHash(params,1);
    uint256 hash = 0;
    if (!pService->GetBlockHash(fork,nHeight,hash))
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Block number out of range.");
    }
    return (hash.GetHex());
}

Value CRPCMod::RPCGetBlock(const Array& params,bool fHelp)
{
    if (fHelp || params.size() != 1)
    {
        throw runtime_error(
            "getblock <hash>\n"
            "Returns details of a block with given block-hash.");
    }

    uint256 hash(params[0].get_str());
    CBlock block;
    uint256 fork;
    int height;
    if (!pService->GetBlock(hash,block,fork,height))
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Unknown block");
    }

    return BlockToJSON(hash,block,fork,height);
}

Value CRPCMod::RPCGetTxPool(const Array& params,bool fHelp)
{
    if (fHelp || params.size() > 2)
    {
        throw runtime_error(
            "gettxpool [fork=0] [detail=0]\n"
            "If detail==0, returns the count and total size of txs for given fork.\n"
            "Otherwise,returns all transaction ids and sizes in memory pool for given fork.");
    }
    uint256 fork = GetForkHash(params,0);
    bool fDetail = (!!GetInt(params,1,0));
    
    vector<pair<uint256,size_t> > vTxPool;
    pService->GetTxPool(fork,vTxPool);

    Object ret;
    if (!fDetail)
    {
        size_t nTotalSize = 0;
        for (int i = 0;i < vTxPool.size();i++)
        {
            nTotalSize += vTxPool[i].second;
        }
        ret.push_back(Pair("count",vTxPool.size()));
        ret.push_back(Pair("size",nTotalSize));
    }
    else
    {
        for (int i = 0;i < vTxPool.size();i++)
        {
            ret.push_back(Pair(vTxPool[i].first.GetHex(),vTxPool[i].second));
        }
    }
    
    return ret;
}

Value CRPCMod::RPCRemovePendingTx(const Array& params,bool fHelp)
{
    if (fHelp || params.size() != 1)
    {
        throw runtime_error(
            "removependingtx <txid>\n"
            "Removes tx whose id is <txid> from txpool.");
    }
    uint256 txid;
    txid.SetHex(params[0].get_str());
    if (!pService->RemovePendingTx(txid))
    {
        throw JSONRPCError(RPC_INVALID_REQUEST, "This transaction is not in tx pool");
    }
    return Value::null;
}

Value CRPCMod::RPCGetTransaction(const Array& params,bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
    {
        throw runtime_error(
            "gettransaction <txid> [serialized=0]\n"
            "If serialized=0, returns an Object with information about <txid>.\n"
            "If serialized is non-zero, returns a string that is \n"
            "serialized, hex-encoded data for <txid>.");
    }
    uint256 txid;
    txid.SetHex(params[0].get_str());
    bool fSerialized = false;
    if (params.size() > 1)
    {
        fSerialized = (params[1].get_int() != 0);
       
    }

    CTransaction tx;
    uint256 hashFork;
    int nHeight;
 
    if (!pService->GetTransaction(txid,tx,hashFork,nHeight))
    {
        throw JSONRPCError(RPC_INVALID_REQUEST, "No information available about transaction");
    }
    if (fSerialized)
    {
        CWalleveBufStream ss;
        ss << tx;
        return ToHexString((const unsigned char*)ss.GetData(),ss.GetSize());
    }
    int nDepth = nHeight < 0 ? 0 : pService->GetBlockCount(hashFork) - nHeight;
    return TxToJSON(txid,tx,hashFork,nDepth);
}

Value CRPCMod::RPCSendTransaction(const Array& params,bool fHelp)
{
    if (fHelp || params.size() != 1)
    {
        throw runtime_error(
            "sendtransaction <hex string> [checkinputs=1]\n"
            "Submits raw transaction (serialized, hex-encoded) to local node and network.");
    }
    vector<unsigned char> txData = ParseHexString(params[0].get_str());
    CWalleveBufStream ss;
    ss.Write((char *)&txData[0],txData.size());
    CTransaction rawTx;
    try
    {
        ss >> rawTx;
    }
    catch (const std::exception &e)
    {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    }
    MvErr err = pService->SendTransaction(rawTx);
    if (err != MV_OK)
    {
        throw JSONRPCError(RPC_TRANSACTION_REJECTED,string("Tx rejected : ")
                                                    + MvErrString(err));
    }
    
    return rawTx.GetHash().GetHex();
}

Value CRPCMod::RPCListKey(const Array& params,bool fHelp)
{
    if (fHelp || params.size() != 0)
    {
        throw runtime_error(
            "listkey\n"
            "Returns Object that has pubkey as keys, associated status as values.\n");
    }
    set<crypto::CPubKey> setPubKey;
    pService->GetPubKeys(setPubKey);

    Object ret;
    BOOST_FOREACH(const crypto::CPubKey& pubkey,setPubKey)
    {
        int nVersion;
        bool fLocked;
        int64 nAutoLockTime;
        if (pService->GetKeyStatus(pubkey,nVersion,fLocked,nAutoLockTime))
        {
            ostringstream oss;
            oss << "ver=" << nVersion;
            if (fLocked)
            {
                oss << ";locked";
            }
            else
            {
                oss << ";unlocked";
                if (nAutoLockTime > 0)
                {
                    oss <<";timeout=" << (nAutoLockTime - GetTime());
                }
            }
            ret.push_back(Pair(pubkey.GetHex(),oss.str()));
        }
    } 
    return ret;
}

Value CRPCMod::RPCGetNewKey(const Array& params,bool fHelp)
{
    if (fHelp || params.size() > 1)
    {
        throw runtime_error(
            "getnewkey [passphrase]\n"
            "Returns a new pubkey for receiving payments.  "
            "If [passphrase] is specified (recommended), the key will be encrypted with [passphrase]. \n");
    }
    
    crypto::CCryptoString strPassphrase;
    if (params.size() != 0)
    {
        strPassphrase = params[0].get_str().c_str();
    }
    crypto::CPubKey pubkey;

    if (!pService->MakeNewKey(strPassphrase,pubkey))
    {
        throw JSONRPCError(RPC_WALLET_ERROR,"Failed add new key.");
    }

    return (pubkey.ToString());
}
Value CRPCMod::RPCEncryptKey(const Array& params,bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size()  > 3)
    {
        throw runtime_error(
            "encryptkey <pubkey> <passphrase> [oldpassphrase]\n"
            "Encrypts the key assoiciated with <passphrase>.  "
            "For encrypted key, changes the passphrase for [oldpassphrase] to <passphrase> \n");
    }

    crypto::CPubKey pubkey;
    pubkey.SetHex(params[0].get_str());
    crypto::CCryptoString strPassphrase,strOldPassphrase;
    strPassphrase = params[1].get_str().c_str();
    if (params.size() > 2)
    {
        strOldPassphrase = params[2].get_str().c_str();
    }
    if (!pService->HaveKey(pubkey))
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,"Unknown key");
    }
    if (!pService->EncryptKey(pubkey,strPassphrase,strOldPassphrase))
    {
        throw JSONRPCError(RPC_WALLET_PASSPHRASE_INCORRECT,"The passphrase entered was incorrect.");
    }
    return Value::null;
}

Value CRPCMod::RPCLockKey(const Array& params,bool fHelp)
{
    if (fHelp || params.size() != 1)
    {
        throw runtime_error(
            "lockkey <pubkey>\n"
            "Removes the encryption key from memory, locking the key.\n"
            "After calling this method, you will need to call unlockkey again."
            "before being able to call any methods which require the key to be unlocked.");
    }

    crypto::CPubKey pubkey;
    pubkey.SetHex(params[0].get_str());
   
    int nVersion;
    bool fLocked;
    int64 nAutoLockTime; 
    if (!pService->GetKeyStatus(pubkey,nVersion,fLocked,nAutoLockTime))
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,"Unknown key");
    }
    if (!fLocked && !pService->Lock(pubkey))
    {
        throw JSONRPCError(RPC_WALLET_ERROR,"Failed to lock key");
    }
    return Value::null;
}

Value CRPCMod::RPCUnlockKey(const Array& params,bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 3)
    {
        throw runtime_error(
            "unlockkey <pubkey> <passphrase> [timeout=0]\n"
            "If [timeout] > 0,stores the wallet decryption key in memory for [timeout] seconds."
            "before being able to call any methods which require the key to be locked.");
    }

    crypto::CPubKey pubkey;
    pubkey.SetHex(params[0].get_str());
    crypto::CCryptoString strPassphrase;
    strPassphrase = params[1].get_str().c_str();
    int64 nTimeout = 0;
    if (params.size() > 2)
    {
         nTimeout = params[2].get_int64();
    }

    int nVersion;
    bool fLocked;
    int64 nAutoLockTime; 
    if (!pService->GetKeyStatus(pubkey,nVersion,fLocked,nAutoLockTime))
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,"Unknown key");
    }
    if (!fLocked)
    {
        throw JSONRPCError(RPC_WALLET_ALREADY_UNLOCKED,"Key is already unlocked");
    }
    if (!pService->Unlock(pubkey,strPassphrase,nTimeout))
    {
        throw JSONRPCError(RPC_WALLET_PASSPHRASE_INCORRECT,"The passphrase entered was incorrect.");
    }
    return Value::null;
}

Value CRPCMod::RPCImportPrivKey(const Array& params,bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
    {
        throw runtime_error(
            "importprivkey <privkey> [passphrase]\n"
            "Adds a private key (as returned by dumpprivkey) to your wallet."
            "If [passphrase] is specified (recommended), the key will be encrypted with [passphrase]. \n");
    }
    uint256 nPriv(params[0].get_str());
    crypto::CCryptoString strPassphrase;
    if (params.size() == 2)
    {
        strPassphrase = params[1].get_str().c_str();
    }

    crypto::CKey key;
    if (!key.SetSecret(crypto::CCryptoKeyData(nPriv.begin(),nPriv.end())))
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,"Invalid private key");
    }
    if (pService->HaveKey(key.GetPubKey()))
    {
        throw JSONRPCError(RPC_WALLET_ERROR,"Already have key");
    }
    if (!strPassphrase.empty())
    {
        key.Encrypt(strPassphrase);
    }
    if (!pService->AddKey(key))
    {
        throw JSONRPCError(RPC_WALLET_ERROR,"Failed to add key");
    }
    if (!pService->SynchronizeWalletTx(CDestination(key.GetPubKey())))
    {
        throw JSONRPCError(RPC_WALLET_ERROR,"Failed to sync wallet tx");
    }
    return key.GetPubKey().GetHex();
}

Value CRPCMod::RPCImportKey(const Array& params,bool fHelp)
{
    if (fHelp || params.size() != 1)
    {
        throw runtime_error(
            "importkey <serialized key>\n"
            "Adds a key (as returned by importkey) to your wallet.");
    }
    vector<unsigned char> vchKey = ParseHexString(params[0].get_str());
    crypto::CKey key;
    if (!key.Load(vchKey))
    {
        throw JSONRPCError(RPC_INVALID_PARAMS,"Failed to verify serialized key");
    }
    if (pService->HaveKey(key.GetPubKey()))
    {
        throw JSONRPCError(RPC_WALLET_ERROR,"Already have key");
    }
    
    if (!pService->AddKey(key))
    {
        throw JSONRPCError(RPC_WALLET_ERROR,"Failed to add key");
    }
    if (!pService->SynchronizeWalletTx(CDestination(key.GetPubKey())))
    {
        throw JSONRPCError(RPC_WALLET_ERROR,"Failed to sync wallet tx");
    }
    return key.GetPubKey().GetHex();
}

Value CRPCMod::RPCExportKey(const Array& params,bool fHelp)
{
    if (fHelp || params.size() != 1)
    {
        throw runtime_error(
            "exportkey <pubkey>\n"
            "Reveals the serialized key corresponding to <pubkey>.");
    }
    crypto::CPubKey pubkey;
    pubkey.SetHex(params[0].get_str());
    if (!pService->HaveKey(pubkey))
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,"Unknown key");
    }
    vector<unsigned char> vchKey;
    if (!pService->ExportKey(pubkey,vchKey))
    {
        throw JSONRPCError(RPC_WALLET_ERROR,"Failed to export key");
    }
    return ToHexString(vchKey);
}

Value CRPCMod::RPCAddNewTemplate(const Array& params,bool fHelp)
{
    if (fHelp || params.size() != 2)
    {
        throw runtime_error(
            "addnewtemplate <type> <params>\n"
            "Returns encoded address for the given template.");
    }
    CTemplatePtr ptr = MakeTemplate(params[0].get_str(),params[1].get_obj());
    if (ptr == NULL || ptr->IsNull())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters,failed to make template");
    }
    if (!pService->AddTemplate(ptr))
    {
        throw JSONRPCError(RPC_WALLET_ERROR,"Failed to add template");
    }
    if (!pService->SynchronizeWalletTx(CDestination(ptr->GetTemplateId())))
    {
        throw JSONRPCError(RPC_WALLET_ERROR,"Failed to sync wallet tx");
    }
    return CMvAddress(ptr->GetTemplateId()).ToString();
}

Value CRPCMod::RPCImportTemplate(const Array& params,bool fHelp)
{
    if (fHelp || params.size() != 1)
    {
        throw runtime_error(
            "importtemplate <templatedata>\n"
            "Returns encoded address for the given template.");
    }
    vector<unsigned char> vchTemplate = ParseHexString(params[0].get_str());
    CTemplatePtr ptr = CTemplateGeneric::CreateTemplatePtr(vchTemplate);
    if (ptr == NULL || ptr->IsNull())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters,failed to make template");
    }
    if (!pService->AddTemplate(ptr))
    {
        throw JSONRPCError(RPC_WALLET_ERROR,"Failed to add template");
    }
    if (!pService->SynchronizeWalletTx(CDestination(ptr->GetTemplateId())))
    {
        throw JSONRPCError(RPC_WALLET_ERROR,"Failed to sync wallet tx");
    }
    return CMvAddress(ptr->GetTemplateId()).ToString();
}

Value CRPCMod::RPCExportTemplate(const Array& params,bool fHelp)
{
    if (fHelp || params.size() != 1)
    {
        throw runtime_error(
            "exporttemplate <templateaddress>\n"
            "Returns encoded address for the given template.");
    }
    CMvAddress address(params[0].get_str());
    CTemplateId tid;
    if (!address.GetTemplateId(tid))
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid address, should be template address");
    }
 
    CTemplatePtr ptr;
    if (!pService->GetTemplate(tid,ptr))
    {
        throw JSONRPCError(RPC_WALLET_ERROR,"Unkown template");
    }
    vector<unsigned char> vchTemplate;
    ptr->Export(vchTemplate);
    return ToHexString(vchTemplate);
}

Value CRPCMod::RPCValidateAddress(const Array& params,bool fHelp)
{
    if (fHelp || params.size() != 1)
    {
        throw runtime_error(
            "validateaddress <address>\n"
            "Return information about <address>.");
    }

    CMvAddress address(params[0].get_str());
    bool isValid = !address.IsNull();
    Object ret;
    ret.push_back(Pair("isvalid", isValid));
    if (isValid)
    {
        ret.push_back(Pair("address", address.ToString()));
        if (address.IsPubKey())
        {
            crypto::CPubKey pubkey;
            address.GetPubKey(pubkey);
            bool isMine = pService->HaveKey(pubkey);
            ret.push_back(Pair("ismine",isMine));
            ret.push_back(Pair("type", "pubkey"));
            ret.push_back(Pair("pubkey",pubkey.GetHex()));
        }
        else if (address.IsTemplate())
        {
            CTemplateId tid;
            address.GetTemplateId(tid);
            CTemplatePtr ptr;
            uint16 nType = tid.GetType();
            bool isMine = pService->GetTemplate(tid,ptr);
            ret.push_back(Pair("ismine",isMine));
            ret.push_back(Pair("type", "template"));
            ret.push_back(Pair("template",CTemplateGeneric::GetTypeName(nType)));
            if (isMine)
            {
                vector<unsigned char> vchTemplate;
                ptr->Export(vchTemplate);
                ret.push_back(Pair("hex",ToHexString(vchTemplate)));
                if (nType == TEMPLATE_WEIGHTED)
                {
                    CTemplateWeighted* p = (CTemplateWeighted*)ptr.get();
                    ret.push_back(Pair("sigsrequired",p->nRequired));
                    Object addresses;
                    for (map<crypto::CPubKey,unsigned char>::iterator it = p->mapPubKeyWeight.begin();
                         it != p->mapPubKeyWeight.end();++it)
                    {
                        addresses.push_back(Pair(CMvAddress((*it).first).ToString(),(boost::uint64_t)(*it).second));
                    }
                    ret.push_back(Pair("addresses",addresses));
                }
                else if (nType == TEMPLATE_MULTISIG)
                {
                    CTemplateMultiSig* p = (CTemplateMultiSig*)ptr.get();
                    ret.push_back(Pair("sigsrequired",p->nRequired));
                    Array addresses;
                    for (map<crypto::CPubKey,unsigned char>::iterator it = p->mapPubKeyWeight.begin();
                         it != p->mapPubKeyWeight.end();++it)
                    {
                        addresses.push_back(CMvAddress((*it).first).ToString());
                    }
                    ret.push_back(Pair("addresses",addresses));
                }
                else if (nType == TEMPLATE_FORK)
                {
                    CTemplateFork* p = (CTemplateFork*)ptr.get();
                    ret.push_back(Pair("fork",p->hashFork.GetHex()));
                    ret.push_back(Pair("redeem",CMvAddress(p->destRedeem).ToString()));
                }
                else if (nType == TEMPLATE_MINT)
                {
                    CTemplateMint* p = (CTemplateMint*)ptr.get();
                    ret.push_back(Pair("mint",CMvAddress(p->keyMint).ToString()));
                    ret.push_back(Pair("spend",CMvAddress(p->destSpend).ToString()));
                }
                else if (nType == TEMPLATE_DELEGATE)
                {
                    CTemplateDelegate* p = (CTemplateDelegate*)ptr.get();
                    ret.push_back(Pair("delegate",CMvAddress(p->keyDelegate).ToString()));
                    ret.push_back(Pair("owner",CMvAddress(p->destOwner).ToString()));
                }
            }
        }
    }
    return ret;
}

Value CRPCMod::RPCResyncWallet(const Array& params,bool fHelp)
{
    if (fHelp || params.size() > 1)
    {
        throw runtime_error(
            "resyncwallet [address]\n"
            "If [address] is not specified, resync wallet's tx for each address.\n"
            "If [address] is specified, resync wallet's tx for the address.");
    }
    if (params.size() == 1)
    {
        CMvAddress address(params[0].get_str());
        if (address.IsNull())
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid address");
        }
        if (!pService->SynchronizeWalletTx(static_cast<CDestination&>(address)))
        {
            throw JSONRPCError(RPC_WALLET_ERROR, "Failed to resync wallet tx");
        }
    }
    else
    {
        if (!pService->ResynchronizeWalletTx())
        {
            throw JSONRPCError(RPC_WALLET_ERROR, "Failed to resync wallet tx");
        }
    }
    return Value::null;
}

Value CRPCMod::RPCGetBalance(const Array& params,bool fHelp)
{
    if (fHelp || params.size() > 2)
    {
        throw runtime_error(
            "getbalance [fork=0] [address]\n"
            "If [address] is not specified, returns the balance for wallet's each address.\n"
            "If [address] is specified, returns the balance in the address.");
    }

    uint256 hashFork = GetForkHash(params,0);
    vector<CDestination> vDest;
    if (params.size() == 2)
    {
        CMvAddress address(params[1].get_str());
        if (address.IsNull())
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid address");
        }
        vDest.push_back(static_cast<CDestination&>(address));
    }
    else 
    {
        ListDestination(vDest);
    }

    Object ret;
    BOOST_FOREACH(const CDestination& dest,vDest)
    {
        CWalletBalance balance;
        if (pService->GetBalance(dest,hashFork,balance))
        {
            Object o;
            o.push_back(Pair("avail",ValueFromAmount(balance.nAvailable)));
            o.push_back(Pair("locked",ValueFromAmount(balance.nLocked)));
            o.push_back(Pair("unconfirmed",ValueFromAmount(balance.nUnconfirmed)));
            ret.push_back(Pair(CMvAddress(dest).ToString(),o));
        }
    }

    return ret;
}

Value CRPCMod::RPCListTransaction(const Array& params,bool fHelp)
{
    if (fHelp || params.size() > 2)
    {
        throw runtime_error(
            "listtransaction [count=10] [from=0]\n"
            "If [from] < 0,returns last [count] transactions,\n"
            "If [from] >= 0,returns up to [count] most recent transactions skipping the first [from] transactions.");
    }
    int nCount = GetInt(params,0,10);
    int nOffset = GetInt(params,1,0);
    if (nCount <= 0)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative or zero count");
    }
    
    vector<CWalletTx> vWalletTx;
    if (!pService->ListWalletTx(nOffset,nCount,vWalletTx))
    {
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to list transactions");
    }

    Array ret;
    BOOST_FOREACH(const CWalletTx& wtx,vWalletTx)
    {
        ret.push_back(WalletTxToJSON(wtx));
    }
    return ret;
}

Value CRPCMod::RPCSendFrom(const Array& params,bool fHelp)
{
    if (fHelp || params.size() < 3 || params.size() > 6)
    {
        throw runtime_error(
            "sendfrom <fromaddress> <toaddress> <amount> ]txfee=0.0001] [fork=0]\n"
            "<amount> and <txfee> are real and rounded to the nearest 0.000001\n"
            "Returns transaction id");
    }
    CMvAddress from(params[0].get_str());
    CMvAddress to(params[1].get_str());
    if (from.IsNull() || to.IsNull())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid address");
    }
    int64 nAmount = AmountFromValue(params[2]);
    
    int64 nTxFee = MIN_TX_FEE;
    if (params.size() > 3)
    {
        nTxFee = AmountFromValue(params[3]);
        if (nTxFee < MIN_TX_FEE)
        {
            nTxFee = MIN_TX_FEE;
        }
    }
    uint256 hashFork = GetForkHash(params,4);

    CTransaction txNew;
    if (!pService->CreateTransaction(hashFork,from,to,nAmount,nTxFee,vector<unsigned char>(),txNew))
    {
        throw JSONRPCError(RPC_WALLET_ERROR,"Failed to create transaction");
    }
    bool fCompleted = false;
    if (!pService->SignTransaction(txNew,fCompleted))
    {
        throw JSONRPCError(RPC_WALLET_ERROR,"Failed to sign transaction");
    }
    if (!fCompleted)
    {
        throw JSONRPCError(RPC_WALLET_ERROR,"The signature is not completed");
    }
    MvErr err = pService->SendTransaction(txNew);
    if (err != MV_OK)
    {
        throw JSONRPCError(RPC_TRANSACTION_REJECTED,string("Tx rejected : ")
                                                    + MvErrString(err));
    }
    return txNew.GetHash().GetHex();
}

Value CRPCMod::RPCCreateTransaction(const Array& params,bool fHelp)
{
    if (fHelp || params.size() < 3 || params.size() > 6)
    {
        throw runtime_error(
            "createtransaction <fromaddress> <toaddress> <amount> [txfee=0.0001] [fork=0] [data]\n"
            "<amount> and <txfee> are real and rounded to the nearest 0.000001.\n"
            "Returns serialized tx.");
    }
    
    CMvAddress from(params[0].get_str());
    CMvAddress to(params[1].get_str());
    if (from.IsNull() || to.IsNull())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid address");
    }
    int64 nAmount = AmountFromValue(params[2]);
    
    int64 nTxFee = MIN_TX_FEE;
    if (params.size() > 3)
    {
        nTxFee = AmountFromValue(params[3]);
        if (nTxFee < MIN_TX_FEE)
        {
            nTxFee = MIN_TX_FEE;
        }
    }
    uint256 hashFork = GetForkHash(params,4);
    vector<unsigned char> vchData;
    if (params.size() > 5)
    {
        vchData = ParseHexString(params[5].get_str());
    }
    CTransaction txNew;
    if (!pService->CreateTransaction(hashFork,from,to,nAmount,nTxFee,vchData,txNew))
    {
        throw JSONRPCError(RPC_WALLET_ERROR,"Failed to create transaction");
    }

    CWalleveBufStream ss;
    ss << txNew;
    return ToHexString((const unsigned char*)ss.GetData(),ss.GetSize()); 
}

Value CRPCMod::RPCSignTransaction(const Array& params,bool fHelp)
{
    if (fHelp || params.size() != 1)
    {
        throw runtime_error(
            "signtransaction <hex string>\n"
            "Sign transaction (serialized, hex-encoded).\n"
            "Returns json object with keys:\n"
            "  hex : raw transaction with signature(s) (hex-encoded string)\n"
            "  complete : true if transaction has a complete set of signature (false if not)\n");
    }
    vector<unsigned char> txData = ParseHexString(params[0].get_str());
    CWalleveBufStream ss;
    ss.Write((char *)&txData[0],txData.size());
    CTransaction rawTx;
    try
    {
        ss >> rawTx;
    }
    catch (const std::exception &e)
    {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    }

    bool fCompleted = false;
    if (!pService->SignTransaction(rawTx,fCompleted))
    {
        throw JSONRPCError(RPC_WALLET_ERROR,"Failed to sign transaction");
    }

    CWalleveBufStream ssNew;
    ssNew << rawTx;

    Object ret;
    ret.push_back(Pair("hex",ToHexString((const unsigned char*)ssNew.GetData(),ssNew.GetSize())));
    ret.push_back(Pair("complate",fCompleted));

    return ret;
}

Value CRPCMod::RPCSignMessage(const Array& params,bool fHelp)
{
    if (fHelp || params.size() != 2)
    {
        throw runtime_error(
            "signmessage <pubkey> <message>\n"
            "Sign a message with the private key of an pubkey");
    }

    crypto::CPubKey pubkey;
    pubkey.SetHex(params[0].get_str());
    string strMessage = params[1].get_str();

    int nVersion;
    bool fLocked;
    int64 nAutoLockTime; 
    if (!pService->GetKeyStatus(pubkey,nVersion,fLocked,nAutoLockTime))
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,"Unknown key");
    }
    if (fLocked)
    {
        throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED,"Key is locked");
    }
    
    const string strMessageMagic = "Multiverse Signed Message:\n";
    CWalleveBufStream ss;
    ss << strMessageMagic;
    ss << strMessage;
    vector<unsigned char> vchSig;
    if (!pService->SignSignature(pubkey,crypto::CryptoHash(ss.GetData(),ss.GetSize()),vchSig))
    {
        throw JSONRPCError(RPC_WALLET_ERROR,"Failed to sign message");
    }

    return ToHexString(vchSig);
}

Value CRPCMod::RPCVerifyMessage(const Array& params,bool fHelp)
{
    if (fHelp || params.size() != 3)
    {
        throw runtime_error(
            "verifymessage <pubkey> <message> <signature>\n"
            "Verify a signed message");
    }

    crypto::CPubKey pubkey;
    pubkey.SetHex(params[0].get_str());
    string strMessage = params[1].get_str();
    vector<unsigned char> vchSig = ParseHexString(params[2].get_str());
    const string strMessageMagic = "Multiverse Signed Message:\n";
    CWalleveBufStream ss;
    ss << strMessageMagic;
    ss << strMessage;
    
    return pubkey.Verify(crypto::CryptoHash(ss.GetData(),ss.GetSize()),vchSig);
}

Value CRPCMod::RPCMakeKeyPair(const Array& params,bool fHelp)
{
    if (fHelp || params.size() != 0)
    {
        throw runtime_error(
            "makekeypair\n"
            "Make a public/private key pair.\n");
    }

    crypto::CCryptoKey key;
    crypto::CryptoMakeNewKey(key);
    
    Object result;
    result.push_back(Pair("privkey", key.secret.GetHex()));
    result.push_back(Pair("pubkey", key.pubkey.GetHex()));
    return result;
}

Value CRPCMod::RPCGetPubKeyAddress(const Array& params,bool fHelp)
{
    if (fHelp || params.size() != 1)
    {
        throw runtime_error(
            "getpubkeyaddress <pubkey>\n"
            "Returns encoded address for the given pubkey.");
    }

    crypto::CPubKey pubkey;
    pubkey.SetHex(params[0].get_str());
    CDestination dest(pubkey);
    return CMvAddress(dest).ToString();
}

Value CRPCMod::RPCGetTemplateAddress(const Array& params,bool fHelp)
{
    if (fHelp || params.size() != 1)
    {
        throw runtime_error(
            "gettemplateaddress <tid>\n"
            "Returns encoded address for the given template id.");
    }

    CTemplateId tid;
    tid.SetHex(params[0].get_str());
    CDestination dest(tid);
    return CMvAddress(dest).ToString();
}

Value CRPCMod::RPCMakeTemplate(const Array& params,bool fHelp)
{
    if (fHelp || params.size() != 2)
    {
        throw runtime_error(
            "maketemplate <type> <params>\n"
            "Returns encoded address for the given template id.");
    }

    CTemplatePtr ptr = MakeTemplate(params[0].get_str(),params[1].get_obj());
    if (ptr == NULL || ptr->IsNull())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters,failed to make template");
    }
    vector<unsigned char> vchTemplate;
    ptr->Export(vchTemplate);
    Object ret;
    ret.push_back(Pair("address",CMvAddress(ptr->GetTemplateId()).ToString()));
    ret.push_back(Pair("hex",ToHexString(vchTemplate)));
    return ret;
}

Value CRPCMod::RPCDecodeTransaction(const Array& params,bool fHelp)
{
    if (fHelp || params.size() != 1)
    {
        throw runtime_error(
            "decoderawtransaction <hex string>\n"
            "Return a JSON object representing the serialized, hex-encoded transaction.");
    }
    vector<unsigned char> txData(ParseHexString(params[0].get_str()));
    CWalleveBufStream ss;
    ss.Write((char *)&txData[0],txData.size());
    CTransaction rawTx;
    try
    {
        ss >> rawTx;
    }
    catch (const std::exception &e)
    {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    }
    
    uint256 hashFork;
    int nHeight;
    if (!pService->GetBlockLocation(rawTx.hashAnchor,hashFork,nHeight))
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Unknown anchor block");
    }
    return TxToJSON(rawTx.GetHash(),rawTx,hashFork,-1);
}

Value CRPCMod::RPCGetWork(const Array& params,bool fHelp)
{
    if (fHelp || params.size() > 1)
    {
        throw runtime_error(
            "getwork [prev hash]\n"
            "If [prev hash] is matched with the current primary chain,returns true\n"
            "If next block is not generated by proof-of-work,return false\n"
            "Otherwise, returns formatted proof-of-work parameters to work on:\n"
            "  \"prevblockhash\" : prevblock hash\n"
            "  \"prevblocktime\" : prevblock timestamp\n"
            "  \"algo\" : proof-of-work algorithm: blake2b=1,...\n"
            "  \"bits\" : proof-of-work difficulty nbits\n"
            "  \"data\" : work data");
    }

    uint256 hashPrev;
    if (!pService->GetBlockHash(pCoreProtocol->GetGenesisBlockHash(),-1,hashPrev))
    {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "The primary chain is invalid.");
    }
    if (params.size() == 1 && hashPrev == uint256(params[0].get_str()))
    {
        return true;
    }
    vector<unsigned char> vchWorkData;
    uint32 nPrevTime;
    int nAlgo,nBits;
    if (!pService->GetWork(vchWorkData,hashPrev,nPrevTime,nAlgo,nBits))
    {
        return false;
    }
    Object ret;
    ret.push_back(Pair("prevblockhash",hashPrev.GetHex()));
    ret.push_back(Pair("prevblocktime",(boost::uint64_t)nPrevTime));
    ret.push_back(Pair("algo",nAlgo));
    ret.push_back(Pair("bits",nBits));
    ret.push_back(Pair("data",ToHexString(vchWorkData)));
    return ret;
}

Value CRPCMod::RPCSubmitWork(const Array& params,bool fHelp)
{
    if (fHelp || params.size() != 3)
    {
        throw runtime_error(
            "submitwork <data> <spent address> <mintkey>\n"
            "Attempts to construct and submit new block to network\n"
            "Return hash of new block.");
    }

    vector<unsigned char> vchWorkData(ParseHexString(params[0].get_str()));
    CMvAddress addrSpent(params[1].get_str());
    uint256 nPriv(params[2].get_str());
    if (addrSpent.IsNull())
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,"Invalid spent address");
    }
    crypto::CKey key;
    if (!key.SetSecret(crypto::CCryptoKeyData(nPriv.begin(),nPriv.end())))
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,"Invalid private key");
    }
    
    CTemplatePtr ptr = CTemplatePtr(new CTemplateMint(key.GetPubKey(),static_cast<CDestination&>(addrSpent)));
    if (ptr == NULL)
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,"Invalid mint template");
    }
    uint256 hashBlock;
    MvErr err = pService->SubmitWork(vchWorkData,ptr,key,hashBlock);
    if (err != MV_OK)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER,string("Block rejected : ") + MvErrString(err));
    }
    return hashBlock.GetHex();
}
