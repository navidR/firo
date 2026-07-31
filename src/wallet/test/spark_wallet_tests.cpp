#include "config/bitcoin-config.h"

#include <../../test/fixtures.h>
#include "../wallet.h"
#include "../../spark/sparkwallet.h"
#ifdef USE_SQLITE
#include "../../init.h"
#include "../../utilstrencodings.h"
#include "../sqlite.h"

#include <sqlite3.h>
#endif
#include "../../validation.h"

#include <boost/test/unit_test.hpp>

#ifdef USE_SQLITE
#include <atomic>
#endif

static std::vector<unsigned char> random_char_vector()
{                                                    
    Scalar temp;
    temp.randomize();
    std::vector<unsigned char> result;
    result.resize(spark::SCALAR_ENCODING);
    temp.serialize(result.data());
    return result;
}

CBlock GetCBlock(CBlockIndex const *blockIdx)
{
    CBlock block;
    if (!ReadBlockFromDisk(block, blockIdx, ::Params().GetConsensus())) {
        throw std::invalid_argument("No block index data");
    }

    return block;
}

void ExtractSpend(CTransaction const &tx,                                                 
     std::vector<spark::Coin>& coins,
     std::vector<GroupElement>& lTags) {

     if (tx.vin[0].scriptSig.IsSparkSpend()) {
         coins.clear();
         coins =  spark::GetSparkMintCoins(tx);
         lTags.clear();
         lTags =  spark::GetSparkUsedTags(tx);
     }
}

#ifdef USE_SQLITE
extern std::atomic<bool> fRequestShutdown;

namespace
{
class InstallDiversifierFailureExecutor final : public SQLiteStatementExecutor
{
private:
    const std::string m_serialized_key_hex;
    bool m_installed{false};

public:
    explicit InstallDiversifierFailureExecutor(
        std::string serializedKeyHex)
        : m_serialized_key_hex(std::move(serializedKeyHex))
    {
    }

    int Execute(sqlite3* database, const char* statement) override
    {
        if (!m_installed &&
            std::string(statement) == "BEGIN TRANSACTION") {
            const std::string trigger = strprintf(
                "CREATE TEMP TRIGGER reject_spark_diversifier "
                "BEFORE INSERT ON main WHEN NEW.key = X'%s' BEGIN "
                "SELECT RAISE(FAIL, 'injected Spark diversifier failure'); END",
                m_serialized_key_hex);
            const int result = SQLiteStatementExecutor::Execute(
                database,
                trigger.c_str());
            if (result != SQLITE_OK) {
                return result;
            }
            m_installed = true;
        }
        return SQLiteStatementExecutor::Execute(
            database,
            statement);
    }
};

class CommitThenFailSQLiteExecutor final : public SQLiteStatementExecutor
{
public:
    int Execute(sqlite3* database, const char* statement) override
    {
        const int result =
            SQLiteStatementExecutor::Execute(database, statement);
        if (result == SQLITE_OK &&
            std::string(statement) == "COMMIT TRANSACTION") {
            return SQLITE_IOERR;
        }
        return result;
    }
};

class ShutdownRequestReset
{
private:
    const bool m_previous;

public:
    ShutdownRequestReset()
        : m_previous(fRequestShutdown.exchange(false))
    {
    }

