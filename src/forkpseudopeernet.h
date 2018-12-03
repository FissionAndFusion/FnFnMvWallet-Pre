// Copyright (c) 2017-2018 The Multiverse developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MULTIVERSE_FORKPSEUDOPEERNET_H
#define MULTIVERSE_FORKPSEUDOPEERNET_H

#include "mvproto.h"
#include "forkpeerevent.h"
#include "mvpeernet.h"
#include "dbpservice.h"

namespace multiverse
{

class CForkPseudoPeerNet: public network::CMvPeerNet, virtual public CFkNodeEventListener
{
public:
    enum class SUPER_NODE_TYPE : int
    {
        SUPER_NODE_TYPE_FNFN = 0,
        SUPER_NODE_TYPE_ROOT,
        SUPER_NODE_TYPE_FORK
    };

public:
    CForkPseudoPeerNet();
    ~CForkPseudoPeerNet();

    SUPER_NODE_TYPE GetSuperNodeType() {return typeNode;};
protected:
    bool WalleveHandleInitialize() override;
    void WalleveHandleDeinitialize() override;

    bool HandleEvent(CFkEventNodeBlockArrive& event) override;
    bool HandleEvent(CFkEventNodeTxArrive& event) override;
    bool HandleEvent(CFkEventNodeBlockRequest& event) override;
    bool HandleEvent(CFkEventNodeTxRequest& event) override;
    bool HandleEvent(CFkEventNodeUpdateForkState& event) override;
    bool HandleEvent(CFkEventNodeSendBlockNotice& event) override;
    bool HandleEvent(CFkEventNodeSendTxNotice& event) override;
    bool HandleEvent(CFkEventNodeSendBlock& event) override;
    bool HandleEvent(CFkEventNodeSendTx& event) override;

    bool ExistForkID(const uint256& forkid) {
        return (mapForkNodeHeight.find(forkid) != mapForkNodeHeight.end());
    }

protected:
    IDbpService* pDbpService;
    std::map<uint256, std::pair<int, uint256>> mapForkNodeHeight;          //ForkID-(LastHeight-BlockHash) for offspring node
    std::map<uint256, std::pair<int, uint256>> mapForkNodeHeightCurrent;   //ForkID-(LastHeight-BlockHash) for current node
    SUPER_NODE_TYPE typeNode;
};

}

#endif //MULTIVERSE_FORKPSEUDOPEERNET_H
