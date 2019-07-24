// Copyright (c) 2017-2019 The Multiverse developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpcmod.h"

#include <regex>

#include <boost/assign/list_of.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/regex.hpp>

#include "json/json_spirit_reader_template.h"
#include "address.h"
#include "version.h"
#include "rpc/auto_protocol.h"
#include "template/template.h"
#include "template/proof.h"

using namespace std;
using namespace multiverse;
using namespace walleve;
using namespace json_spirit;
using namespace rpc;
namespace fs = boost::filesystem;

///////////////////////////////
// static function

//remove all sensible information such as private key or passphrass from data
static string MaskSecret(const string& data)
{
    boost::regex ptnSec(R"raw(("privkey"|"passphrase"|"oldpassphrase")(\s*:\s*)(".*?"))raw", boost::regex::perl);
    return boost::regex_replace(data, ptnSec, string(R"raw($1$2"***")raw"));
};

static int64 AmountFromValue(const double dAmount)
{
    if (dAmount <= 0.0 || dAmount > MAX_MONEY)
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Invalid amount");
    }
    int64 nAmount = (int64)(dAmount * COIN + 0.5);
    if (!MoneyRange(nAmount))
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Invalid amount");
    }
    return nAmount;
}

static double ValueFromAmount(int64 amount)
{
    return ((double)amount / (double)COIN);
}

static CBlockData BlockToJSON(const uint256& hashBlock,const CBlock& block,const uint256& hashFork,const int32 nHeight)
{
    CBlockData data;
    data.strHash = hashBlock.GetHex();
    data.nVersion = block.nVersion;
    data.strType = GetBlockTypeStr(block.nType,block.txMint.nType);
    data.nTime = block.GetBlockTime();
    if (block.hashPrev != 0)
    {
        data.strPrev = block.hashPrev.GetHex();
  }
    data.strFork = hashFork.GetHex();
    data.nHeight = nHeight;
    
    data.strTxmint = block.txMint.GetHash().GetHex();
    for(const CTransaction& tx : block.vtx)
    {
        data.vecTx.push_back(tx.GetHash().GetHex());
    }
    return data;
}

static CTransactionData TxToJSON(const uint256& txid,const CTransaction& tx,const uint256& hashFork,int nDepth)
{
    CTransactionData ret;
    ret.strTxid = txid.GetHex();
    ret.nVersion = tx.nVersion;
    ret.strType = tx.GetTypeString();
    ret.nTime = tx.nTimeStamp;
    ret.nLockuntil = tx.nLockUntil;
    ret.strAnchor = tx.hashAnchor.GetHex();
    for(const CTxIn& txin : tx.vInput)
    {
        CTransactionData::CVin vin;
        vin.nVout = txin.prevout.n;
        vin.strTxid = txin.prevout.hash.GetHex();
        ret.vecVin.push_back(move(vin));
    }
    ret.strSendto = CMvAddress(tx.sendTo).ToString();
    ret.fAmount = ValueFromAmount(tx.nAmount);
    ret.fTxfee = ValueFromAmount(tx.nTxFee);

    ret.strData = walleve::ToHexString(tx.vchData);
    ret.strSig = walleve::ToHexString(tx.vchSig);
    ret.strFork = hashFork.GetHex();
    if (nDepth >= 0)
    {
        ret.nConfirmations = nDepth;
    }

    return ret;
}

static CWalletTxData WalletTxToJSON(const CWalletTx& wtx)
{
    CWalletTxData data;
    data.strTxid = wtx.txid.GetHex();
    data.strFork = wtx.hashFork.GetHex();
    if (wtx.nBlockHeight >= 0)
    {
        data.nBlockheight = wtx.nBlockHeight;
    }
    data.strType = wtx.GetTypeString();
    data.nTime = (boost::int64_t)wtx.nTimeStamp;
    data.fSend = wtx.IsFromMe();
    if (!wtx.IsMintTx())
    {
        data.strFrom = CMvAddress(wtx.destIn).ToString();
    }
    data.strTo = CMvAddress(wtx.sendTo).ToString();
    data.fAmount = ValueFromAmount(wtx.nAmount);
    data.fFee = ValueFromAmount(wtx.nTxFee);
    data.nLockuntil = (boost::int64_t)wtx.nLockUntil;
    return data;
}

///////////////////////////////
// CRPCMod

CRPCMod::CRPCMod()
: IIOModule("rpcmod"), nWorkId(0)
{
    pHttpServer = NULL;
    pRPCModWorker = NULL;
}

CRPCMod::~CRPCMod()
{
}

bool CRPCMod::WalleveHandleInitialize()
{
    if (!WalleveGetObject("httpserver",pHttpServer))
    {
        WalleveError("Failed to request httpserver\n");
        return false;
    }

    if (!WalleveGetObject("rpcmodworker",pRPCModWorker))
    {
        WalleveError("Failed to request rpc worker\n");
        return false;
    }

    return true;
}

void CRPCMod::WalleveHandleDeinitialize()
{
    pHttpServer = NULL;
    pRPCModWorker = NULL;
}

bool CRPCMod::HandleEvent(CWalleveEventHttpReq& eventHttpReq)
{
    uint64 nNonce = eventHttpReq.nNonce;

    string strResult;
    try
    {
        // check version
        string strVersion = eventHttpReq.data.mapHeader["url"].substr(1);
        if (!strVersion.empty())
        {
            if (!CheckVersion(strVersion))
            {
                throw CRPCException(RPC_VERSION_OUT_OF_DATE,
                    string("Out of date version. Server version is v") + MV_VERSION_STR
                    + ", but client version is v" + strVersion);
            }
        }

        WalleveDebug("request : %s\n", MaskSecret(eventHttpReq.data.strContent).c_str());

        bool fArray;
        CRPCReqVec vecReq = DeserializeCRPCReq(eventHttpReq.data.strContent, fArray);
        size_t nReqSize = vecReq.size();

        list<CWork>& listWork = mapWork[nNonce];
        listWork.push_back(CWork{++nWorkId, nReqSize, fArray, move(vecReq), CRPCRespVec(nReqSize)});
        
        WalleveDebug("work push id: %llu, remainder: %llu, nonce: %llu\n", listWork.back().nWorkId, listWork.back().nRemainder, nNonce);

        if (listWork.size() == 1)
        {
            AssignWork(nNonce, listWork.front());
        }
    }
    catch(CRPCException& e)
    {
        auto spError = MakeCRPCErrorPtr(e);
        CRPCResp resp(e.valData, spError);
        strResult = resp.Serialize();
    }
    catch(exception& e)
    {
        auto spError = MakeCRPCErrorPtr(RPC_MISC_ERROR, e.what());
        CRPCResp resp(Value(), spError);
        strResult = resp.Serialize();
    }

    return true;
}

bool CRPCMod::HandleEvent(CWalleveEventHttpBroken& eventHttpBroken)
{
    uint64 nNonce = eventHttpBroken.nNonce;
    mapWork.erase(nNonce);
    return true;
}

bool CRPCMod::HandleEvent(CMvEventRPCModResponse& eventRPCModResponse)
{
    uint64 nNonce = eventRPCModResponse.nNonce;
    CRPCModResponse& respData = eventRPCModResponse.data;

    auto it = mapWork.find(nNonce);
    if (it == mapWork.end())
    {
        WalleveDebug("Worker response nonce is not exists. nonce: %llu\n", nNonce);
        return true;
    }

    list<CWork>& listWork = it->second;
    if (listWork.empty())
    {
        WalleveError("Worker response list is empty. nonce: %llu\n", nNonce);
        return true;
    }

    CWork& work = listWork.front();
    if (work.nWorkId != respData.nWorkId)
    {
        WalleveError("Worker response work id is not exists. nonce: %llu, work id: %llu, resp work id: %llu\n", 
            nNonce, work.nWorkId, respData.nWorkId);
        return true;
    }

    if (work.vecResp.size() <= respData.nSubWorkId || work.vecResp[respData.nSubWorkId])
    {
        WalleveError("Worker response sub work id error or exists. nonce: %llu, work id: %llu, sub work id: %llu, req size: %llu\n", 
            nNonce, respData.nWorkId, respData.nSubWorkId, work.vecResp.size());
        return true;
    }

    // save response
    CRPCReqPtr spReq = work.vecReq[respData.nSubWorkId];
    CRPCRespPtr spResp = work.vecResp[respData.nSubWorkId];
    if (respData.spError)
    {
        work.vecResp[respData.nSubWorkId] = MakeCRPCRespPtr(spReq->valID, respData.spError);
    }
    else
    {
        work.vecResp[respData.nSubWorkId] = MakeCRPCRespPtr(spReq->valID, respData.spResult);
    }

    WalleveDebug("work finish id: %llu, subid: %llu, remainder: %llu, nonce: %llu\n", respData.nWorkId, respData.nSubWorkId, work.nRemainder, nNonce);

    // reply
    if (--work.nRemainder == 0)
    {
        string strResult;
        if (work.fArray)
        {
            strResult = SerializeCRPCResp(work.vecResp);
        }
        else
        {
            strResult = work.vecResp[0]->Serialize();
        }

        WalleveDebug("response : %s\n", MaskSecret(strResult).c_str());

        JsonReply(nNonce, strResult);

        listWork.pop_front();

        if (!listWork.empty())
        {
            AssignWork(nNonce, listWork.front());
        }
    }

    return true;
}

void CRPCMod::JsonReply(uint64 nNonce, const std::string& result)
{
    CWalleveEventHttpRsp eventHttpRsp(nNonce);
    eventHttpRsp.data.nStatusCode = 200;
    eventHttpRsp.data.mapHeader["content-type"] = "application/json";
    eventHttpRsp.data.mapHeader["connection"] = "Keep-Alive";
    eventHttpRsp.data.mapHeader["server"] = "multiverse-rpc";
    eventHttpRsp.data.strContent = result + "\n";

    pHttpServer->DispatchEvent(&eventHttpRsp);
}

bool CRPCMod::CheckVersion(string& strVersion)
{
    uint32 nMajor, nMinor, nRevision;
    if (!ResolveVersion(strVersion, nMajor, nMinor, nRevision))
    {
        return false;
    }

    strVersion = FormatVersion(nMajor, nMinor, nRevision);
    if (nMajor != MV_VERSION_MAJOR || nMinor != MV_VERSION_MINOR)
    {
        return false;
    }

    return true;
}

void CRPCMod::AssignWork(const uint64 nNonce, const CWork& work)
{
    for (size_t i = 0; i < work.vecReq.size(); ++i)
    {
        const CRPCReqPtr spReq = work.vecReq[i];

        CMvEventRPCModRequest* pEventRequest = new CMvEventRPCModRequest(nNonce);
        CRPCModRequest& reqData = pEventRequest->data;
        reqData.spReq = spReq;
        reqData.nWorkId = work.nWorkId;
        reqData.nSubWorkId = i;

        pRPCModWorker->PostEvent(pEventRequest);
    }
}

///////////////////////////////
// CRPCModWorker

