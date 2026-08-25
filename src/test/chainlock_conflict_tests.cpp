// Copyright (c) 2026 The Firo Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "test/test_bitcoin.h"

#include "chainparams.h"
#include "consensus/validation.h"
#include "pow.h"
#include "primitives/block.h"
#include "validation.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(chainlock_conflict_tests, TestChain100Setup)

BOOST_AUTO_TEST_CASE(mark_conflicting_block_uses_separate_status)
{
    CBlockIndex* conflictingBlock;
    CBlockIndex* parent;
    CBlockHeader conflictingHeader;

    {
        LOCK(cs_main);
        conflictingBlock = chainActive.Tip();
        parent = conflictingBlock->pprev;
        conflictingHeader = conflictingBlock->GetBlockHeader();
    }

    const CBlockHeader descendantHeader = CreateBlock({}, coinbaseKey).GetBlockHeader();
    const CBlockIndex* descendantBlock = nullptr;
    CValidationState headerState;
    BOOST_REQUIRE(ProcessNewBlockHeaders({descendantHeader}, headerState, Params(), &descendantBlock));

    CBlockHeader extensionHeader = descendantHeader;
    extensionHeader.hashPrevBlock = descendantHeader.GetHash();
    ++extensionHeader.nTime;
    extensionHeader.nNonce = 0;
    while (!CheckProofOfWork(extensionHeader.GetHash(), extensionHeader.nBits, Params().GetConsensus())) {
        ++extensionHeader.nNonce;
    }

    {
        LOCK(cs_main);
        BOOST_REQUIRE(descendantBlock);
        BOOST_CHECK_EQUAL(descendantBlock->pprev, conflictingBlock);
        BOOST_CHECK(!(descendantBlock->nStatus & BLOCK_HAVE_DATA));
        BOOST_CHECK_EQUAL(pindexBestHeader, descendantBlock);
        BOOST_REQUIRE(conflictingBlock->IsValid(BLOCK_VALID_SCRIPTS));

        CValidationState state;
        BOOST_REQUIRE(MarkConflictingBlock(state, Params(), conflictingBlock));
        BOOST_CHECK_EQUAL(chainActive.Tip(), parent);
        BOOST_CHECK(conflictingBlock->nStatus & BLOCK_CONFLICT_CHAINLOCK);
        BOOST_CHECK(descendantBlock->nStatus & BLOCK_CONFLICT_CHAINLOCK);
        BOOST_CHECK(!(conflictingBlock->nStatus & BLOCK_FAILED_MASK));
        BOOST_CHECK(conflictingBlock->IsValid(BLOCK_VALID_SCRIPTS));
        BOOST_CHECK_EQUAL(pindexBestHeader, parent);

        BOOST_REQUIRE(ResetBlockFailureFlags(conflictingBlock));
        BOOST_CHECK(conflictingBlock->nStatus & BLOCK_CONFLICT_CHAINLOCK);
        BOOST_CHECK(descendantBlock->nStatus & BLOCK_CONFLICT_CHAINLOCK);
    }

    CValidationState duplicateState;
    BOOST_CHECK(!ProcessNewBlockHeaders({conflictingHeader}, duplicateState, Params()));
    BOOST_CHECK_EQUAL(duplicateState.GetRejectReason(), "duplicate");

    CValidationState descendantDuplicateState;
    BOOST_CHECK(!ProcessNewBlockHeaders({descendantHeader}, descendantDuplicateState, Params()));
    BOOST_CHECK_EQUAL(descendantDuplicateState.GetRejectReason(), "duplicate");

    CValidationState extensionState;
    BOOST_CHECK(!ProcessNewBlockHeaders({extensionHeader}, extensionState, Params()));
    BOOST_CHECK_EQUAL(extensionState.GetRejectReason(), "bad-prevblk-chainlock");

    CValidationState preciousState;
    BOOST_REQUIRE(PreciousBlock(preciousState, Params(), conflictingBlock));

    LOCK(cs_main);
    BOOST_CHECK_EQUAL(chainActive.Tip(), parent);
    BOOST_CHECK_EQUAL(pindexBestHeader, parent);
    BOOST_CHECK(conflictingBlock->nStatus & BLOCK_CONFLICT_CHAINLOCK);
    BOOST_CHECK(descendantBlock->nStatus & BLOCK_CONFLICT_CHAINLOCK);
    BOOST_CHECK(!(conflictingBlock->nStatus & BLOCK_FAILED_MASK));
}

BOOST_AUTO_TEST_SUITE_END()
