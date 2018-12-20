// Copyright (c) 2017-2018 The Multiverse developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "mode/rpc_config.h"

#include <boost/algorithm/algorithm.hpp>
#include "mode/config_macro.h"

namespace multiverse
{
namespace po = boost::program_options;
namespace fs = boost::filesystem;
using tcp = boost::asio::ip::tcp;

/////////////////////////////////////////////////////////////////////
// CMvRPCBasicConfig

CMvRPCBasicConfig::CMvRPCBasicConfig()
{
    po::options_description desc("RPCBasic");

    CMvRPCBasicConfigOption::AddOptionsImpl(desc);

    AddOptions(desc);
}

CMvRPCBasicConfig::~CMvRPCBasicConfig() {}

bool CMvRPCBasicConfig::PostLoad()
{
    if (fHelp)
    {
        return true;
    }

    if (nRPCPortInt <= 0 || nRPCPortInt > 0xFFFF)
    {
        nRPCPort = (fTestNet ? DEFAULT_TESTNET_RPCPORT : DEFAULT_RPCPORT);
    }
    else
    {
        nRPCPort = (unsigned short)nRPCPortInt;
    }

    if (!strRPCCAFile.empty())
    {
        if (!fs::path(strRPCCAFile).is_complete())
        {
            strRPCCAFile = (pathRoot / strRPCCAFile).string();
        }
    }

    if (!strRPCCertFile.empty())
    {
        if (!fs::path(strRPCCertFile).is_complete())
        {
            strRPCCertFile = (pathRoot / strRPCCertFile).string();
        }
    }

    if (!strRPCPKFile.empty())
    {
        if (!fs::path(strRPCPKFile).is_complete())
        {
            strRPCPKFile = (pathRoot / strRPCPKFile).string();
        }
    }

    return true;
}

std::string CMvRPCBasicConfig::ListConfig() const
{
    std::ostringstream oss;
    oss << CMvRPCBasicConfigOption::ListConfigImpl();
    oss << "RPCPort: " << nRPCPort << "\n";
    return oss.str();
}

std::string CMvRPCBasicConfig::Help() const
{
    return CMvRPCBasicConfigOption::HelpImpl();
}

/////////////////////////////////////////////////////////////////////
// CMvRPCServerConfig

CMvRPCServerConfig::CMvRPCServerConfig()
{
    po::options_description desc("RPCServer");

    CMvRPCServerConfigOption::AddOptionsImpl(desc);

    AddOptions(desc);
}

CMvRPCServerConfig::~CMvRPCServerConfig() {}

bool CMvRPCServerConfig::PostLoad()
{
    if (fHelp)
    {
        return true;
    }

    CMvRPCBasicConfig::PostLoad();

    epRPC = tcp::endpoint(!vRPCAllowIP.empty()
                              ? boost::asio::ip::address_v4::any()
                              : boost::asio::ip::address_v4::loopback(),
                          nRPCPort);
    
    return true;
}

std::string CMvRPCServerConfig::ListConfig() const
{
    std::ostringstream oss;
    oss << CMvRPCServerConfigOption::ListConfigImpl();
    oss << "epRPC: " << epRPC << "\n";
    return CMvRPCBasicConfig::ListConfig() + oss.str();
}

std::string CMvRPCServerConfig::Help() const
{
    return CMvRPCBasicConfig::Help() + CMvRPCServerConfigOption::HelpImpl();
}

/////////////////////////////////////////////////////////////////////
// CMvRPCClientConfig

CMvRPCClientConfig::CMvRPCClientConfig()
{
    po::options_description desc("RPCClient");

    CMvRPCClientConfigOption::AddOptionsImpl(desc);

    AddOptions(desc);
}

CMvRPCClientConfig::~CMvRPCClientConfig() {}

bool CMvRPCClientConfig::PostLoad()
{
    if (fHelp)
    {
        return true;
    }

    CMvRPCBasicConfig::PostLoad();

    if (nRPCConnectTimeout == 0)
    {
        nRPCConnectTimeout = 1;
    }

    return true;
}

std::string CMvRPCClientConfig::ListConfig() const
{
    std::ostringstream oss;
    oss << CMvRPCClientConfigOption::ListConfigImpl();
    return CMvRPCBasicConfig::ListConfig() + oss.str();
}

std::string CMvRPCClientConfig::Help() const
{
    return CMvRPCBasicConfig::Help() + CMvRPCClientConfigOption::HelpImpl();
}


}  // namespace multiverse