    ~ShutdownRequestReset()
    {
        fRequestShutdown.store(m_previous);
    }
};

std::unique_ptr<CWallet> MakeSQLiteSparkTestWallet(
    const std::string& filename)
{
    DatabaseOptions options;
    options.require_create = true;
    options.require_format = DatabaseFormat::SQLITE;
    DatabaseStatus status;
    std::string error;
    std::unique_ptr<WalletDatabase> database =
        MakeSQLiteDatabase(filename, options, status, error);
    if (!database) {
        throw std::runtime_error(
            "Unable to create SQLite Spark test wallet: " + error);
    }

    auto wallet = std::make_unique<CWallet>(std::move(database));
    const CPubKey masterKey = wallet->GenerateNewHDMasterKey();
    if (!wallet->SetHDMasterKey(
            masterKey,
            CHDChain().VERSION_WITH_BIP44)) {
        throw std::runtime_error(
            "Unable to initialize SQLite Spark test wallet");
    }
    return wallet;
}

void InstallDiversifierFailure(WalletDatabase& database)
{
    CDataStream key(SER_DISK, CLIENT_VERSION);
    key << std::string("div");
    std::unique_ptr<DatabaseBatch> batch = database.MakeBatch();
    if (!SetSQLiteStatementExecutorForTesting(
            *batch,
            std::make_unique<InstallDiversifierFailureExecutor>(
                HexStr(key.begin(), key.end()))) ||
        !batch->TxnBegin() ||
        !batch->TxnAbort()) {
        throw std::runtime_error(
            "Unable to install SQLite Spark failure trigger");
    }
}

void CheckSameSparkAddress(
    const spark::Address& first,
    const spark::Address& second)
{
    BOOST_CHECK(first.get_d() == second.get_d());
    BOOST_CHECK(first.get_Q1() == second.get_Q1());
    BOOST_CHECK(first.get_Q2() == second.get_Q2());
}
} // namespace
#endif

BOOST_FIXTURE_TEST_SUITE(spark_wallet_tests, SparkTestingSetup)

#ifdef USE_SQLITE
BOOST_AUTO_TEST_CASE(sqlite_initialization_state_contract)
{
    const spark::Params* params = spark::Params::get_default();

    {
        auto wallet = MakeSQLiteSparkTestWallet(
            "spark_sqlite_success.dat");
        std::string firstViewKey;
        spark::Address firstDefaultAddress(params);
        {
            CSparkWallet sparkWallet(*wallet);
            firstViewKey = wallet->GetSparkViewKeyStr();
            firstDefaultAddress = sparkWallet.getDefaultAddress();
            const auto addresses = sparkWallet.getAllAddresses();
            BOOST_REQUIRE_EQUAL(addresses.size(), 1U);
            BOOST_CHECK(addresses.count(0) == 1);
        }

        {
            CSparkWallet reloaded(*wallet);
            BOOST_CHECK_EQUAL(
                wallet->GetSparkViewKeyStr(),
                firstViewKey);
            CheckSameSparkAddress(
                reloaded.getDefaultAddress(),
                firstDefaultAddress);
            const auto addresses = reloaded.getAllAddresses();
            BOOST_REQUIRE_EQUAL(addresses.size(), 1U);
            BOOST_CHECK(addresses.count(0) == 1);
        }

        CWalletDB walletdb(wallet->GetDatabase());
        spark::FullViewKey fullViewKey(params);
        int32_t diversifier = -1;
        BOOST_CHECK(
            walletdb.readFullViewKeyWithStatus(fullViewKey) ==
            DatabaseReadStatus::SUCCESS);
        BOOST_CHECK(
            walletdb.readDiversifierWithStatus(diversifier) ==
            DatabaseReadStatus::SUCCESS);
        BOOST_CHECK_EQUAL(diversifier, 0);
    }

    {
        auto wallet = MakeSQLiteSparkTestWallet(
            "spark_sqlite_full_view_key_only.dat");
        {
            CSparkWallet initialized(*wallet);
        }
        {
            std::unique_ptr<DatabaseBatch> batch =
                wallet->GetDatabase().MakeBatch();
            BOOST_REQUIRE(batch->Erase(std::string("div")));
        }

        BOOST_CHECK_THROW(
            std::make_unique<CSparkWallet>(*wallet),
            std::runtime_error);
        CWalletDB walletdb(wallet->GetDatabase());
        spark::FullViewKey fullViewKey(params);
        int32_t diversifier = -1;
        BOOST_CHECK(
            walletdb.readFullViewKeyWithStatus(fullViewKey) ==
            DatabaseReadStatus::SUCCESS);
        BOOST_CHECK(
            walletdb.readDiversifierWithStatus(diversifier) ==
            DatabaseReadStatus::NOT_FOUND);
    }

    {
        auto wallet = MakeSQLiteSparkTestWallet(
            "spark_sqlite_diversifier_only.dat");
        {
            CSparkWallet initialized(*wallet);
        }
        {
            std::unique_ptr<DatabaseBatch> batch =
                wallet->GetDatabase().MakeBatch();
            BOOST_REQUIRE(
                batch->Erase(std::string("fullViewKey")));
        }

        BOOST_CHECK_THROW(
            std::make_unique<CSparkWallet>(*wallet),
            std::runtime_error);
        BOOST_CHECK_THROW(
            wallet->GetSparkViewKey(),
            std::runtime_error);
        CWalletDB walletdb(wallet->GetDatabase());
        spark::FullViewKey fullViewKey(params);
        int32_t diversifier = -1;
        BOOST_CHECK(
            walletdb.readFullViewKeyWithStatus(fullViewKey) ==
            DatabaseReadStatus::NOT_FOUND);
        BOOST_CHECK(
            walletdb.readDiversifierWithStatus(diversifier) ==
            DatabaseReadStatus::SUCCESS);
        BOOST_CHECK_EQUAL(diversifier, 0);
    }

    {
        auto wallet = MakeSQLiteSparkTestWallet(
            "spark_sqlite_corrupt_full_view_key.dat");
        {
            CSparkWallet initialized(*wallet);
        }
        {
            std::unique_ptr<DatabaseBatch> batch =
                wallet->GetDatabase().MakeBatch();
            BOOST_REQUIRE(batch->Write(
                std::string("fullViewKey"),
                uint8_t{1}));
        }

        BOOST_CHECK_THROW(
            std::make_unique<CSparkWallet>(*wallet),
            std::runtime_error);
        BOOST_CHECK_THROW(
            wallet->GetSparkViewKey(),
            std::runtime_error);
        CWalletDB walletdb(wallet->GetDatabase());
        spark::FullViewKey fullViewKey(params);
        int32_t diversifier = -1;
        BOOST_CHECK(
            walletdb.readFullViewKeyWithStatus(fullViewKey) ==
            DatabaseReadStatus::CORRUPT);
        BOOST_CHECK(
            walletdb.readDiversifierWithStatus(diversifier) ==
            DatabaseReadStatus::SUCCESS);
        BOOST_CHECK_EQUAL(diversifier, 0);
    }
}

