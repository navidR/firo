// Copyright (c) 2026 The Firo Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef FIRO_HELSING_TYPES_H
#define FIRO_HELSING_TYPES_H

#include "amount.h"
#include "serialize.h"
#include "spark/primitives.h"
#include "uint256.h"

#include <cstdint>
#include <vector>

namespace helsing {

// Revised Helsing spec output_id: canonical ledger output identity, e.g. (txid, vout).
struct OutputId {
    uint256 txid;
    uint32_t vout{0};

    OutputId() = default;
    OutputId(const uint256& txid_, uint32_t vout_) : txid(txid_), vout(vout_) {}

    friend bool operator==(const OutputId& a, const OutputId& b)
    {
        return a.txid == b.txid && a.vout == b.vout;
    }

    friend bool operator!=(const OutputId& a, const OutputId& b)
    {
        return !(a == b);
    }

    friend bool operator<(const OutputId& a, const OutputId& b)
    {
        int cmp = a.txid.Compare(b.txid);
        return cmp < 0 || (cmp == 0 && a.vout < b.vout);
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(txid);
        READWRITE(vout);
    }
};

struct StakeContext {
    std::vector<unsigned char> bytes;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(bytes);
    }
};

struct ProofBlob {
    std::vector<unsigned char> bytes;

    bool empty() const { return bytes.empty(); }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(bytes);
    }
};

enum class SparkOutputType : uint8_t {
    UNKNOWN = 0,
    MINT = 1,
    SPEND = 2,
};

struct SparkOutputRecord {
    OutputId output_id;
    GroupElement S;
    GroupElement C;
    GroupElement K;
    int nHeight{-1};
    SparkOutputType type{SparkOutputType::UNKNOWN};
    bool helsing_eligible{false};
};

struct StakeTx {
    std::vector<OutputId> inCoinIDs;
    GroupElement S_prime;
    GroupElement C_prime;
    GroupElement T;
    StakeContext m;
    ProofBlob pi_par;
    ProofBlob pi_val;
    ProofBlob pi_tag;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(inCoinIDs);
        READWRITE(S_prime);
        READWRITE(C_prime);
        READWRITE(T);
        READWRITE(m);
        READWRITE(pi_par);
        READWRITE(pi_val);
        READWRITE(pi_tag);
    }
};

enum class StakeStatus : uint8_t {
    ACTIVE = 0,
    SPENT = 1,
    REVOKED = 2,
};

struct StakeRecord {
    uint256 stake_id;
    GroupElement T;
    StakeContext m;
    int nHeight{-1};
    int nSpentHeight{-1};
    StakeStatus status{StakeStatus::ACTIVE};
};

} // namespace helsing

#endif // FIRO_HELSING_TYPES_H