CRPCModWorker::CRPCModWorker(const uint nThreadIn, const bool fAffinity)
: IIOModule("rpcmodworker", nThreadIn, fAffinity)
{
    pCoreProtocol = NULL;
    pService = NULL;
    pRPCMod = NULL;

    mapRPCFunc = {
        /* System */
        {"help",                  &CRPCModWorker::RPCHelp},
        {"stop",                  &CRPCModWorker::RPCStop},
        {"version",               &CRPCModWorker::RPCVersion},
        /* Network */
        {"getpeercount",          &CRPCModWorker::RPCGetPeerCount},
        {"listpeer",              &CRPCModWorker::RPCListPeer},
        {"addnode",               &CRPCModWorker::RPCAddNode},
        {"removenode",            &CRPCModWorker::RPCRemoveNode},
        /* Worldline & TxPool */
        {"getforkcount",          &CRPCModWorker::RPCGetForkCount},
        {"listfork",              &CRPCModWorker::RPCListFork},
        {"getgenealogy",          &CRPCModWorker::RPCGetForkGenealogy},
        {"getblocklocation",      &CRPCModWorker::RPCGetBlockLocation},
        {"getblockcount",         &CRPCModWorker::RPCGetBlockCount},
        {"getblockhash",          &CRPCModWorker::RPCGetBlockHash},
        {"getblock",              &CRPCModWorker::RPCGetBlock},
        {"gettxpool",             &CRPCModWorker::RPCGetTxPool},
        {"gettransaction",        &CRPCModWorker::RPCGetTransaction},
        {"sendtransaction",       &CRPCModWorker::RPCSendTransaction},
        {"getforkheight",         &CRPCModWorker::RPCGetForkHeight},
        /* Wallet */
        {"listkey",               &CRPCModWorker::RPCListKey},
        {"getnewkey",             &CRPCModWorker::RPCGetNewKey},
        {"encryptkey",            &CRPCModWorker::RPCEncryptKey},
        {"lockkey",               &CRPCModWorker::RPCLockKey},
        {"unlockkey",             &CRPCModWorker::RPCUnlockKey},
        {"importprivkey",         &CRPCModWorker::RPCImportPrivKey},
        {"importkey",             &CRPCModWorker::RPCImportKey},
        {"exportkey",             &CRPCModWorker::RPCExportKey},
        {"addnewtemplate",        &CRPCModWorker::RPCAddNewTemplate},
        {"importtemplate",        &CRPCModWorker::RPCImportTemplate},
        {"exporttemplate",        &CRPCModWorker::RPCExportTemplate},
        {"validateaddress",       &CRPCModWorker::RPCValidateAddress},
        {"resyncwallet",          &CRPCModWorker::RPCResyncWallet},
        {"getbalance",            &CRPCModWorker::RPCGetBalance},
        {"listtransaction",       &CRPCModWorker::RPCListTransaction},
        {"sendfrom",              &CRPCModWorker::RPCSendFrom},
        {"createtransaction",     &CRPCModWorker::RPCCreateTransaction},
        {"signtransaction",       &CRPCModWorker::RPCSignTransaction},
        {"signmessage",           &CRPCModWorker::RPCSignMessage},
        {"listaddress",           &CRPCModWorker::RPCListAddress},
        {"exportwallet",          &CRPCModWorker::RPCExportWallet},
        {"importwallet",          &CRPCModWorker::RPCImportWallet},
        {"makeorigin",            &CRPCModWorker::RPCMakeOrigin},
        /* Util */
        {"verifymessage",         &CRPCModWorker::RPCVerifyMessage},
        {"makekeypair",           &CRPCModWorker::RPCMakeKeyPair},
        {"getpubkeyaddress",      &CRPCModWorker::RPCGetPubKeyAddress},
        {"gettemplateaddress",    &CRPCModWorker::RPCGetTemplateAddress},
        {"maketemplate",          &CRPCModWorker::RPCMakeTemplate},
        {"decodetransaction",     &CRPCModWorker::RPCDecodeTransaction},
        /* Mint */
        {"getwork",               &CRPCModWorker::RPCGetWork},
        {"submitwork",            &CRPCModWorker::RPCSubmitWork},
    };
}

CRPCModWorker::~CRPCModWorker()
{
}

bool CRPCModWorker::WalleveHandleInitialize()
{
    if (!WalleveGetObject("coreprotocol",pCoreProtocol))
    {
        WalleveError("Failed to request coreprotocol\n");
        return false;
    }
    
    if (!WalleveGetObject("service",pService))
    {
        WalleveError("Failed to request service\n");
        return false;
    }

    if (!WalleveGetObject("rpcmod",pRPCMod))
    {
        WalleveError("Failed to request rpc worker\n");
        return false;
    }

    return true;
}

void CRPCModWorker::WalleveHandleDeinitialize()
{
    pCoreProtocol = NULL;
    pService = NULL;
    pRPCMod = NULL;
}

const CMvNetworkConfig* CRPCModWorker::WalleveConfig()
{
    return dynamic_cast<const CMvNetworkConfig*>(IWalleveBase::WalleveConfig());
}

int CRPCModWorker::GetInt(const CRPCInt64& i, int valDefault)
{
    return i.IsValid() ? int(i) : valDefault;
}
unsigned int CRPCModWorker::GetUint(const CRPCUint64& i, unsigned int valDefault)
{
    return i.IsValid() ? uint64(i) : valDefault;
}
const bool CRPCModWorker::GetForkHashOfDef(const CRPCString& hex, uint256& hashFork)
{
    if (!hex.empty())
    {
        if (hashFork.SetHex(hex) != hex.size())
        {
            return false;
        }
    }
    else
    {
        hashFork = pCoreProtocol->GetGenesisBlockHash();
    }
    return true;
}

bool CRPCModWorker::HandleEvent(CMvEventRPCModRequest& eventRequest)
{
    CRPCModRequest& reqData = eventRequest.data;
    CRPCReqPtr spReq = reqData.spReq;

    CMvEventRPCModResponse* pEventResponse = new CMvEventRPCModResponse(eventRequest.nNonce);
    CRPCModResponse& respData = pEventResponse->data;
    respData.nWorkId = reqData.nWorkId;
    respData.nSubWorkId = reqData.nSubWorkId;
    try
    {
        map<std::string, RPCFunc>::iterator it = mapRPCFunc.find(spReq->strMethod);
        if (it == mapRPCFunc.end())
        {
            throw CRPCException(RPC_METHOD_NOT_FOUND, "Method not found");
        }

        respData.spResult = (this->*(it->second))(spReq->spParam);
    }
    catch (CRPCException& e)
    {
        respData.spError = CRPCErrorPtr(new CRPCError(e));
    }
    catch (exception& e)
    {
        respData.spError = CRPCErrorPtr(new CRPCError(RPC_MISC_ERROR, e.what()));
    }

    pRPCMod->PostEvent(pEventResponse);
    return true;
}

bool CRPCModWorker::CheckWalletError(MvErr err)
{
    switch (err)
    {
    case MV_ERR_WALLET_NOT_FOUND:
        throw CRPCException(RPC_INVALID_REQUEST,"Missing wallet");
        break;
    case MV_ERR_WALLET_IS_LOCKED:
        throw CRPCException(RPC_WALLET_UNLOCK_NEEDED,
                           "Wallet is locked,enter the wallet passphrase with walletpassphrase first.");
    case MV_ERR_WALLET_IS_UNLOCKED:
        throw CRPCException(RPC_WALLET_ALREADY_UNLOCKED,"Wallet is already unlocked");
        break;
    case MV_ERR_WALLET_IS_ENCRYPTED:
        throw CRPCException(RPC_WALLET_WRONG_ENC_STATE,"Running with an encrypted wallet, "
                                                      "but encryptwallet was called");
        break;
    case MV_ERR_WALLET_IS_UNENCRYPTED:
        throw CRPCException(RPC_WALLET_WRONG_ENC_STATE,"Running with an unencrypted wallet, "
                                                      "but walletpassphrasechange/walletlock was called.");
        break;
    default:
        break;
    }
    return (err == MV_OK);
}

crypto::CPubKey CRPCModWorker::GetPubKey(const string& addr)
{
    crypto::CPubKey pubkey;
    CMvAddress address(addr);
    if (!address.IsNull())
    {
        if (!address.GetPubKey(pubkey))
        {
            throw CRPCException(RPC_INVALID_PARAMETER, "Invalid address, should be pubkey address");
        }
    }
    else
    {
        pubkey.SetHex(addr);
    }
    return pubkey;
}

void CRPCModWorker::ListDestination(vector<CDestination>& vDestination)
{
    set<crypto::CPubKey> setPubKey;
    set<CTemplateId> setTid;
    pService->GetPubKeys(setPubKey);
    pService->GetTemplateIds(setTid);

    vDestination.clear();
    for(const crypto::CPubKey& pubkey : setPubKey)
    {
        vDestination.push_back(CDestination(pubkey));
    }
    for(const CTemplateId& tid : setTid)
    {
        vDestination.push_back(CDestination(tid));
    }
}

/* System */
CRPCResultPtr CRPCModWorker::RPCHelp(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CHelpParam>(param);
    string command = spParam->strCommand;
    return MakeCHelpResultPtr(RPCHelpInfo(EModeType::CONSOLE, command));
}

CRPCResultPtr CRPCModWorker::RPCStop(CRPCParamPtr param)
{
    pService->Shutdown();
    return MakeCStopResultPtr("multiverse server stopping");
}

CRPCResultPtr CRPCModWorker::RPCVersion(CRPCParamPtr param)
{
    string strVersion = string("Multiverse server version is v") + MV_VERSION_STR;
    return MakeCVersionResultPtr(strVersion);
}

/* Network */
CRPCResultPtr CRPCModWorker::RPCGetPeerCount(CRPCParamPtr param)
{
    return MakeCGetPeerCountResultPtr(pService->GetPeerCount());
}

CRPCResultPtr CRPCModWorker::RPCListPeer(CRPCParamPtr param)
{
    vector<network::CMvPeerInfo> vPeerInfo;
    pService->GetPeers(vPeerInfo);

    auto spResult = MakeCListPeerResultPtr();
    for(const network::CMvPeerInfo& info : vPeerInfo)
    {
        CListPeerResult::CPeer peer;
        peer.strAddress = info.strAddress;
        peer.strServices = UIntToHexString(info.nService);
        peer.nLastsend = info.nLastSend;
        peer.nLastrecv = info.nLastRecv;
        peer.nConntime = info.nActive;
        peer.strVersion = FormatVersion(info.nVersion);
        peer.strSubver = info.strSubVer;
        peer.fInbound = info.fInBound;
        peer.nHeight = info.nStartingHeight;
        peer.nBanscore = info.nScore;
        spResult->vecPeer.push_back(peer);
    }

    return spResult;
}

CRPCResultPtr CRPCModWorker::RPCAddNode(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CAddNodeParam>(param);
    string strNode = spParam->strNode;

    if (!pService->AddNode(CNetHost(strNode,WalleveConfig()->nPort)))
    {
        throw CRPCException(RPC_CLIENT_INVALID_IP_OR_SUBNET,"Failed to add node.");
    }

    return MakeCAddNodeResultPtr(string("Add node successfully: ") + strNode);
}