BOOST_AUTO_TEST_CASE(sqlite_initialization_write_is_atomic)
{
    auto wallet = MakeSQLiteSparkTestWallet(
        "spark_sqlite_atomic_initialization.dat");
    InstallDiversifierFailure(wallet->GetDatabase());

    BOOST_CHECK_THROW(
        std::make_unique<CSparkWallet>(*wallet),
        std::runtime_error);

    CWalletDB walletdb(wallet->GetDatabase());
    spark::FullViewKey fullViewKey(spark::Params::get_default());
    int32_t diversifier = -1;
    BOOST_CHECK(
        walletdb.readFullViewKeyWithStatus(fullViewKey) ==
        DatabaseReadStatus::NOT_FOUND);
    BOOST_CHECK(
        walletdb.readDiversifierWithStatus(diversifier) ==
        DatabaseReadStatus::NOT_FOUND);
}

BOOST_AUTO_TEST_CASE(sqlite_address_persistence_precedes_publication)
{
    auto wallet = MakeSQLiteSparkTestWallet(
        "spark_sqlite_address_persistence.dat");
    CSparkWallet sparkWallet(*wallet);
    const auto addressesBefore = sparkWallet.getAllAddresses();
    BOOST_REQUIRE_EQUAL(addressesBefore.size(), 1U);
    BOOST_REQUIRE(addressesBefore.count(0) == 1);

    InstallDiversifierFailure(wallet->GetDatabase());
    BOOST_CHECK_THROW(
        sparkWallet.generateNewAddress(),
        std::runtime_error);

    const auto addressesAfter = sparkWallet.getAllAddresses();
    BOOST_REQUIRE_EQUAL(
        addressesAfter.size(),
        addressesBefore.size());
    BOOST_REQUIRE(addressesAfter.count(0) == 1);
    CheckSameSparkAddress(
        addressesAfter.at(0),
        addressesBefore.at(0));
    CWalletDB walletdb(wallet->GetDatabase());
    int32_t diversifier = -1;
    BOOST_REQUIRE(
        walletdb.readDiversifierWithStatus(diversifier) ==
        DatabaseReadStatus::SUCCESS);
    BOOST_CHECK_EQUAL(diversifier, 0);
}

