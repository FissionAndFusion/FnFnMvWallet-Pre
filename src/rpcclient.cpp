// Copyright (c) 2017-2018 The Multiverse developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpcclient.h"

#include "json/json_spirit_reader_template.h"
#include "json/json_spirit_writer_template.h"
#include "json/json_spirit_utils.h"

#include <boost/regex.hpp>
#include <boost/algorithm/string.hpp>
#include <readline/readline.h>
#include <readline/history.h>

#include "version.h"

using namespace std;
using namespace multiverse;
using namespace walleve;
using namespace json_spirit;
using namespace multiverse::rpc;
using boost::asio::ip::tcp;

static void Initialize_ReadLine();
static void Uninitialize_ReadLine();
static void ReadlineCallback(char* line);
extern void MvShutdown();

static string LocalCommandUsage(string command = "")
{
    ostringstream oss;
    if (command == "")
    {
        oss << "Local Command:\n";
    }
    if (command == "" || command == "quit")
    {
        oss << "  quit\t\t\t\tQuits this console.(CTRL-D)\n";
    }
    return oss.str();
}

static CRPCClient* pClient = NULL;
static const char* prompt = "multiverse> ";

///////////////////////////////
// CRPCClient

CRPCClient::CRPCClient(bool fConsole)
: IIOModule("rpcclient"),
  ioStrand(ioService),
  inStream(ioService,::dup(STDIN_FILENO)),
  thrDispatch("rpcclient", boost::bind(fConsole ? &CRPCClient::LaunchConsole : &CRPCClient::LaunchCommand,this))
{
    nLastNonce = 0;
    pHttpGet = NULL;
}

CRPCClient::~CRPCClient()
{
}

bool CRPCClient::WalleveHandleInitialize()
{
    if (!WalleveGetObject("httpget",pHttpGet))
    {
        cerr << "Failed to request httpget\n";
        return false;
    }
    return true;
}

void CRPCClient::WalleveHandleDeinitialize()
{
    pHttpGet = NULL;
}

bool CRPCClient::WalleveHandleInvoke()
{
    if (!WalleveThreadDelayStart(thrDispatch))
    {
        return false;
    }
    pClient = this;
    return IIOModule::WalleveHandleInvoke();
}

void CRPCClient::WalleveHandleHalt()
{
    IIOModule::WalleveHandleHalt();

    pClient = NULL;
    if (!ioService.stopped())
    {
        ioService.stop();
    }

    if (thrDispatch.IsRunning())
    {
        CancelCommand();
        thrDispatch.Interrupt();
    }
    thrDispatch.Exit();
}

const CMvRPCClientConfig * CRPCClient::WalleveConfig()
{
    return dynamic_cast<const CMvRPCClientConfig *>(IWalleveBase::WalleveConfig());
}

bool CRPCClient::HandleEvent(CWalleveEventHttpGetRsp& event)
{
    try
    {
        CWalleveHttpRsp& rsp = event.data;

        if (rsp.nStatusCode < 0)
        {
            
            const char * strErr[] = {"","connect failed","invalid nonce","activate failed",
                                     "disconnected","no response","resolve failed",
                                     "internal failure","aborted"};
            throw runtime_error(rsp.nStatusCode >= HTTPGET_ABORTED ? 
                                strErr[-rsp.nStatusCode] : "unknown error");
        }
        if (rsp.nStatusCode == 401)
        {
            throw runtime_error("incorrect rpcuser or rpcpassword (authorization failed)");
        }
        else if (rsp.nStatusCode > 400 && rsp.nStatusCode != 404 && rsp.nStatusCode != 500)
        {
            ostringstream oss;
            oss << "server returned HTTP error " << rsp.nStatusCode;
            throw runtime_error(oss.str());
        }
        else if (rsp.strContent.empty())
        {
            throw runtime_error("no response from server");
        }

        // Parse reply
        if (WalleveConfig()->fDebug)
        {
            cout << "response: " << rsp.strContent;
        }

        auto spResp = DeserializeCRPCResp("", rsp.strContent);
        if (spResp->IsError())
        {
            // Error
            cerr << spResp->spError->Serialize(true) << endl;
            cerr << strHelpTips << endl;
        }
        else if (spResp->IsSuccessful())
        {
            cout << spResp->spResult->Serialize(true) << endl;
        }
        else
        {
            cerr << "server error: neigher error nor result. resp: " << spResp->Serialize(true) << endl;
        }
    }
    catch (exception& e)
    {
        /*StdError(__PRETTY_FUNCTION__, e.what());*/
        cerr << "CRPCClient::HandleEvent: " << e.what() << endl;
    }

    ioComplt.Completed(false);
    return true;
}