CRPCResultPtr CRPCModWorker::RPCRemoveNode(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CRemoveNodeParam>(param);
    string strNode = spParam->strNode;

    if (!pService->RemoveNode(CNetHost(strNode,WalleveConfig()->nPort)))
    {
        throw CRPCException(RPC_CLIENT_INVALID_IP_OR_SUBNET,"Failed to remove node.");
    }

    return MakeCRemoveNodeResultPtr(string("Remove node successfully: ") + strNode);
}

CRPCResultPtr CRPCModWorker::RPCGetForkCount(CRPCParamPtr param)
{
    return MakeCGetForkCountResultPtr(pService->GetForkCount());
}

CRPCResultPtr CRPCModWorker::RPCListFork(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CListForkParam>(param);
    vector<pair<uint256,CProfile> > vFork;
    pService->ListFork(vFork, spParam->fAll);

    auto spResult = MakeCListForkResultPtr();
    for (size_t i = 0; i < vFork.size(); i++)
    {
        CProfile& profile = vFork[i].second;
        spResult->vecProfile.push_back({ vFork[i].first.GetHex(), profile.strName, profile.strSymbol,
                                         profile.IsIsolated(), profile.IsPrivate(), profile.IsEnclosed(),
                                         CMvAddress(profile.destOwner).ToString() });
    }

    return spResult;
}

CRPCResultPtr CRPCModWorker::RPCGetForkGenealogy(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CGetGenealogyParam>(param);

    //getgenealogy (-f="fork")
    uint256 fork;
    if (!GetForkHashOfDef(spParam->strFork, fork))
    {
    	throw CRPCException(RPC_INVALID_PARAMETER, "Invalid fork");
    }

    vector<pair<uint256,int32> > vAncestry;
    vector<pair<int32,uint256> > vSubline;
    if (!pService->GetForkGenealogy(fork,vAncestry,vSubline))
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Unknown fork");
    }

    auto spResult = MakeCGetGenealogyResultPtr();
    for (size_t i = vAncestry.size();i > 0;i--)
    {
        spResult->vecAncestry.push_back({vAncestry[i - 1].first.GetHex(), vAncestry[i - 1].second});
    }
    for (size_t i = 0;i < vSubline.size();i++)
    {
        spResult->vecSubline.push_back({vSubline[i].second.GetHex(), vSubline[i].first});
    }
    return spResult;
}

CRPCResultPtr CRPCModWorker::RPCGetBlockLocation(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CGetBlockLocationParam>(param);
    
    //getblocklocation <"block">
    uint256 hashBlock;
    hashBlock.SetHex(spParam->strBlock);

    uint256 fork;
    int32 height;
    if (!pService->GetBlockLocation(hashBlock,fork,height))
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Unknown block");
    }

    auto spResult = MakeCGetBlockLocationResultPtr();
    spResult->strFork = fork.GetHex();
    spResult->nHeight = height;
    return spResult;
}

CRPCResultPtr CRPCModWorker::RPCGetBlockCount(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CGetBlockCountParam>(param);

    //getblockcount (-f="fork")
    uint256 hashFork;
	if (!GetForkHashOfDef(spParam->strFork, hashFork))
    {
    	throw CRPCException(RPC_INVALID_PARAMETER, "Invalid fork");
    }
    
    if (!pService->HaveFork(hashFork))
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Unknown fork");
    }

    return MakeCGetBlockCountResultPtr(pService->GetBlockCount(hashFork));
}

CRPCResultPtr CRPCModWorker::RPCGetBlockHash(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CGetBlockHashParam>(param);

    //getblockhash <height> (-f="fork")
    int32 nHeight = spParam->nHeight;

    uint256 hashFork;
    if (!GetForkHashOfDef(spParam->strFork, hashFork))
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Invalid fork");
    }
    
    if (!pService->HaveFork(hashFork))
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Unknown fork");
    }

    vector<uint256> vBlockHash;
    if (!pService->GetBlockHash(hashFork,nHeight,vBlockHash))
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Block number out of range.");
    }

    auto spResult = MakeCGetBlockHashResultPtr();
    for (const uint256& hash: vBlockHash)
    {
        spResult->vecHash.push_back(hash.GetHex());
    }

    return spResult;
}

CRPCResultPtr CRPCModWorker::RPCGetBlock(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CGetBlockParam>(param);
    
    //getblock <"block">
    uint256 hashBlock;
    hashBlock.SetHex(spParam->strBlock);

    CBlock block;
    uint256 fork;
    int32 height;
    if (!pService->GetBlock(hashBlock,block,fork,height))
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Unknown block");
    }

    return MakeCGetBlockResultPtr(BlockToJSON(hashBlock,block,fork,height));
}

CRPCResultPtr CRPCModWorker::RPCGetTxPool(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CGetTxPoolParam>(param);

    //gettxpool (-f="fork") (-d|-nod*detail*)
    uint256 hashFork;
    if (!GetForkHashOfDef(spParam->strFork, hashFork))
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Invalid fork");
    }
    
    if (!pService->HaveFork(hashFork))
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Unknown fork");
    }

    bool fDetail = spParam->fDetail.IsValid() ? bool(spParam->fDetail) : false;
    
    vector<pair<uint256,size_t> > vTxPool;
    pService->GetTxPool(hashFork,vTxPool);

    auto spResult = MakeCGetTxPoolResultPtr();
    if (!fDetail)
    {
        size_t nTotalSize = 0;
        for (std::size_t i = 0;i < vTxPool.size();i++)
        {
            nTotalSize += vTxPool[i].second;
        }
        spResult->nCount = vTxPool.size();
        spResult->nSize = nTotalSize;
    }
    else
    {
        for (std::size_t i = 0;i < vTxPool.size();i++)
        {
            spResult->vecList.push_back({vTxPool[i].first.GetHex(), vTxPool[i].second});
        }
    }
    
    return spResult;
}

CRPCResultPtr CRPCModWorker::RPCGetTransaction(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CGetTransactionParam>(param);
    uint256 txid;
    txid.SetHex(spParam->strTxid);

    CTransaction tx;
    uint256 hashFork;
    int32 nHeight;

    if (!pService->GetTransaction(txid,tx,hashFork,nHeight))
    {
        throw CRPCException(RPC_INVALID_REQUEST, "No information available about transaction");
    }

    auto spResult = MakeCGetTransactionResultPtr();
    if (spParam->fSerialized)
    {
        CWalleveBufStream ss;
        ss << tx;
        spResult->strSerialization = ToHexString((const unsigned char*)ss.GetData(),ss.GetSize());
        return spResult;
    }

    int nDepth = nHeight < 0 ? 0 : pService->GetBlockCount(hashFork) - nHeight;
    spResult->transaction = TxToJSON(txid,tx,hashFork,nDepth);
    return spResult;
}

CRPCResultPtr CRPCModWorker::RPCSendTransaction(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CSendTransactionParam>(param);

    vector<unsigned char> txData = ParseHexString(spParam->strTxdata);
    CWalleveBufStream ss;
    ss.Write((char *)&txData[0],txData.size());
    CTransaction rawTx;
    try
    {
        ss >> rawTx;
    }
    catch (const std::exception &e)
    {
        throw CRPCException(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    }
    MvErr err = pService->SendTransaction(rawTx);
    if (err != MV_OK)
    {
        throw CRPCException(RPC_TRANSACTION_REJECTED,string("Tx rejected : ")
                                                    + MvErrString(err));
    }
    
    return MakeCSendTransactionResultPtr(rawTx.GetHash().GetHex());
}

CRPCResultPtr CRPCModWorker::RPCGetForkHeight(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CGetForkHeightParam>(param);

    //getforkheight (-f="fork")
    uint256 hashFork;
    if (!GetForkHashOfDef(spParam->strFork, hashFork))
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Invalid fork");
    }
    
    if (!pService->HaveFork(hashFork))
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Unknown fork");
    }
    
    return MakeCGetForkHeightResultPtr(pService->GetForkHeight(hashFork));
}

/* Wallet */
CRPCResultPtr CRPCModWorker::RPCListKey(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CListKeyParam>(param);

    set<crypto::CPubKey> setPubKey;
    pService->GetPubKeys(setPubKey);

    auto spResult = MakeCListKeyResultPtr();
    for(const crypto::CPubKey& pubkey : setPubKey)
    {
        uint32 nVersion;
        bool fLocked;
        int64 nAutoLockTime;
        if (pService->GetKeyStatus(pubkey,nVersion,fLocked,nAutoLockTime))
        {
            CListKeyResult::CPubkey p;
            p.strKey = pubkey.GetHex();
            p.nVersion = nVersion;
            p.fLocked = fLocked;
            if (!fLocked && nAutoLockTime > 0)
            {
                p.nTimeout = (nAutoLockTime - GetTime());
            }
            spResult->vecPubkey.push_back(p);
        }
    } 
    return spResult;
}

CRPCResultPtr CRPCModWorker::RPCGetNewKey(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CGetNewKeyParam>(param);

    if (spParam->strPassphrase.empty())
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Passphrase must be nonempty");
    }
    crypto::CCryptoString strPassphrase = spParam->strPassphrase.c_str();
    crypto::CPubKey pubkey;
    if (!pService->MakeNewKey(strPassphrase,pubkey))
    {
        throw CRPCException(RPC_WALLET_ERROR,"Failed add new key.");
    }

    return MakeCGetNewKeyResultPtr(pubkey.ToString());
}

CRPCResultPtr CRPCModWorker::RPCEncryptKey(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CEncryptKeyParam>(param);

    //encryptkey <"pubkey"> <-new="passphrase"> <-old="oldpassphrase">
    crypto::CPubKey pubkey;
    pubkey.SetHex(spParam->strPubkey);

    if (spParam->strPassphrase.empty())
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Passphrase must be nonempty");
    }
    crypto::CCryptoString strPassphrase = spParam->strPassphrase.c_str();

    if (spParam->strOldpassphrase.empty())
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Old passphrase must be nonempty");
    }
    crypto::CCryptoString strOldPassphrase = spParam->strOldpassphrase.c_str();

    if (!pService->HaveKey(pubkey))
    {
        throw CRPCException(RPC_INVALID_ADDRESS_OR_KEY,"Unknown key");
    }
    if (!pService->EncryptKey(pubkey,strPassphrase,strOldPassphrase))
    {
        throw CRPCException(RPC_WALLET_PASSPHRASE_INCORRECT,"The passphrase entered was incorrect.");
    }

    return MakeCEncryptKeyResultPtr(string("Encrypt key successfully: ") + spParam->strPubkey);
}

CRPCResultPtr CRPCModWorker::RPCLockKey(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CLockKeyParam>(param);

    CMvAddress address(spParam->strPubkey);
    if(address.IsTemplate())
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "This method only accepts pubkey or pubkey address as parameter rather than template address you supplied.");
    }

    crypto::CPubKey pubkey;
    if(address.IsPubKey())
    {
        address.GetPubKey(pubkey);
    }
    else
    {
        pubkey.SetHex(spParam->strPubkey);
    }

    uint32 nVersion;
    bool fLocked;
    int64 nAutoLockTime; 
    if (!pService->GetKeyStatus(pubkey,nVersion,fLocked,nAutoLockTime))
    {
        throw CRPCException(RPC_INVALID_ADDRESS_OR_KEY,"Unknown key");
    }
    if (!fLocked && !pService->Lock(pubkey))
    {
        throw CRPCException(RPC_WALLET_ERROR,"Failed to lock key");
    }
    return MakeCLockKeyResultPtr(string("Lock key successfully: ") + spParam->strPubkey);
}