BOOST_AUTO_TEST_CASE(sqlite_backend_failure_does_not_initialize_spark)
{
    ShutdownRequestReset shutdownReset;
    BOOST_REQUIRE(!ShutdownRequested());
    const std::string filename{
        "spark_sqlite_backend_failure.dat"};
    auto wallet = MakeSQLiteSparkTestWallet(filename);
    std::unique_ptr<DatabaseBatch> batch =
        wallet->GetDatabase().MakeBatch();
    BOOST_REQUIRE(SetSQLiteStatementExecutorForTesting(
        *batch,
        std::make_unique<CommitThenFailSQLiteExecutor>()));
    BOOST_CHECK_THROW(
        batch->Write(
            std::string("failure-probe"),
            std::string("applied-before-error")),
        std::runtime_error);
    BOOST_CHECK(ShutdownRequested());
    BOOST_CHECK_THROW(
        std::make_unique<CSparkWallet>(*wallet),
        std::runtime_error);
    batch.reset();
    wallet.reset();

    DatabaseOptions options;
    options.require_existing = true;
    options.require_format = DatabaseFormat::SQLITE;
    DatabaseStatus status;
    std::string error;
    std::unique_ptr<WalletDatabase> reopened =
        MakeSQLiteDatabase(filename, options, status, error);
    BOOST_REQUIRE(reopened);
    CWalletDB walletdb(*reopened);
    spark::FullViewKey fullViewKey(
        spark::Params::get_default());
    int32_t diversifier = -1;
    BOOST_CHECK(
        walletdb.readFullViewKeyWithStatus(fullViewKey) ==
        DatabaseReadStatus::NOT_FOUND);
    BOOST_CHECK(
        walletdb.readDiversifierWithStatus(diversifier) ==
        DatabaseReadStatus::NOT_FOUND);
}

BOOST_AUTO_TEST_CASE(sqlite_failed_initialization_relocks_wallet)
{
    const SecureString passphrase{
        "Spark SQLite relock test passphrase"};
    auto wallet = MakeSQLiteSparkTestWallet(
        "spark_sqlite_relock.dat");
    BOOST_REQUIRE(wallet->EncryptWallet(passphrase));
    BOOST_REQUIRE(wallet->IsLocked());
    InstallDiversifierFailure(wallet->GetDatabase());

    bool unlockRequested = false;
    boost::signals2::scoped_connection unlockConnection(
        UnlockWallet.connect([&](CWallet* requestedWallet) {
            if (requestedWallet != wallet.get() ||
                !requestedWallet->Unlock(passphrase)) {
                throw std::runtime_error(
                    "Unable to unlock SQLite Spark test wallet");
            }
            unlockRequested = true;
        }));
    BOOST_CHECK_THROW(
        std::make_unique<CSparkWallet>(*wallet),
        std::runtime_error);
    BOOST_CHECK(unlockRequested);
    BOOST_CHECK(wallet->IsLocked());

    CWalletDB walletdb(wallet->GetDatabase());
    spark::FullViewKey fullViewKey(
        spark::Params::get_default());
    int32_t diversifier = -1;
    BOOST_CHECK(
        walletdb.readFullViewKeyWithStatus(fullViewKey) ==
        DatabaseReadStatus::NOT_FOUND);
    BOOST_CHECK(
        walletdb.readDiversifierWithStatus(diversifier) ==
        DatabaseReadStatus::NOT_FOUND);
}
#endif

