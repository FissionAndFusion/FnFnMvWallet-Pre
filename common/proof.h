// Copyright (c) 2017-2019 The Multiverse developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef  MULTIVERSE_PROOF_H
#define  MULTIVERSE_PROOF_H

#include "uint256.h"
#include "key.h"
#include <walleve/stream/datastream.h>
#include "destination.h"

class CProofOfSecretShare
{
public:
    unsigned char nWeight;
    uint256 nAgreement;
public:
    void Save(std::vector<unsigned char>& vchProof)
    {
        walleve::CWalleveODataStream os(vchProof);
        ToStream(os);
    }
    void Load(const std::vector<unsigned char>& vchProof)
    {
        walleve::CWalleveIDataStream is(vchProof);
        FromStream(is);
    }
protected:
    virtual void ToStream(walleve::CWalleveODataStream& os)
    {
        os << nWeight << nAgreement;
    }
    virtual void FromStream(walleve::CWalleveIDataStream& is)
    {
        is >> nWeight >> nAgreement;
    }
};

class CProofOfPiggyback : public CProofOfSecretShare
{
public:
    uint256 hashRefBlock;
protected:
    virtual void ToStream(walleve::CWalleveODataStream& os) override
    {
        CProofOfSecretShare::ToStream(os);
        os << hashRefBlock;
    }
    virtual void FromStream(walleve::CWalleveIDataStream& is) override
    {
        CProofOfSecretShare::FromStream(is);
        is >> hashRefBlock;
    }
};

class CProofOfHashWork : public CProofOfSecretShare
{
public:
    unsigned char nAlgo;
    unsigned char nBits;
    CDestination destMint;
    uint256 nNonce;
protected:
    virtual void ToStream(walleve::CWalleveODataStream& os) override
    {
        CProofOfSecretShare::ToStream(os);
        os << nAlgo << nBits << destMint.prefix << destMint.data << nNonce;
    }
    virtual void FromStream(walleve::CWalleveIDataStream& is) override
    {
        CProofOfSecretShare::FromStream(is);
        is >> nAlgo >> nBits >> destMint.prefix >> destMint.data >> nNonce;
    }
};

class CProofOfHashWorkCompact
{
public:
    unsigned char nAlgo;
    unsigned char nBits;
    CDestination destMint;
    uint256 nNonce;
    
public:
    enum { PROOFHASHWORK_SIZE = 67 };
    void Save(std::vector<unsigned char>& vchProof)
    {
        unsigned char *p = &vchProof[vchProof.size() - PROOFHASHWORK_SIZE];
        *p++ = nAlgo; *p++ = nBits; *p++ = destMint.prefix; *((uint256*)p) = destMint.data; p += 32; *((uint256*)p) = nNonce;
    }
    void Load(const std::vector<unsigned char>& vchProof)
    {
        const unsigned char *p = &vchProof[vchProof.size() - PROOFHASHWORK_SIZE];
        nAlgo = *p++; nBits = *p++; destMint.prefix = *p++; destMint.data = *((uint256*)p); p += 32; nNonce = *((uint256*)p);
    }
};

#endif //MULTIVERSE_PROOF_H