CRPCResultPtr CRPCModWorker::RPCUnlockKey(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CUnlockKeyParam>(param);

    CMvAddress address(spParam->strPubkey);
    if(address.IsTemplate())
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "This method only accepts pubkey or pubkey address as parameter rather than template address you supplied.");
    }
  
    crypto::CPubKey pubkey;
    if(address.IsPubKey())
    {
        address.GetPubKey(pubkey);
    }
    else
    {
        pubkey.SetHex(spParam->strPubkey);
    }

    if (spParam->strPassphrase.empty())
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Passphrase must be nonempty");
    }
        
    crypto::CCryptoString strPassphrase = spParam->strPassphrase.c_str();
    int64 nTimeout = 0;
    if (spParam->nTimeout.IsValid())
    {
         nTimeout = spParam->nTimeout;
    }

    uint32 nVersion;
    bool fLocked;
    int64 nAutoLockTime; 
    if (!pService->GetKeyStatus(pubkey,nVersion,fLocked,nAutoLockTime))
    {
        throw CRPCException(RPC_INVALID_ADDRESS_OR_KEY,"Unknown key");
    }

    if (!fLocked)
    {
        throw CRPCException(RPC_WALLET_ALREADY_UNLOCKED,"Key is already unlocked");
    }

    if (!pService->Unlock(pubkey,strPassphrase,nTimeout))
    {
        throw CRPCException(RPC_WALLET_PASSPHRASE_INCORRECT,"The passphrase entered was incorrect.");
    }
        
    return MakeCUnlockKeyResultPtr(string("Unlock key successfully: ") + spParam->strPubkey);
}

CRPCResultPtr CRPCModWorker::RPCImportPrivKey(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CImportPrivKeyParam>(param);

    //importprivkey <"privkey"> <"passphrase">
    uint256 nPriv;
    if (nPriv.SetHex(spParam->strPrivkey) != spParam->strPrivkey.size())
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Invalid private key");
    }

    if (spParam->strPassphrase.empty())
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Passphrase must be nonempty");
    }

    crypto::CCryptoString strPassphrase = spParam->strPassphrase.c_str();

    crypto::CKey key;
    if (!key.SetSecret(crypto::CCryptoKeyData(nPriv.begin(),nPriv.end())))
    {
        throw CRPCException(RPC_INVALID_PARAMETER,"Invalid private key");
    }
    if (pService->HaveKey(key.GetPubKey()))
    {
        throw CRPCException(RPC_WALLET_ERROR,"Already have key");
    }
    if (!strPassphrase.empty())
    {
        key.Encrypt(strPassphrase);
    }
    if (!pService->AddKey(key))
    {
        throw CRPCException(RPC_WALLET_ERROR,"Failed to add key");
    }
    if (!pService->SynchronizeWalletTx(CDestination(key.GetPubKey())))
    {
        throw CRPCException(RPC_WALLET_ERROR,"Failed to sync wallet tx");
    }

    return MakeCImportPrivKeyResultPtr(key.GetPubKey().GetHex());
}

CRPCResultPtr CRPCModWorker::RPCImportKey(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CImportKeyParam>(param);

    vector<unsigned char> vchKey = ParseHexString(spParam->strPubkey);
    crypto::CKey key;
    if (!key.Load(vchKey))
    {
        throw CRPCException(RPC_INVALID_PARAMS,"Failed to verify serialized key");
    }
    if (key.GetVersion() == 0)
    {
        throw CRPCException(RPC_INVALID_PARAMS, "Can't import the key with empty passphrase");
    }
    if (pService->HaveKey(key.GetPubKey()))
    {
        throw CRPCException(RPC_WALLET_ERROR,"Already have key");
    }
    
    if (!pService->AddKey(key))
    {
        throw CRPCException(RPC_WALLET_ERROR,"Failed to add key");
    }
    if (!pService->SynchronizeWalletTx(CDestination(key.GetPubKey())))
    {
        throw CRPCException(RPC_WALLET_ERROR,"Failed to sync wallet tx");
    }

    return MakeCImportKeyResultPtr(key.GetPubKey().GetHex());
}

CRPCResultPtr CRPCModWorker::RPCExportKey(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CExportKeyParam>(param);

    crypto::CPubKey pubkey;
    pubkey.SetHex(spParam->strPubkey);

    if (!pService->HaveKey(pubkey))
    {
        throw CRPCException(RPC_INVALID_ADDRESS_OR_KEY,"Unknown key");
    }
    vector<unsigned char> vchKey;
    if (!pService->ExportKey(pubkey,vchKey))
    {
        throw CRPCException(RPC_WALLET_ERROR,"Failed to export key");
    }

    return MakeCExportKeyResultPtr(ToHexString(vchKey));
}

CRPCResultPtr CRPCModWorker::RPCAddNewTemplate(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CAddNewTemplateParam>(param);
    CTemplatePtr ptr = CTemplate::CreateTemplatePtr(spParam->data, CMvAddress());
    if (ptr == NULL)
    {
        throw CRPCException(RPC_INVALID_PARAMETER,"Invalid parameters, failed to make template");
    }
    if (!pService->AddTemplate(ptr))
    {
        throw CRPCException(RPC_WALLET_ERROR,"Failed to add template");
    }
    if (!pService->SynchronizeWalletTx(CDestination(ptr->GetTemplateId())))
    {
        throw CRPCException(RPC_WALLET_ERROR,"Failed to sync wallet tx");
    }

    return MakeCAddNewTemplateResultPtr(CMvAddress(ptr->GetTemplateId()).ToString());
}

CRPCResultPtr CRPCModWorker::RPCImportTemplate(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CImportTemplateParam>(param);
    vector<unsigned char> vchTemplate = ParseHexString(spParam->strData);
    CTemplatePtr ptr = CTemplate::Import(vchTemplate);
    if (ptr == NULL)
    {
        throw CRPCException(RPC_INVALID_PARAMETER,"Invalid parameters,failed to make template");
    }
    if (pService->HaveTemplate(ptr->GetTemplateId()))
    {
        throw CRPCException(RPC_WALLET_ERROR,"Already have this template");
    }
    if (!pService->AddTemplate(ptr))
    {
        throw CRPCException(RPC_WALLET_ERROR,"Failed to add template");
    }
    if (!pService->SynchronizeWalletTx(CDestination(ptr->GetTemplateId())))
    {
        throw CRPCException(RPC_WALLET_ERROR,"Failed to sync wallet tx");
    }
    
    return MakeCImportTemplateResultPtr(CMvAddress(ptr->GetTemplateId()).ToString()); 
}

CRPCResultPtr CRPCModWorker::RPCExportTemplate(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CExportTemplateParam>(param);
    CMvAddress address(spParam->strAddress);
    if (address.IsNull())
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Invalid address");
    }

    CTemplateId tid = address.GetTemplateId();
    if (!tid)
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Invalid address");
    }

    CTemplatePtr ptr = pService->GetTemplate(tid);
    if (!ptr)
    {
        throw CRPCException(RPC_WALLET_ERROR,"Unkown template");
    }

    vector<unsigned char> vchTemplate = ptr->Export();
    return MakeCExportTemplateResultPtr(ToHexString(vchTemplate));
}

CRPCResultPtr CRPCModWorker::RPCValidateAddress(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CValidateAddressParam>(param);

    CMvAddress address(spParam->strAddress);
    bool isValid = !address.IsNull();

    auto spResult = MakeCValidateAddressResultPtr();
    spResult->fIsvalid = isValid;
    if (isValid)
    {
        auto& addressData = spResult->addressdata;

        addressData.strAddress = address.ToString();
        if (address.IsPubKey())
        {
            crypto::CPubKey pubkey;
            address.GetPubKey(pubkey);
            bool isMine = pService->HaveKey(pubkey);
            addressData.fIsmine = isMine;
            addressData.strType = "pubkey";
            addressData.strPubkey = pubkey.GetHex();
        }
        else if (address.IsTemplate())
        {
            CTemplateId tid = address.GetTemplateId();
            uint16 nType = tid.GetType();
            CTemplatePtr ptr = pService->GetTemplate(tid);
            addressData.fIsmine = (ptr != NULL);
            addressData.strType = "template";
            addressData.strTemplate = CTemplate::GetTypeName(nType);
            if (ptr)
            {
                auto& templateData = addressData.templatedata;

                templateData.strHex = ToHexString(ptr->Export());
                templateData.strType = ptr->GetName();
                ptr->GetTemplateData(templateData, CMvAddress());
            }
        }
    }
    return spResult;
}

CRPCResultPtr CRPCModWorker::RPCResyncWallet(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CResyncWalletParam>(param);
    if (spParam->strAddress.IsValid())
    {
        CMvAddress address(spParam->strAddress);
        if (address.IsNull())
        {
            throw CRPCException(RPC_INVALID_PARAMETER, "Invalid address");
        }
        if (!pService->SynchronizeWalletTx(static_cast<CDestination&>(address)))
        {
            throw CRPCException(RPC_WALLET_ERROR, "Failed to resync wallet tx");
        }
    }
    else
    {
        if (!pService->ResynchronizeWalletTx())
        {
            throw CRPCException(RPC_WALLET_ERROR, "Failed to resync wallet tx");
        }
    }
    return MakeCResyncWalletResultPtr("Resync wallet successfully.");
}

CRPCResultPtr CRPCModWorker::RPCGetBalance(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CGetBalanceParam>(param);

    //getbalance (-f="fork") (-a="address")
    uint256 hashFork;
    if (!GetForkHashOfDef(spParam->strFork, hashFork))
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Invalid fork");
    }
    
    if (!pService->HaveFork(hashFork))
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Unknown fork");
    }
    
    vector<CDestination> vDest;
    if (spParam->strAddress.IsValid())
    {
        CMvAddress address(spParam->strAddress);
        if (address.IsNull())
        {
            throw CRPCException(RPC_INVALID_PARAMETER, "Invalid address");
        }
        vDest.push_back(static_cast<CDestination&>(address));
    }
    else 
    {
        ListDestination(vDest);
    }

    auto spResult = MakeCGetBalanceResultPtr();
    for(const CDestination& dest : vDest)
    {
        CWalletBalance balance;
        if (pService->GetBalance(dest,hashFork,balance))
        {
            CGetBalanceResult::CBalance b;
            b.strAddress = CMvAddress(dest).ToString();
            b.fAvail = ValueFromAmount(balance.nAvailable);
            b.fLocked = ValueFromAmount(balance.nLocked);
            b.fUnconfirmed = ValueFromAmount(balance.nUnconfirmed);
            spResult->vecBalance.push_back(b);
        }
    }

    return spResult;
}