BOOST_AUTO_TEST_CASE(create_mint_recipient)
{
    const uint64_t v = 1;
    spark::Address sparkAddress = pwalletMain->sparkWallet->getDefaultAddress();

    spark::MintedCoinData data;
    data.address = sparkAddress;
    data.v = v;
    data.memo = "Test memo";

    std::vector<spark::MintedCoinData> mintedCoins;
    mintedCoins.push_back(data);

    auto recipients = CSparkWallet::CreateSparkMintRecipients(mintedCoins, random_char_vector(), true);

    BOOST_CHECK(recipients[0].scriptPubKey.IsSparkMint());
    BOOST_CHECK_EQUAL(recipients[0].nAmount, v);
}

BOOST_AUTO_TEST_CASE(mint_and_store_spark)
{
    pwalletMain->SetBroadcastTransactions(true);
    GenerateBlocks(1001);

    std::vector<std::pair<CWalletTx, CAmount>> wtxAndFee;

    const uint64_t v = 1;
    spark::Address sparkAddress = pwalletMain->sparkWallet->getDefaultAddress();

    spark::MintedCoinData data;
    data.address = sparkAddress;
    data.v = v;
    data.memo = "Test memo";

    std::vector<spark::MintedCoinData> mintedCoins;
    mintedCoins.push_back(data);

    std::string result = pwalletMain->MintAndStoreSpark(mintedCoins, wtxAndFee, false, true);
    BOOST_CHECK_EQUAL(result, "");

    size_t mintAmount = 0;
    for (const auto& wtx : wtxAndFee) {
        auto tx = wtx.first.tx.get();

        BOOST_CHECK(tx->IsSparkMint());
        BOOST_CHECK(tx->IsSparkTransaction());

        for (const auto& out : tx->vout) {
            if (out.scriptPubKey.IsSparkMint()) {
                mintAmount += out.nValue;
            }
        }
        CMutableTransaction mtx(*tx);
        BOOST_CHECK(GenerateBlock({mtx}));
    }

    BOOST_CHECK_EQUAL(data.v, mintAmount);

    auto sparkState = spark::CSparkState::GetState();
    sparkState->Reset();
}

BOOST_AUTO_TEST_CASE(mint_subtract_fee)
{
    pwalletMain->SetBroadcastTransactions(true);
    GenerateBlocks(1001);

    std::vector<std::pair<CWalletTx, CAmount>> wtxAndFee;

    const uint64_t v = 1 * COIN;
    spark::Address sparkAddress = pwalletMain->sparkWallet->getDefaultAddress();

    spark::MintedCoinData data;
    data.address = sparkAddress;
    data.v = v;
    data.memo = "Test memo";

    std::vector<spark::MintedCoinData> mintedCoins;
    mintedCoins.push_back(data);

    std::string result = pwalletMain->MintAndStoreSpark(mintedCoins, wtxAndFee, true, true);
    BOOST_CHECK_EQUAL(result, "");

    size_t mintAmount = 0;
    size_t fee = 0;
    for (const auto& wtx : wtxAndFee) {
        auto tx = wtx.first.tx.get();

        BOOST_CHECK(tx->IsSparkMint());
        BOOST_CHECK(tx->IsSparkTransaction());

        for (const auto& out : tx->vout) {
            if (out.scriptPubKey.IsSparkMint()) {
                mintAmount += out.nValue;
            }
        }
        CMutableTransaction mtx(*tx);
        BOOST_CHECK(GenerateBlock({mtx}));
        fee += wtx.second;
    }

    BOOST_CHECK_EQUAL(data.v, mintAmount + fee);

    auto sparkState = spark::CSparkState::GetState();
    sparkState->Reset();
}