bool CRPCClient::GetResponse(uint64 nNonce, const std::string& content)
{
    if (WalleveConfig()->fDebug)
    {
        cout << "request: " << content << endl;
    }

    CWalleveEventHttpGet eventHttpGet(nNonce);
    CWalleveHttpGet& httpGet = eventHttpGet.data;
    httpGet.strIOModule = WalleveGetOwnKey();
    httpGet.nTimeout = WalleveConfig()->nRPCConnectTimeout;;
    if (WalleveConfig()->fRPCSSLEnable)
    {
        httpGet.strProtocol = "https";
        httpGet.fVerifyPeer = true;
        httpGet.strPathCA   = WalleveConfig()->strRPCCAFile;
        httpGet.strPathCert = WalleveConfig()->strRPCCertFile;
        httpGet.strPathPK   = WalleveConfig()->strRPCPKFile;
    }
    else
    {
        httpGet.strProtocol = "http";
    }

    CNetHost host(WalleveConfig()->strRPCConnect,WalleveConfig()->nRPCPort);
    httpGet.mapHeader["host"] = host.ToString();
    httpGet.mapHeader["url"] = "/" + to_string(MV_VERSION);
    httpGet.mapHeader["method"] = "POST";
    httpGet.mapHeader["accept"] = "application/json";
    httpGet.mapHeader["content-type"] = "application/json";
    httpGet.mapHeader["user-agent"] = string("multiverse-json-rpc/");
    httpGet.mapHeader["connection"] = "Keep-Alive";
    if (!WalleveConfig()->strRPCPass.empty() || !WalleveConfig()->strRPCUser.empty())
    {
        string strAuth;
        CHttpUtil().Base64Encode(WalleveConfig()->strRPCUser + ":" + WalleveConfig()->strRPCPass,strAuth);
        httpGet.mapHeader["authorization"] = string("Basic ") + strAuth;
    }

    httpGet.strContent = content + "\n";
    
    ioComplt.Reset();

    if (!pHttpGet->DispatchEvent(&eventHttpGet))
    {
        throw runtime_error("failed to send json request");
    }
    bool fResult = false;
    return (ioComplt.WaitForComplete(fResult) && fResult);
}

bool CRPCClient::CallRPC(CRPCParamPtr spParam, int nReqId)
{
    try
    {
        CRPCReqPtr spReq = MakeCRPCReqPtr(nReqId, spParam->Method(), spParam);
        return GetResponse(1, spReq->Serialize());
    }
    catch (exception& e)
    {
        StdError(__PRETTY_FUNCTION__, e.what());
    }
    catch (...)
    {
        StdError(__PRETTY_FUNCTION__, "unknown");
    }
    return false;
}

bool CRPCClient::CallConsoleCommand(const vector<std::string>& vCommand)
{
    if (vCommand[0] == "help")
    {
        if (vCommand.size() == 1)
        {
            cout << LocalCommandUsage() << endl;
            return false;
        }
        else
        {
            string usage = LocalCommandUsage(vCommand[1]);
            if (usage.empty())
            {
                return false;
            }
            cout << usage << endl;
        }
    }
    else
    {
        return false;
    }
    return true;
}

void CRPCClient::LaunchConsole()
{
    ioService.reset();

    EnterLoop();

    WaitForChars();
    ioService.run();

    LeaveLoop();
}

void CRPCClient::DispatchLine(const string& strLine)
{
    ioStrand.dispatch(boost::bind(&CRPCClient::ConsoleHandleLine, this, strLine));
}

void CRPCClient::LaunchCommand()
{
    const CRPCParam* param = dynamic_cast<const CRPCParam*>(IWalleveBase::WalleveConfig());
    if (param != NULL)
    {
        // avoid delete global pointer
        CRPCParamPtr spParam(const_cast<CRPCParam*>(param), [](CRPCParam* p) {});
        CallRPC(spParam, nLastNonce);
    }
    else
    {
        cerr << "Unknown command" << endl;
    }
    MvShutdown();
}

void CRPCClient::CancelCommand()
{
    if (pHttpGet)
    {
        CWalleveEventHttpAbort eventAbort(0);
        CWalleveHttpAbort& httpAbort = eventAbort.data;
        httpAbort.strIOModule = WalleveGetOwnKey();
        httpAbort.vNonce.push_back(1);
        pHttpGet->DispatchEvent(&eventAbort);
    }
}