CRPCResultPtr CRPCModWorker::RPCListTransaction(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CListTransactionParam>(param);

    int nCount = GetUint(spParam->nCount, 10);
    int nOffset = GetInt(spParam->nOffset, 0);
    if (nCount <= 0)
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Negative, zero or out of range count");
    }
    
    vector<CWalletTx> vWalletTx;
    if (!pService->ListWalletTx(nOffset,nCount,vWalletTx))
    {
        throw CRPCException(RPC_WALLET_ERROR, "Failed to list transactions");
    }

    auto spResult = MakeCListTransactionResultPtr();
    for(const CWalletTx& wtx : vWalletTx)
    {
        spResult->vecTransaction.push_back(WalletTxToJSON(wtx));
    }
    return spResult;
}

CRPCResultPtr CRPCModWorker::RPCSendFrom(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CSendFromParam>(param);

    //sendfrom <"from"> <"to"> <$amount$> ($txfee$) (-f="fork") (-d="data")
    CMvAddress from(spParam->strFrom);
    if (from.IsNull())
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Invalid from address");
    }

    CMvAddress to(spParam->strTo);
    if (to.IsNull())
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Invalid to address");
    }

    int64 nAmount = AmountFromValue(spParam->fAmount);
    
    int64 nTxFee = MIN_TX_FEE;
    if (spParam->fTxfee.IsValid())
    {
        nTxFee = AmountFromValue(spParam->fTxfee);
        if (nTxFee < MIN_TX_FEE)
        {
            nTxFee = MIN_TX_FEE;
        }
    }
    
    uint256 hashFork;
    if (!GetForkHashOfDef(spParam->strFork, hashFork))
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Invalid fork");
    }
    
    if (!pService->HaveFork(hashFork))
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Unknown fork");
    }
    
    vector<unsigned char> vchData;
    if (spParam->strData.IsValid())
    {
        vchData = ParseHexString(spParam->strData);
    }

    // locked by from destination
    CTransaction txNew;
    {
        CDestForkLock lock(CDestFork{from, hashFork}, destForkMutex, mapDestMutex);

        if (!pService->CreateTransaction(hashFork,from,to,nAmount,nTxFee,vchData,txNew))
        {
            throw CRPCException(RPC_WALLET_ERROR,"Failed to create transaction");
        }
        bool fCompleted = false;
        if (!pService->SignTransaction(txNew,fCompleted))
        {
            throw CRPCException(RPC_WALLET_ERROR,"Failed to sign transaction");
        }
        if (!fCompleted)
        {
            throw CRPCException(RPC_WALLET_ERROR,"The signature is not completed");
        }
        MvErr err = pService->SendTransaction(txNew);
        if (err != MV_OK)
        {
            throw CRPCException(RPC_TRANSACTION_REJECTED,string("Tx rejected : ")
                                                        + MvErrString(err));
        }
    }

    return MakeCSendFromResultPtr(txNew.GetHash().GetHex());
}

CRPCResultPtr CRPCModWorker::RPCCreateTransaction(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CCreateTransactionParam>(param);

    //createtransaction <"from"> <"to"> <$amount$> ($txfee$) (-f="fork") (-d="data")
    CMvAddress from(spParam->strFrom);
    if (from.IsNull())
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Invalid from address");
    }

    CMvAddress to(spParam->strTo);
    if (to.IsNull())
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Invalid to address");
    }

    int64 nAmount = AmountFromValue(spParam->fAmount);
    
    int64 nTxFee = MIN_TX_FEE;
    if (spParam->fTxfee.IsValid())
    {
        nTxFee = AmountFromValue(spParam->fTxfee);
        if (nTxFee < MIN_TX_FEE)
        {
            nTxFee = MIN_TX_FEE;
        }
    }

    uint256 hashFork;
    if (!GetForkHashOfDef(spParam->strFork, hashFork))
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Invalid fork");
    }
    
    if (!pService->HaveFork(hashFork))
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Unknown fork");
    }
    
    vector<unsigned char> vchData;
    if (spParam->strData.IsValid())
    {
        vchData = ParseHexString(spParam->strData);
    }
    CTransaction txNew;
    if (!pService->CreateTransaction(hashFork,from,to,nAmount,nTxFee,vchData,txNew))
    {
        throw CRPCException(RPC_WALLET_ERROR,"Failed to create transaction");
    }

    CWalleveBufStream ss;
    ss << txNew;

    return MakeCCreateTransactionResultPtr(
        ToHexString((const unsigned char*)ss.GetData(),ss.GetSize()));
}

CRPCResultPtr CRPCModWorker::RPCSignTransaction(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CSignTransactionParam>(param);

    vector<unsigned char> txData = ParseHexString(spParam->strTxdata);
    CWalleveBufStream ss;
    ss.Write((char *)&txData[0],txData.size());
    CTransaction rawTx;
    try
    {
        ss >> rawTx;
    }
    catch (const std::exception &e)
    {
        throw CRPCException(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    }

    bool fCompleted = false;
    if (!pService->SignTransaction(rawTx,fCompleted))
    {
        throw CRPCException(RPC_WALLET_ERROR,"Failed to sign transaction");
    }

    CWalleveBufStream ssNew;
    ssNew << rawTx;

    auto spResult = MakeCSignTransactionResultPtr();
    spResult->strHex = ToHexString((const unsigned char*)ssNew.GetData(),ssNew.GetSize());
    spResult->fCompleted = fCompleted;
    return spResult;
}

CRPCResultPtr CRPCModWorker::RPCSignMessage(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CSignMessageParam>(param);

    crypto::CPubKey pubkey;
    pubkey.SetHex(spParam->strPubkey);

    string strMessage = spParam->strMessage;

    uint32 nVersion;
    bool fLocked;
    int64 nAutoLockTime; 
    if (!pService->GetKeyStatus(pubkey,nVersion,fLocked,nAutoLockTime))
    {
        throw CRPCException(RPC_INVALID_ADDRESS_OR_KEY,"Unknown key");
    }
    if (fLocked)
    {
        throw CRPCException(RPC_WALLET_UNLOCK_NEEDED,"Key is locked");
    }
    
    const string strMessageMagic = "Multiverse Signed Message:\n";
    CWalleveBufStream ss;
    ss << strMessageMagic;
    ss << strMessage;
    vector<unsigned char> vchSig;
    if (!pService->SignSignature(pubkey,crypto::CryptoHash(ss.GetData(),ss.GetSize()),vchSig))
    {
        throw CRPCException(RPC_WALLET_ERROR,"Failed to sign message");
    }

    return MakeCSignMessageResultPtr(ToHexString(vchSig));
}

CRPCResultPtr CRPCModWorker::RPCListAddress(CRPCParamPtr param)
{
    auto spResult = MakeCListAddressResultPtr();
    vector<CDestination> vDes;
    ListDestination(vDes);
    for(const auto& des : vDes)
    {
        CListAddressResult::CAddressdata addressData;
        addressData.strAddress = CMvAddress(des).ToString();
        if(des.IsPubKey())
        {
            addressData.strType = "pubkey";
            crypto::CPubKey pubkey;
            des.GetPubKey(pubkey);
            addressData.strPubkey = pubkey.GetHex();
        }
        else if(des.IsTemplate())
        {
            addressData.strType = "template";

            CTemplateId tid = des.GetTemplateId();
            uint16 nType = tid.GetType();
            CTemplatePtr ptr = pService->GetTemplate(tid);
            addressData.strTemplate = CTemplate::GetTypeName(nType);

            auto& templateData = addressData.templatedata;
            templateData.strHex = ToHexString(ptr->Export());
            templateData.strType = ptr->GetName();
            ptr->GetTemplateData(templateData, CMvAddress());
        }
        else
        {
            continue;
        }
        spResult->vecAddressdata.push_back(addressData);
    }

    return spResult;
}

CRPCResultPtr CRPCModWorker::RPCExportWallet(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CExportWalletParam>(param);

    fs::path pSave(string(spParam->strPath));
    //check if the file name given is available
    if(!pSave.is_absolute())
    {
        throw CRPCException(RPC_WALLET_ERROR, "Must be an absolute path.");
    }
    if(is_directory(pSave))
    {
        throw CRPCException(RPC_WALLET_ERROR, "Cannot export to a folder.");
    }
    if(exists(pSave))
    {
        throw CRPCException(RPC_WALLET_ERROR, "File has been existed.");
    }
    if(pSave.filename() == "." || pSave.filename() == "..")
    {
        throw CRPCException(RPC_WALLET_ERROR, "Cannot export to a folder.");
    }

    if(!exists(pSave.parent_path()) && !create_directories(pSave.parent_path()))
    {
        throw CRPCException(RPC_WALLET_ERROR, "Failed to create directories.");
    }

    Array aAddr;
    vector<CDestination> vDes;
    ListDestination(vDes);
    for(const auto& des : vDes)
    {
        if (des.IsPubKey())
        {
            Object oKey;
            oKey.push_back(Pair("address", CMvAddress(des).ToString()));

            crypto::CPubKey pubkey;
            des.GetPubKey(pubkey);
            vector<unsigned char> vchKey;
            if (!pService->ExportKey(pubkey, vchKey))
            {
                throw CRPCException(RPC_WALLET_ERROR, "Failed to export key");
            }
            oKey.push_back(Pair("hex", ToHexString(vchKey)));
            aAddr.push_back(oKey);
        }

        if (des.IsTemplate())
        {
            Object oTemp;
            CMvAddress address(des);

            oTemp.push_back(Pair("address", address.ToString()));

            CTemplateId tid;
            if (!address.GetTemplateId(tid))
            {
                throw CRPCException(RPC_INVALID_PARAMETER, "Invalid template address");
            }
            CTemplatePtr ptr = pService->GetTemplate(tid);
            if (!ptr)
            {
                throw CRPCException(RPC_WALLET_ERROR, "Unkown template");
            }
            vector<unsigned char> vchTemplate = ptr->Export();

            oTemp.push_back(Pair("hex", ToHexString(vchTemplate)));

            aAddr.push_back(oTemp);
        }
    }
    //output them together to file
    try
    {
        std::ofstream ofs(pSave.string(), std::ios::out);
        if (!ofs)
        {
            throw runtime_error("write error");
        }

        write_stream(Value(aAddr), ofs, pretty_print);
        ofs.close();
    }
    catch(...)
    {
        throw CRPCException(RPC_WALLET_ERROR, "filesystem_error - failed to write.");
    }

    return MakeCExportWalletResultPtr(string("Wallet file has been saved at: ") + pSave.string());
}

CRPCResultPtr CRPCModWorker::RPCImportWallet(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CImportWalletParam>(param);

    fs::path pLoad(string(spParam->strPath));
    //check if the file name given is available
    if(!pLoad.is_absolute())
    {
        throw CRPCException(RPC_WALLET_ERROR, "Must be an absolute path.");
    }
    if(!exists(pLoad) || is_directory(pLoad))
    {
        throw CRPCException(RPC_WALLET_ERROR, "File name is invalid.");
    }

    Value vWallet;
    try
    {
        fs::ifstream ifs(pLoad);
        if (!ifs)
        {
            throw runtime_error("read error");
        }

        read_stream(ifs, vWallet);
        ifs.close();
    }
    catch(...)
    {
        throw CRPCException(RPC_WALLET_ERROR, "Filesystem_error - failed to read.");
    }

    if(array_type != vWallet.type())
    {
        throw CRPCException(RPC_WALLET_ERROR, "Wallet file exported is invalid, check it and try again.");
    }

    Array aAddr;
    uint32 nKey = 0;
    uint32 nTemp = 0;
    for(const auto& oAddr : vWallet.get_array())
    {
        if(oAddr.get_obj()[0].name_ != "address" || oAddr.get_obj()[1].name_ != "hex")
        {
            throw CRPCException(RPC_WALLET_ERROR, "Data format is not correct, check it and try again.");
        }
        string sAddr = oAddr.get_obj()[0].value_.get_str(); //"address" field
        string sHex = oAddr.get_obj()[1].value_.get_str();  //"hex" field

        CMvAddress addr(sAddr);
        if (addr.IsNull())
        {
            throw CRPCException(RPC_WALLET_ERROR, "Data format is not correct, check it and try again.");
        }

        //import keys
        if(addr.IsPubKey())
        {
            vector<unsigned char> vchKey = ParseHexString(sHex);
            crypto::CKey key;
            if (!key.Load(vchKey))
            {
                throw CRPCException(RPC_INVALID_PARAMS, "Failed to verify serialized key");
            }
            if (key.GetVersion() == 0)
            {
                throw CRPCException(RPC_INVALID_PARAMS, "Can't import the key with empty passphrase");
            }
            if (pService->HaveKey(key.GetPubKey()))
            {
                continue;   //step to next one to continue importing
            }
            if (!pService->AddKey(key))
            {
                throw CRPCException(RPC_WALLET_ERROR, "Failed to add key");
            }
            if (!pService->SynchronizeWalletTx(CDestination(key.GetPubKey())))
            {
                throw CRPCException(RPC_WALLET_ERROR, "Failed to sync wallet tx");
            }
            aAddr.push_back(key.GetPubKey().GetHex());
            ++nKey;
        }

        //import templates
        if(addr.IsTemplate())
        {
            vector<unsigned char> vchTemplate = ParseHexString(sHex);
            CTemplatePtr ptr = CTemplate::Import(vchTemplate);
            if (ptr == NULL)
            {
                throw CRPCException(RPC_INVALID_PARAMETER,"Invalid parameters,failed to make template");
            }
            if (pService->HaveTemplate(addr.GetTemplateId()))
            {
                continue;   //step to next one to continue importing
            }
            if (!pService->AddTemplate(ptr))
            {
                throw CRPCException(RPC_WALLET_ERROR,"Failed to add template");
            }
            if (!pService->SynchronizeWalletTx(CDestination(ptr->GetTemplateId())))
            {
                throw CRPCException(RPC_WALLET_ERROR,"Failed to sync wallet tx");
            }
            aAddr.push_back(CMvAddress(ptr->GetTemplateId()).ToString());
            ++nTemp;
        }
    }

    return MakeCImportWalletResultPtr(string("Imported ") + std::to_string(nKey)
                + string(" keys and ") + std::to_string(nTemp) + string(" templates."));
}

CRPCResultPtr CRPCModWorker::RPCMakeOrigin(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CMakeOriginParam>(param);
    
    //makeorigin <"prev"> <"owner"> <$amount$> <"name"> <"symbol"> <$reward$> (-i|-noi*isolated*) (-p|-nop*private*) (-e|-noe*enclosed*)
    uint256 hashPrev;
    hashPrev.SetHex(spParam->strPrev);

    CDestination destOwner = static_cast<CDestination>(CMvAddress(spParam->strOwner));
    if (destOwner.IsNull())
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Invalid owner");
    }
	
    int64 nAmount = AmountFromValue(spParam->fAmount);
    int64 nMintReward = AmountFromValue(spParam->fReward);

    if (spParam->strName.empty() || spParam->strName.size() > 128
        || spParam->strSymbol.empty() || spParam->strSymbol.size() > 16)
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Invalid name or symbol");
    }
 
    CBlock blockPrev;
    uint256 hashParent;
    int32 nJointHeight;
    if (!pService->GetBlock(hashPrev,blockPrev,hashParent,nJointHeight))
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Unknown prev block");
    }

    if (blockPrev.IsExtended() || blockPrev.IsVacant())
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Prev block should not be extended/vacant block");
    }

    CProfile profile;
    profile.strName      = spParam->strName;
    profile.strSymbol    = spParam->strSymbol;
    profile.destOwner    = destOwner;
    profile.hashParent   = hashParent; 
    profile.nJointHeight = nJointHeight; 
    profile.nMintReward  = nMintReward;
    profile.nMinTxFee    = MIN_TX_FEE;
    profile.SetFlag(spParam->fIsolated,spParam->fPrivate,spParam->fEnclosed);

    CBlock block;
    block.nVersion   = 1;
    block.nType      = CBlock::BLOCK_ORIGIN;
    block.nTimeStamp = blockPrev.nTimeStamp + BLOCK_TARGET_SPACING;
    block.hashPrev   = hashPrev;
    profile.Save(block.vchProof);

    CTransaction& tx = block.txMint;
    tx.nType         = CTransaction::TX_GENESIS;
    tx.nTimeStamp    = block.nTimeStamp;
    tx.sendTo        = destOwner;
    tx.nAmount       = nAmount;
    tx.vchData.assign(profile.strName.begin(),profile.strName.end());

    crypto::CPubKey pubkey;
    if (!destOwner.GetPubKey(pubkey))
    {
        throw CRPCException(RPC_INVALID_ADDRESS_OR_KEY,"Owner' address should be pubkey address");
    }

    uint32 nVersion;
    bool fLocked;
    int64 nAutoLockTime; 
    if (!pService->GetKeyStatus(pubkey,nVersion,fLocked,nAutoLockTime))
    {
        throw CRPCException(RPC_INVALID_ADDRESS_OR_KEY,"Unknown key");
    }
    if (fLocked)
    {
        throw CRPCException(RPC_WALLET_UNLOCK_NEEDED,"Key is locked");
    }

    uint256 hashBlock = block.GetHash();
    if (!pService->SignSignature(pubkey,hashBlock,block.vchSig))
    {
        throw CRPCException(RPC_WALLET_ERROR,"Failed to sign message");
    }
 
    CWalleveBufStream ss;
    ss << block;
    
    auto spResult = MakeCMakeOriginResultPtr();
    spResult->strHash = hashBlock.GetHex();
    spResult->strHex = ToHexString((const unsigned char*)ss.GetData(),ss.GetSize());

    return spResult;
}