BOOST_AUTO_TEST_CASE(list_spark_mints)
{
    GenerateBlocks(1001);
    std::vector<CAmount> confirmedAmounts = {1, 2 * COIN};
    std::vector<CAmount> unconfirmedAmounts = {10 * COIN};
    std::vector<CAmount> allAmounts(confirmedAmounts);
    allAmounts.insert(allAmounts.end(), unconfirmedAmounts.begin(), unconfirmedAmounts.end());

    std::vector<CMutableTransaction> txs;
    auto mints = GenerateMints(allAmounts, txs);
    std::vector<CMutableTransaction> inTxs(txs.begin(), txs.begin() + txs.size() - 1);

    auto bIndex = GenerateBlock(inTxs);
    BOOST_CHECK(bIndex);

    auto block = GetCBlock(bIndex);
    pwalletMain->sparkWallet->UpdateMintStateFromBlock(block);

    auto extractAmountsFromAvailableCoins = [](std::vector<CSparkMintMeta> const &coins) -> std::vector<CAmount> {
         std::vector<CAmount> amounts;
         for (auto const &coin : coins) {
             amounts.push_back(coin.v);
         }

         return amounts;
     };

    std::vector<CSparkMintMeta> confirmedCoins = pwalletMain->sparkWallet->ListSparkMints(true, true);
    std::vector<CSparkMintMeta> allCoins = pwalletMain->sparkWallet->ListSparkMints(true, false);
    auto confirmed = extractAmountsFromAvailableCoins(confirmedCoins);
    auto all = extractAmountsFromAvailableCoins(allCoins);

    BOOST_CHECK(std::is_permutation(confirmed.begin(), confirmed.end(), confirmedAmounts.begin()));
    BOOST_CHECK(std::is_permutation(all.begin(), all.end(), allAmounts.begin()));

    // get mint
    CSparkMintMeta mint = pwalletMain->sparkWallet->getMintMeta(mints.front().k);
    BOOST_CHECK(mint.v == mints.front().v);

    auto sparkState = spark::CSparkState::GetState();
    sparkState->Reset();
}


BOOST_AUTO_TEST_CASE(spend)
{
    pwalletMain->SetBroadcastTransactions(true);
    GenerateBlocks(1001);
    const uint64_t v = 2 * COIN;

    spark::Address sparkAddress = pwalletMain->sparkWallet->getDefaultAddress();

    spark::MintedCoinData data;
    data.address = sparkAddress;
    data.v = v;
    data.memo = "Test memo";

    std::vector<spark::MintedCoinData> mintedCoins;
    mintedCoins.push_back(data);

    std::vector<std::pair<CWalletTx, CAmount>> wtxAndFee;
    std::string result = pwalletMain->MintAndStoreSpark(mintedCoins, wtxAndFee, false, true);

    std::vector<std::pair<CWalletTx, CAmount>> wtxAndFee2;
    pwalletMain->MintAndStoreSpark(mintedCoins, wtxAndFee2, false, true);

    BOOST_CHECK_EQUAL("", result);

    CMutableTransaction mutableTx(*(wtxAndFee[0].first.tx));
    CMutableTransaction mutableTx2(*(wtxAndFee2[0].first.tx));
    GenerateBlock({mutableTx, mutableTx2}, &script);
    GenerateBlocks(5);
    BOOST_CHECK_EQUAL(1, wtxAndFee.size());
    wtxAndFee.clear();

    auto spTx = GenerateSparkSpend({1 * COIN}, {}, nullptr);

    std::vector<spark::Coin> coins;
    std::vector<GroupElement> tags;
    ExtractSpend(spTx, coins, tags);

    BOOST_CHECK_EQUAL(1, coins.size());
    BOOST_CHECK_EQUAL(1, tags.size());

    auto sparkState = spark::CSparkState::GetState();
    sparkState->Reset();
}