void CRPCClient::WaitForChars()
{
#if BOOST_VERSION < 106600
    // TODO: Just to be compilable. It works incorrect.
    inStream.async_read_some(bufRead, boost::bind(&CRPCClient::HandleRead,this,
                                                     boost::asio::placeholders::error,
                                                     boost::asio::placeholders::bytes_transferred));
#else
    inStream.async_wait(boost::asio::posix::stream_descriptor::wait_read, boost::bind(&CRPCClient::HandleRead,this,
                                                     boost::asio::placeholders::error));
#endif
}

#if BOOST_VERSION < 106600
void CRPCClient::HandleRead(const boost::system::error_code& err, size_t nTransferred)
#else
void CRPCClient::HandleRead(const boost::system::error_code& err)
#endif
{
    if (err == boost::system::errc::success)
    {
        rl_callback_read_char();
        WaitForChars();
    }
}

void CRPCClient::EnterLoop()
{
    Initialize_ReadLine();
}

void CRPCClient::LeaveLoop()
{
    cout << "\n";
    Uninitialize_ReadLine();
}

void CRPCClient::ConsoleHandleLine(const string& strLine)
{
    if (strLine == "quit")
    {
        Uninitialize_ReadLine();
        MvShutdown();
        return;
    }

    vector<string> vCommand;

    // parse command line input
    // part 1: Parse pair of blank and quote(' or ")
    // part 2: If part 1 is blank, part 2 is any charactor besides blank. 
    //         if part 1 is quote, part 2 is any charactor besides the quote in part 1.
    // part 3: consume the tail of match.
    boost::regex e("[ \t]*((?<quote>['\"])|[ \t]*)"
        "((?(<quote>).*?|[^ \t]+))"
        "(?(<quote>)\\k<quote>|([ \t]+|$))", boost::regex::perl);
    boost::sregex_iterator it(strLine.begin(), strLine.end(), e);
    boost::sregex_iterator end;
    for (; it != end; it++)
    {
        string str = (*it)[3];
        vCommand.push_back(str);
    }

    if (WalleveConfig()->fDebug)
    {
        cout << "command : " ;
        for (auto& x: vCommand)
        {
            cout << x << ',';
        }
        cout << endl;
    }

    if (!vCommand.empty())
    {
        add_history(strLine.c_str());

        if (!CallConsoleCommand(vCommand))
        {
            try
            {
                CMvConfig config;

                char* argv[vCommand.size() + 1];
                argv[0] = const_cast<char*>("multiverse-cli");
                for (int i = 0; i < vCommand.size(); ++i)
                {
                    argv[i + 1] = const_cast<char*>(vCommand[i].c_str());
                }

                if (!config.Load(vCommand.size() + 1, argv, "", "") || !config.PostLoad())
                {
                    return;
                }

                // help
                if (config.GetConfig()->fHelp)
                {
                    cout << config.Help() << endl;
                    return;
                }

                // call rpc
                CRPCParam* param = dynamic_cast<CRPCParam*>(config.GetConfig());
                if (param != NULL)
                {
                    // avoid delete global point
                    CRPCParamPtr spParam(param, [](CRPCParam* p) {});
                    CallRPC(spParam, ++nLastNonce);
                }
                else
                {
                    cerr << "Unknown command" << endl;
                }
            }
            catch (CRPCException& e)
            {
                StdError(__PRETTY_FUNCTION__, (e.strMessage + strHelpTips).c_str());
            }
            catch (exception& e)
            {
                StdError(__PRETTY_FUNCTION__, e.what());
            }
        }
    }
}

///////////////////////////////
// readline

static char * RPCCommand_Generator(const char* text,int state)
{
    static int listIndex,len;
    if (!state)
    {
        listIndex = 0;
        len = strlen(text);
    }

    auto& list = RPCCmdList();
    for (; listIndex < list.size(); )
    {
        const char* cmd = list[listIndex].c_str();
        listIndex++;
        if (strncmp(cmd,text,len) == 0)
        {
            char *r = (char*)malloc(strlen(cmd) + 1);
            strcpy(r,cmd);
            return r;
        }
    }
    return NULL;
}

static char ** RPCCommand_Completion(const char* text, int start, int end)
{
    (void)end;
    char **matches = NULL;
    if (start == 0)
    {
        matches = rl_completion_matches(text,RPCCommand_Generator);
    }
    return matches;
}

static void Initialize_ReadLine()
{
    rl_catch_signals = 0;
    rl_attempted_completion_function = RPCCommand_Completion;
    rl_callback_handler_install(prompt, ReadlineCallback);
}

static void Uninitialize_ReadLine()
{
    rl_callback_handler_remove();
}

static void ReadlineCallback(char* line)
{
    string strLine = line ? line : "quit";
    if (pClient)
    {
        pClient->DispatchLine(strLine);
    }
}