/* Util */
CRPCResultPtr CRPCModWorker::RPCVerifyMessage(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CVerifyMessageParam>(param);

    //verifymessage <"pubkey"> <"message"> <"sig">
    crypto::CPubKey pubkey;
    if (pubkey.SetHex(spParam->strPubkey) != spParam->strPubkey.size())
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Invalid pubkey");
    }

    string strMessage = spParam->strMessage;

    if (spParam->strSig.empty())
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Invalid sig");
    }
    vector<unsigned char> vchSig = ParseHexString(spParam->strSig);
    if (vchSig.size() == 0)
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Invalid sig");
    }

    const string strMessageMagic = "Multiverse Signed Message:\n";
    CWalleveBufStream ss;
    ss << strMessageMagic;
    ss << strMessage;
    
    return MakeCVerifyMessageResultPtr(
        pubkey.Verify(crypto::CryptoHash(ss.GetData(),ss.GetSize()),vchSig));
}

CRPCResultPtr CRPCModWorker::RPCMakeKeyPair(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CMakeKeyPairParam>(param);

    crypto::CCryptoKey key;
    crypto::CryptoMakeNewKey(key);
    
    auto spResult = MakeCMakeKeyPairResultPtr();
    spResult->strPrivkey = key.secret.GetHex();
    spResult->strPubkey = key.pubkey.GetHex();
    return spResult;
}

CRPCResultPtr CRPCModWorker::RPCGetPubKeyAddress(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CGetPubkeyAddressParam>(param);
    crypto::CPubKey pubkey;
    if (pubkey.SetHex(spParam->strPubkey) != spParam->strPubkey.size())
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Invalid pubkey");
    }

    CDestination dest(pubkey);

    return MakeCGetPubkeyAddressResultPtr(CMvAddress(dest).ToString());
}

CRPCResultPtr CRPCModWorker::RPCGetTemplateAddress(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CGetTemplateAddressParam>(param);
    CTemplateId tid;
    if (tid.SetHex(spParam->strTid) != spParam->strTid.size())
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Invalid tid");
    }

    CDestination dest(tid);

    return MakeCGetTemplateAddressResultPtr(CMvAddress(dest).ToString());
}

CRPCResultPtr CRPCModWorker::RPCMakeTemplate(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CMakeTemplateParam>(param);
    CTemplatePtr ptr = CTemplate::CreateTemplatePtr(spParam->data, CMvAddress());
    if (ptr == NULL)
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Invalid parameters,failed to make template");
    }

    auto spResult = MakeCMakeTemplateResultPtr();
    vector<unsigned char> vchTemplate = ptr->Export();
    spResult->strHex = ToHexString(vchTemplate);
    spResult->strAddress = CMvAddress(ptr->GetTemplateId()).ToString();
    return spResult;
}

CRPCResultPtr CRPCModWorker::RPCDecodeTransaction(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CDecodeTransactionParam>(param);
    vector<unsigned char> txData(ParseHexString(spParam->strTxdata));
    CWalleveBufStream ss;
    ss.Write((char *)&txData[0],txData.size());
    CTransaction rawTx;
    try
    {
        ss >> rawTx;
    }
    catch (const std::exception &e)
    {
        throw CRPCException(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    }
    
    uint256 hashFork;
    int32 nHeight;
    if (!pService->GetBlockLocation(rawTx.hashAnchor,hashFork,nHeight))
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Unknown anchor block");
    }

    return MakeCDecodeTransactionResultPtr(TxToJSON(rawTx.GetHash(),rawTx,hashFork,-1));
}


// /* Mint */
CRPCResultPtr CRPCModWorker::RPCGetWork(CRPCParamPtr param)
{
    //getwork <"spent"> <"privkey"> ("prev")
    auto spParam = CastParamPtr<CGetWorkParam>(param);

    CMvAddress addrSpent(spParam->strSpent);
    uint256 nPriv(spParam->strPrivkey);
    if (addrSpent.IsNull() || !addrSpent.IsPubKey())
    {
        throw CRPCException(RPC_INVALID_ADDRESS_OR_KEY,"Invalid spent address");
    }
    crypto::CKey key;
    if (!key.SetSecret(crypto::CCryptoKeyData(nPriv.begin(),nPriv.end())))
    {
        throw CRPCException(RPC_INVALID_ADDRESS_OR_KEY,"Invalid private key");
    }
    crypto::CPubKey pubkeySpent;
    if (addrSpent.GetPubKey(pubkeySpent) && pubkeySpent == key.GetPubKey())
    {
        throw CRPCException(RPC_INVALID_ADDRESS_OR_KEY,"Invalid spent address or private key");
    }
    CTemplateMintPtr ptr = CTemplateMint::CreateTemplatePtr(new CTemplateProof(key.GetPubKey(),static_cast<CDestination&>(addrSpent)));
    if (ptr == NULL)
    {
        throw CRPCException(RPC_INVALID_ADDRESS_OR_KEY,"Invalid mint template");
    }

    auto spResult = MakeCGetWorkResultPtr();

    vector<unsigned char> vchWorkData;
    uint256 hashPrev;
    uint32 nPrevTime;
    int nAlgo,nBits;
    if (!pService->GetWork(vchWorkData,hashPrev,nPrevTime,nAlgo,nBits,ptr))
    {
        spResult->fResult = false;
        return spResult;
    }

    spResult->fResult = true;

    spResult->work.strPrevblockhash = hashPrev.GetHex();
    spResult->work.nPrevblocktime = nPrevTime;
    spResult->work.nAlgo = nAlgo;
    spResult->work.nBits = nBits;
    spResult->work.strData = ToHexString(vchWorkData);

    return spResult;
}