BOOST_AUTO_TEST_CASE(mintspark_and_mint_all)
{
    auto countMintsInBalance = [&](
        std::vector<std::pair<CWalletTx, CAmount>> const& wtxs,
        bool includeFee = false) -> CAmount {

        CAmount sum = 0;
        for (auto const &w : wtxs) {
            for (auto const &out : w.first.tx->vout) {
                if (out.scriptPubKey.IsSparkMint()) {
                    sum += out.nValue;
                }
             }

            if (includeFee) {
                sum += w.second;
            }
        }
        return sum;
    };

    auto getAvailableCoinsForMintBalance = [&]() -> CAmount {
        std::vector<std::pair<CAmount, std::vector<COutput>>> valueAndUTXO;
        pwalletMain->AvailableCoinsForLMint(valueAndUTXO, nullptr);
        CAmount s = 0;

        for (auto const &v : valueAndUTXO) {
            s += v.first;
        }

        return s;
    };

    CScript externalScript;
    {
        uint160 seed;
        GetRandBytes(seed.begin(), seed.size());

        externalScript = GetScriptForDestination(CKeyID(seed));
    }

    auto generateBlocksPerScripts = [&](size_t blocks, size_t blocksPerScript) -> std::vector<CScript> {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        std::vector<CScript> scripts;
        while (blocks != 0) {
            CPubKey key;
            key = pwalletMain->GenerateNewKey();
            scripts.push_back(GetScriptForDestination(key.GetID()));
            auto blockCount = std::min(blocksPerScript, blocks);
            GenerateBlocks(blockCount, &scripts.back());
            blocks -= blockCount;
        }

        return scripts;
    };

    auto scripts = generateBlocksPerScripts(200, 10);
    GenerateBlocks(100, &externalScript);

    std::vector<std::pair<CWalletTx, CAmount>> wtxAndFee;
    const uint64_t v = 10 * COIN;

    spark::Address sparkAddress = pwalletMain->sparkWallet->getDefaultAddress();

    spark::MintedCoinData data;
    data.address = sparkAddress;
    data.v = v;
    data.memo = "Test memo";
    std::vector<spark::MintedCoinData> mintedCoins;
    mintedCoins.push_back(data);

    auto result = pwalletMain->MintAndStoreSpark(mintedCoins, wtxAndFee, false, true);
    BOOST_CHECK_EQUAL("", result);
    BOOST_CHECK_EQUAL(1, wtxAndFee.size());
    BOOST_CHECK_EQUAL(10 * COIN, countMintsInBalance(wtxAndFee));
    wtxAndFee.clear();
    mintedCoins.clear();

    data.v = 600 * COIN;;
    mintedCoins.clear();
    mintedCoins.push_back(data);

    result = pwalletMain->MintAndStoreSpark(mintedCoins, wtxAndFee, false, true);
    BOOST_CHECK_EQUAL("", result);
    BOOST_CHECK_GT(wtxAndFee.size(), 1);
    BOOST_CHECK_EQUAL(600 * COIN, countMintsInBalance(wtxAndFee));

    wtxAndFee.clear();
    mintedCoins.clear();

    auto balance = getAvailableCoinsForMintBalance();
    BOOST_CHECK_GT(balance, 0);

    result = pwalletMain->MintAndStoreSpark({}, wtxAndFee, false, true, true);
    BOOST_CHECK_EQUAL("", result);
    BOOST_CHECK_GT(balance, countMintsInBalance(wtxAndFee));
    BOOST_CHECK_EQUAL(balance, countMintsInBalance(wtxAndFee, true));
    BOOST_CHECK_EQUAL(0, getAvailableCoinsForMintBalance());

    scripts = generateBlocksPerScripts(500, 200);
    GenerateBlocks(100, &externalScript);

    wtxAndFee.clear();
    mintedCoins.clear();
    balance = getAvailableCoinsForMintBalance();
    BOOST_CHECK_GT(balance, 0);

    result = pwalletMain->MintAndStoreSpark({ }, wtxAndFee, false, true, true);
    BOOST_CHECK_EQUAL("", result);
    BOOST_CHECK_GT(balance, countMintsInBalance(wtxAndFee));
    BOOST_CHECK_EQUAL(balance, countMintsInBalance(wtxAndFee, true));
    BOOST_CHECK_EQUAL(0, pwalletMain->GetBalance());

    // Scripts of all changes should unique
    std::set<CScript> changeScripts;
    for (auto const &wtx : wtxAndFee) {
        for (auto const &out : wtx.first.tx->vout) {
            if (!out.scriptPubKey.IsSparkMint()) {
                BOOST_CHECK(!changeScripts.count(out.scriptPubKey));
                changeScripts.insert(out.scriptPubKey);
            }
        }
    }

    auto sparkState = spark::CSparkState::GetState();
    sparkState->Reset();
}

BOOST_AUTO_TEST_SUITE_END()