CRPCResultPtr CRPCModWorker::RPCSubmitWork(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CSubmitWorkParam>(param);
    vector<unsigned char> vchWorkData(ParseHexString(spParam->strData));
    CMvAddress addrSpent(spParam->strSpent);
    uint256 nPriv(spParam->strPrivkey);
    if (addrSpent.IsNull() || !addrSpent.IsPubKey())
    {
        throw CRPCException(RPC_INVALID_ADDRESS_OR_KEY,"Invalid spent address");
    }
    crypto::CKey key;
    if (!key.SetSecret(crypto::CCryptoKeyData(nPriv.begin(),nPriv.end())))
    {
        throw CRPCException(RPC_INVALID_ADDRESS_OR_KEY,"Invalid private key");
    }
    
    CTemplateMintPtr ptr = CTemplateMint::CreateTemplatePtr(new CTemplateProof(key.GetPubKey(),static_cast<CDestination&>(addrSpent)));
    if (ptr == NULL)
    {
        throw CRPCException(RPC_INVALID_ADDRESS_OR_KEY,"Invalid mint template");
    }
    uint256 hashBlock;
    MvErr err = pService->SubmitWork(vchWorkData,ptr,key,hashBlock);
    if (err != MV_OK)
    {
        throw CRPCException(RPC_INVALID_PARAMETER,string("Block rejected : ") + MvErrString(err));
    }

    return MakeCSubmitWorkResultPtr(hashBlock.GetHex());
}

CRPCResultPtr CRPCModWorker::SnRPCStop(CRPCParamPtr param)
{
    (void)param;
    return NULL;
}

CRPCResultPtr CRPCModWorker::SnRPCGetForkCount(CRPCParamPtr param)
{
    (void)param;
    return NULL;
}

CRPCResultPtr CRPCModWorker::SnRPCListFork(CRPCParamPtr param)
{
    (void)param;
    return NULL;
}

CRPCResultPtr CRPCModWorker::SnRPCGetBlockLocation(CRPCParamPtr param)
{
    (void)param;
    return NULL;
}

CRPCResultPtr CRPCModWorker::SnRPCGetBlockCount(CRPCParamPtr param)
{
    (void)param;
    return NULL;
}

CRPCResultPtr CRPCModWorker::SnRPCGetBlockHash(CRPCParamPtr param)
{
    (void)param;
    return NULL;
}

CRPCResultPtr CRPCModWorker::SnRPCGetBlock(CRPCParamPtr param)
{
    (void)param;
    return NULL;
}

CRPCResultPtr CRPCModWorker::SnRPCGetTxPool(CRPCParamPtr param)
{
    (void)param;
    return NULL;
}

CRPCResultPtr CRPCModWorker::SnRPCGetTransaction(CRPCParamPtr param)
{
    (void)param;
    return NULL;
}

CRPCResultPtr CRPCModWorker::SnRPCGetForkHeight(CRPCParamPtr param)
{
    (void)param;
    return NULL;
}

CRPCResultPtr CRPCModWorker::SnRPCSendTransaction(CRPCParamPtr param)
{
    (void)param;
    return NULL;
}

///////////////////////////////
// CSnRPCModWorker

CSnRPCModWorker::CSnRPCModWorker(const uint nThreadIn, const bool fAffinity)
: CRPCModWorker(nThreadIn, fAffinity)
{
    mapRPCFunc["stop"] = &CRPCModWorker::SnRPCStop;
    mapRPCFunc["getforkcount"] = &CRPCModWorker::SnRPCGetForkCount;
    mapRPCFunc["listfork"] = &CRPCModWorker::SnRPCListFork;
    mapRPCFunc["getblocklocation"] = &CRPCModWorker::SnRPCGetBlockLocation;
    mapRPCFunc["getblockcount"] = &CRPCModWorker::SnRPCGetBlockCount;
    mapRPCFunc["getblockhash"] = &CRPCModWorker::SnRPCGetBlockHash;
    mapRPCFunc["getblock"] = &CRPCModWorker::SnRPCGetBlock;
    mapRPCFunc["gettxpool"] = &CRPCModWorker::SnRPCGetTxPool;
    mapRPCFunc["gettransaction"] = &CRPCModWorker::SnRPCGetTransaction;
    mapRPCFunc["getforkheight"] = &CRPCModWorker::SnRPCGetForkHeight;
    mapRPCFunc["sendtransaction"] = &CRPCModWorker::SnRPCSendTransaction;
}

CSnRPCModWorker::~CSnRPCModWorker()
{
}

bool CSnRPCModWorker::WalleveHandleInitialize()
{
    CRPCModWorker::WalleveHandleInitialize();

    if (!WalleveGetObject("dbpservice", pDbpService))
    {
        WalleveLog("Failed to request DBP service\n");
        return false;
    }

    return true;
}

void CSnRPCModWorker::WalleveHandleDeinitialize()
{
    CRPCModWorker::WalleveHandleDeinitialize();
    pDbpService = NULL;
}

uint64 CSnRPCModWorker::GenNonce()
{
    uint64 nNonce;
    RAND_bytes((unsigned char*)&nNonce, sizeof(nNonce));
    while(nNonce <= 0xFF || IsSuperNodeInnerNonce(nNonce))
    {
        RAND_bytes((unsigned char*)&nNonce, sizeof(nNonce));
    }
    return nNonce;
}

void CSnRPCModWorker::DelCompltUntilByNonce(uint64 nNonce)
{
    CMvEventRPCRouteDelCompltUntil * pEvent = new CMvEventRPCRouteDelCompltUntil("");
    pEvent->data.nNonce = nNonce;
    if(!pEvent)
    {
        return;
    }
    pDbpService->PostEvent(pEvent);
}

CRPCResultPtr CSnRPCModWorker::SnRPCStop(CRPCParamPtr param)
{
    CMvEventRPCRouteStop* pEvent = new CMvEventRPCRouteStop("");
    uint64 nNonce = GenNonce();
    auto ptrCompltUntil =
      std::make_shared<walleve::CIOCompletionUntil>(200 * 1000);
    ptrCompltUntil->Reset();

    pEvent->data.nNonce = nNonce;
    pEvent->data.spIoCompltUntil = ptrCompltUntil;
    if(!pEvent)
    {
        return NULL;
    }
    pDbpService->PostEvent(pEvent);

    bool fResult = false;
    ptrCompltUntil->WaitForComplete(fResult);
    DelCompltUntilByNonce(nNonce);
    if(!fResult)
    {
        throw CRPCException(RPC_REQUEST_TIMEOUT, "Timeout");
    }

    std::string reason = "[supernode]multiverse server stopping";
    return MakeCStopResultPtr(reason);
}

CRPCResultPtr CSnRPCModWorker::SnRPCGetForkCount(CRPCParamPtr param)
{
    CMvEventRPCRouteListFork* pEvent = new CMvEventRPCRouteListFork("");
    uint64 nNonce = GenNonce();
    auto ptrCompltUntil =
      std::make_shared<walleve::CIOCompletionUntil>(200 * 1000);
    ptrCompltUntil->Reset();

    pEvent->data.spIoCompltUntil = ptrCompltUntil;
    pEvent->data.nNonce = nNonce;
    pEvent->data.fAll = false;
    if(!pEvent)
    {
        return NULL;
    }
    pDbpService->PostEvent(pEvent);

    bool fResult = false;
    ptrCompltUntil->WaitForComplete(fResult);
    DelCompltUntilByNonce(nNonce);
    if(!fResult)
    {
        throw CRPCException(RPC_REQUEST_TIMEOUT, "Timeout");
    }

    CMvRPCRouteListForkRet ret =
      boost::any_cast<CMvRPCRouteListForkRet>(ptrCompltUntil->obj);
    return MakeCGetForkCountResultPtr(ret.vFork.size());
}

CRPCResultPtr CSnRPCModWorker::SnRPCListFork(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CListForkParam>(param);
    CMvEventRPCRouteListFork* pEvent = new CMvEventRPCRouteListFork("");
    uint64 nNonce = GenNonce();
    auto ptrCompltUntil =
      std::make_shared<walleve::CIOCompletionUntil>(200 * 1000);
    ptrCompltUntil->Reset();

    pEvent->data.spIoCompltUntil = ptrCompltUntil;
    pEvent->data.nNonce = nNonce;
    pEvent->data.fAll = spParam->fAll;
    if(!pEvent)
    {
        return NULL;
    }
    pDbpService->PostEvent(pEvent);

    bool fResult = false;
    ptrCompltUntil->WaitForComplete(fResult);
    DelCompltUntilByNonce(nNonce);
    if(!fResult)
    {
        throw CRPCException(RPC_REQUEST_TIMEOUT, "Timeout");
    }

    CMvRPCRouteListForkRet ret =
      boost::any_cast<CMvRPCRouteListForkRet>(ptrCompltUntil->obj);
    auto vFork = ret.vFork;
    auto spResult = MakeCListForkResultPtr();
    for (size_t i = 0; i < vFork.size(); i++)
    {
        CMvRPCProfile& profile = vFork[i];
        spResult->vecProfile.push_back({ profile.strHex, profile.strName,
                                         profile.strSymbol, profile.fIsolated,
                                         profile.fPrivate, profile.fEnclosed,
                                         profile.address });
    }
    return spResult;
}

CRPCResultPtr CSnRPCModWorker::SnRPCGetBlockLocation(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CGetBlockLocationParam>(param);
    CMvEventRPCRouteGetBlockLocation* pEvent = new CMvEventRPCRouteGetBlockLocation("");
    uint64 nNonce = GenNonce();
    auto ptrCompltUntil =
      std::make_shared<walleve::CIOCompletionUntil>(200 * 1000);
    ptrCompltUntil->Reset();

    pEvent->data.spIoCompltUntil = ptrCompltUntil;
    pEvent->data.nNonce = nNonce;
    pEvent->data.strBlock = spParam->strBlock;
    if(!pEvent)
    {
        return NULL;
    }
    pDbpService->PostEvent(pEvent);

    bool fResult = false;
    ptrCompltUntil->WaitForComplete(fResult);
    DelCompltUntilByNonce(nNonce);
    if(!fResult)
    {
        throw CRPCException(RPC_REQUEST_TIMEOUT, "Timeout");
    }

    CMvRPCRouteGetBlockLocationRet ret =
      boost::any_cast<CMvRPCRouteGetBlockLocationRet>(ptrCompltUntil->obj);
    if (ret.strFork.empty())
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Unknown block");
    }

    auto spResult = MakeCGetBlockLocationResultPtr();
    spResult->strFork = ret.strFork;
    spResult->nHeight = ret.height;
    return spResult;
}

CRPCResultPtr CSnRPCModWorker::SnRPCGetBlockCount(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CGetBlockCountParam>(param);
    auto* pEvent = new CMvEventRPCRouteGetBlockCount("");
    uint64 nNonce = GenNonce();
    auto ptrCompltUntil =
      std::make_shared<walleve::CIOCompletionUntil>(200 * 1000);
    ptrCompltUntil->Reset();

    pEvent->data.nNonce = nNonce;
    pEvent->data.spIoCompltUntil = ptrCompltUntil;
    pEvent->data.strFork = spParam->strFork;
    if (!pEvent)
    {
        return NULL;
    }
    pDbpService->PostEvent(pEvent);

    bool fResult = false;
    ptrCompltUntil->WaitForComplete(fResult);
    DelCompltUntilByNonce(nNonce);
    if (!fResult)
    {
        throw CRPCException(RPC_REQUEST_TIMEOUT, "Timeout");
    }

    auto ret =
      boost::any_cast<CMvRPCRouteGetBlockCountRet>(ptrCompltUntil->obj);

    uint256 hashFork;
    int height = 0;
    if (ret.exception == 0)
    {
        height = ret.height;
    }
    else if (ret.exception == 1)
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Invalid fork");
    }
    else if (ret.exception == 2)
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Unknown fork");
    }
    return MakeCGetBlockCountResultPtr(height);
}

CRPCResultPtr CSnRPCModWorker::SnRPCGetBlockHash(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CGetBlockHashParam>(param);
    auto* pEvent = new CMvEventRPCRouteGetBlockHash("");
    uint64 nNonce = GenNonce();
    auto ptrCompltUntil =
      std::make_shared<walleve::CIOCompletionUntil>(200 * 1000);
    ptrCompltUntil->Reset();

    pEvent->data.spIoCompltUntil = ptrCompltUntil;
    pEvent->data.nNonce = nNonce;
    pEvent->data.height = spParam->nHeight;
    pEvent->data.strFork = spParam->strFork;
    if(!pEvent)
    {
        return NULL;
    }
    pDbpService->PostEvent(pEvent);

    bool fResult = false;
    ptrCompltUntil->WaitForComplete(fResult);
    DelCompltUntilByNonce(nNonce);
    if(!fResult)
    {
        throw CRPCException(RPC_REQUEST_TIMEOUT, "Timeout");
    }

    auto ret = boost::any_cast<CMvRPCRouteGetBlockHashRet>(ptrCompltUntil->obj);
    int height = 0;
    if (ret.exception == 0)
    {
    }
    else if (ret.exception == 1)
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Invalid fork");
    }
    else if (ret.exception == 2)
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Unknown fork");
    }
    else if (ret.exception == 3)
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Block number out of range.");
    }

    auto spResult = MakeCGetBlockHashResultPtr();
    for (const auto& hash: ret.vHash)
    {
        spResult->vecHash.push_back(hash);
    }
    return spResult;
}

CRPCResultPtr CSnRPCModWorker::SnRPCGetBlock(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CGetBlockParam>(param);
    auto* pEvent = new CMvEventRPCRouteGetBlock("");
    uint64 nNonce = GenNonce();
    auto ptrCompltUntil =
      std::make_shared<walleve::CIOCompletionUntil>(200 * 1000);
    ptrCompltUntil->Reset();

    pEvent->data.spIoCompltUntil = ptrCompltUntil;
    pEvent->data.nNonce = nNonce;
    pEvent->data.hash = spParam->strBlock;
    if (!pEvent)
    {
        return NULL;
    }
    pDbpService->PostEvent(pEvent);

    bool fResult = false;
    ptrCompltUntil->WaitForComplete(fResult);
    DelCompltUntilByNonce(nNonce);
    if(!fResult)
    {
        throw CRPCException(RPC_REQUEST_TIMEOUT, "Timeout");
    }

    auto ret = boost::any_cast<CMvRPCRouteGetBlockRet>(ptrCompltUntil->obj);
    if (ret.exception == 1)
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Unknown block");
    }

    uint256 fork;
    fork.SetHex(ret.strFork);
    return MakeCGetBlockResultPtr(BlockToJSON(ret.block.GetHash(), ret.block, fork, ret.height));
}

CRPCResultPtr CSnRPCModWorker::SnRPCGetTxPool(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CGetTxPoolParam>(param);
    bool fDetail = spParam->fDetail.IsValid() ? bool(spParam->fDetail) : false;
    auto* pEvent = new CMvEventRPCRouteGetTxPool("");
    uint64 nNonce = GenNonce();
    auto ptrCompltUntil =
      std::make_shared<walleve::CIOCompletionUntil>(200 * 1000);
    ptrCompltUntil->Reset();

    pEvent->data.spIoCompltUntil = ptrCompltUntil;
    pEvent->data.nNonce = nNonce;
    pEvent->data.strFork = spParam->strFork;
    pEvent->data.fDetail = fDetail;
    if (!pEvent)
    {
        return NULL;
    }
    pDbpService->PostEvent(pEvent);

    bool fResult = false;
    ptrCompltUntil->WaitForComplete(fResult);
    DelCompltUntilByNonce(nNonce);
    if(!fResult)
    {
        throw CRPCException(RPC_REQUEST_TIMEOUT, "Timeout");
    }

    auto spResult = MakeCGetTxPoolResultPtr();
    auto ret = boost::any_cast<CMvRPCRouteGetTxPoolRet>(ptrCompltUntil->obj);
    if (ret.exception == 0)
    {
    }
    else if (ret.exception == 1)
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Invalid fork");
    }
    else if (ret.exception == 2)
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Unknown fork");
    }

    if (!fDetail)
    {
        size_t nTotalSize = 0;
        for (std::size_t i = 0; i < ret.vTxPool.size(); i++)
        {
            nTotalSize += ret.vTxPool[i].second;
        }
        spResult->nCount = ret.vTxPool.size();
        spResult->nSize = nTotalSize;
    }
    else
    {
        for (std::size_t i = 0; i < ret.vTxPool.size(); i++)
        {
            spResult->vecList.push_back({ret.vTxPool[i].first, ret.vTxPool[i].second});
        }
    }
    return spResult;
}

CRPCResultPtr CSnRPCModWorker::SnRPCGetTransaction(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CGetTransactionParam>(param);
    auto* pEvent = new CMvEventRPCRouteGetTransaction("");
    uint64 nNonce = GenNonce();
    auto ptrCompltUntil =
      std::make_shared<walleve::CIOCompletionUntil>(200 * 1000);
    ptrCompltUntil->Reset();

    pEvent->data.spIoCompltUntil = ptrCompltUntil;
    pEvent->data.nNonce = nNonce;
    pEvent->data.strTxid = spParam->strTxid;
    if (!pEvent)
    {
        return NULL;
    }
    pDbpService->PostEvent(pEvent);

    bool fResult = false;
    ptrCompltUntil->WaitForComplete(fResult);
    DelCompltUntilByNonce(nNonce);
    if(!fResult)
    {
        throw CRPCException(RPC_REQUEST_TIMEOUT, "Timeout");
    }

    auto spResult = MakeCGetTransactionResultPtr();
    auto ret = boost::any_cast<CMvRPCRouteGetTransactionRet>(ptrCompltUntil->obj);
    if (ret.exception == 0)
    {
    }
    else if (ret.exception == 1)
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "No information available about transaction");
    }

    if (spParam->fSerialized)
    {
        CWalleveBufStream ss;
        ss << ret.tx;
        spResult->strSerialization = ToHexString((const unsigned char*)ss.GetData(),ss.GetSize());
        return spResult;
    }

    uint256 txid, hashFork;
    txid.SetHex(spParam->strTxid);
    hashFork.SetHex(ret.strFork);
    spResult->transaction = TxToJSON(txid, ret.tx, hashFork, ret.nDepth);
    return spResult;
}

CRPCResultPtr CSnRPCModWorker::SnRPCGetForkHeight(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CGetForkHeightParam>(param);
    auto* pEvent = new CMvEventRPCRouteGetForkHeight("");
    uint64 nNonce = GenNonce();
    auto ptrCompltUntil =
      std::make_shared<walleve::CIOCompletionUntil>(200 * 1000);
    ptrCompltUntil->Reset();

    pEvent->data.spIoCompltUntil = ptrCompltUntil;
    pEvent->data.nNonce = nNonce;
    pEvent->data.strFork = spParam->strFork;
    if (!pEvent)
    {
        return NULL;
    }
    pDbpService->PostEvent(pEvent);

    bool fResult = false;
    ptrCompltUntil->WaitForComplete(fResult);
    DelCompltUntilByNonce(nNonce);
    if(!fResult)
    {
        throw CRPCException(RPC_REQUEST_TIMEOUT, "Timeout");
    }

    auto ret =
      boost::any_cast<CMvRPCRouteGetForkHeightRet>(ptrCompltUntil->obj);
    if (ret.exception == 0)
    {
    }
    else if (ret.exception == 1)
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Invalid fork");
    }
    else if (ret.exception == 2)
    {
        throw CRPCException(RPC_INVALID_PARAMETER, "Unknown fork");
    }
    return MakeCGetForkHeightResultPtr(ret.height);
}

CRPCResultPtr CSnRPCModWorker::SnRPCSendTransaction(CRPCParamPtr param)
{
    auto spParam = CastParamPtr<CSendTransactionParam>(param);
    vector<unsigned char> txData = ParseHexString(spParam->strTxdata);
    CWalleveBufStream ss;
    ss.Write((char *)&txData[0],txData.size());
    CTransaction rawTx;
    try
    {
        ss >> rawTx;
    }
    catch (const std::exception &e)
    {
        throw CRPCException(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    }

    auto* pEvent = new CMvEventRPCRouteSendTransaction("");
    uint64 nNonce = GenNonce();
    auto ptrCompltUntil =
      std::make_shared<walleve::CIOCompletionUntil>(200 * 1000);
    ptrCompltUntil->Reset();

    pEvent->data.spIoCompltUntil = ptrCompltUntil;
    pEvent->data.nNonce = nNonce;
    pEvent->data.rawTx = rawTx;
    if (!pEvent)
    {
        return NULL;
    }
    pDbpService->PostEvent(pEvent);

    bool fResult = false;
    ptrCompltUntil->WaitForComplete(fResult);
    DelCompltUntilByNonce(nNonce);
    if(!fResult)
    {
        throw CRPCException(RPC_REQUEST_TIMEOUT, "Timeout");
    }

    auto ret =
      boost::any_cast<CMvRPCRouteSendTransactionRet>(ptrCompltUntil->obj);
    if (ret.exception == 0)
    {
    }
    else if (ret.exception == 1)
    {
        throw CRPCException(RPC_TRANSACTION_REJECTED,string("Tx rejected : ") + MvErrString((MvErr)ret.err));
    }
    return MakeCSendTransactionResultPtr(rawTx.GetHash().GetHex());
}
