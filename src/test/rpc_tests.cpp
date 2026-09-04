// Copyright (c) 2012-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpc/server.h"
#include "rpc/client.h"

#include "base58.h"
#include "clientversion.h"
#include "evo/evodb.h"
#include "keystore.h"
#include "llmq/quorums_instantsend.h"
#include "netbase.h"
#include "script/sign.h"
#ifdef ENABLE_WALLET
#include "spark/sparkwallet.h"
#endif
#include "streams.h"
#include "validation.h"
#include "validationinterface.h"

#include "test/test_bitcoin.h"

#include <boost/algorithm/string.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/test/unit_test.hpp>

#include <limits>

#include <univalue.h>

UniValue CallRPC(std::string args)
{
    std::vector<std::string> vArgs;
    boost::split(vArgs, args, boost::is_any_of(" \t"));
    std::string strMethod = vArgs[0];
    vArgs.erase(vArgs.begin());
    JSONRPCRequest request;
    request.strMethod = strMethod;
    request.params = RPCConvertValues(strMethod, vArgs);
    request.fHelp = false;
    BOOST_CHECK(tableRPC[strMethod]);
    rpcfn_type method = tableRPC[strMethod]->actor;
    try {
        UniValue result = (*method)(request);
        return result;
    }
    catch (const UniValue& objError) {
        throw std::runtime_error(find_value(objError, "message").get_str());
    }
}

namespace
{

class ScopedBool
{
public:
    ScopedBool(bool& value, bool replacement) : value(value), original(value)
    {
        value = replacement;
    }

    ~ScopedBool()
    {
        value = original;
    }

private:
    bool& value;
    const bool original;
};

class PopBlocksNotificationRecorder : public CValidationInterface
{
public:
    int tipNotifications{0};
    const CBlockIndex* newTip{nullptr};
    const CBlockIndex* fork{nullptr};
    std::vector<const CBlockIndex*> transactionTips;
    std::vector<int> transactionPositions;

protected:
    void UpdatedBlockTip(
        const CBlockIndex* pindexNew,
        const CBlockIndex* pindexFork,
        bool) override
    {
        ++tipNotifications;
        newTip = pindexNew;
        fork = pindexFork;
    }

    void SyncTransaction(
        const CTransaction&,
        const CBlockIndex* pindex,
        int posInBlock) override
    {
        transactionTips.push_back(pindex);
        transactionPositions.push_back(posInBlock);
    }
};

class ReentrantInvalidationRecorder :
    public CValidationInterface
{
public:
    explicit ReentrantInvalidationRecorder(
        CBlockIndex* reentrantIndex) :
        reentrantIndex(reentrantIndex)
    {
    }

    int tipNotifications{0};
    bool reentrantResult{false};

protected:
    void UpdatedBlockTip(
        const CBlockIndex*,
        const CBlockIndex*,
        bool) override
    {
        ++tipNotifications;
        if (tipNotifications != 1)
            return;

        LOCK(cs_main);
        CValidationState state;
        reentrantResult = InvalidateBlock(
            state, Params(), reentrantIndex);
    }

private:
    CBlockIndex* reentrantIndex;
};

class ScopedValidationRegistration
{
public:
    explicit ScopedValidationRegistration(
        CValidationInterface& validationInterface) :
        validationInterface(validationInterface)
    {
        RegisterValidationInterface(&validationInterface);
    }

    ~ScopedValidationRegistration()
    {
        UnregisterValidationInterface(&validationInterface);
    }

private:
    CValidationInterface& validationInterface;
};

} // namespace


BOOST_FIXTURE_TEST_SUITE(rpc_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(rpc_rawparams)
{
    // Test raw transaction API argument handling
    UniValue r;

    BOOST_CHECK_THROW(CallRPC("getrawtransaction"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("getrawtransaction not_hex"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("getrawtransaction a3b807410df0b60fcb9736768df5823938b2f838694939ba45f3c0a1bff150ed not_int"), std::runtime_error);

    BOOST_CHECK_THROW(CallRPC("createrawtransaction"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("createrawtransaction null null"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("createrawtransaction not_array"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("createrawtransaction [] []"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("createrawtransaction {} {}"), std::runtime_error);
    BOOST_CHECK_NO_THROW(CallRPC("createrawtransaction [] {}"));
    BOOST_CHECK_THROW(CallRPC("createrawtransaction [] {} extra"), std::runtime_error);

    BOOST_CHECK_THROW(CallRPC("decoderawtransaction"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("decoderawtransaction null"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("decoderawtransaction DEADBEEF"), std::runtime_error);
    std::string rawtx = "0100000001a15d57094aa7a21a28cb20b59aab8fc7d1149a3bdbcddba9c622e4f5f6a99ece010000006c493046022100f93bb0e7d8db7bd46e40132d1f8242026e045f03a0efe71bbb8e3f475e970d790221009337cd7f1f929f00cc6ff01f03729b069a7c21b59b1736ddfee5db5946c5da8c0121033b9b137ee87d5a812d6f506efdd37f0affa7ffc310711c06c7f3e097c9447c52ffffffff0100e1f505000000001976a9140389035a9225b3839e2bbf32d826a1e222031fd888ac00000000";
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("decoderawtransaction ")+rawtx));
    BOOST_CHECK_EQUAL(find_value(r.get_obj(), "size").get_int(), 193);
    BOOST_CHECK_EQUAL(find_value(r.get_obj(), "version").get_int(), 1);
    BOOST_CHECK_EQUAL(find_value(r.get_obj(), "locktime").get_int(), 0);
    BOOST_CHECK_THROW(r = CallRPC(std::string("decoderawtransaction ")+rawtx+" extra"), std::runtime_error);

    BOOST_CHECK_THROW(CallRPC("signrawtransaction"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("signrawtransaction null"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("signrawtransaction ff00"), std::runtime_error);
    BOOST_CHECK_NO_THROW(CallRPC(std::string("signrawtransaction ")+rawtx));
    BOOST_CHECK_NO_THROW(CallRPC(std::string("signrawtransaction ")+rawtx+" null null NONE|ANYONECANPAY"));
    BOOST_CHECK_NO_THROW(CallRPC(std::string("signrawtransaction ")+rawtx+" [] [] NONE|ANYONECANPAY"));
    BOOST_CHECK_THROW(CallRPC(std::string("signrawtransaction ")+rawtx+" null null badenum"), std::runtime_error);

    // Only check failure cases for sendrawtransaction, there's no network to send to...
    BOOST_CHECK_THROW(CallRPC("sendrawtransaction"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("sendrawtransaction null"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("sendrawtransaction DEADBEEF"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC(std::string("sendrawtransaction ")+rawtx+" extra"), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(rpc_togglenetwork)
{
    UniValue r;

    r = CallRPC("getnetworkinfo");
    bool netState = find_value(r.get_obj(), "networkactive").get_bool();
    BOOST_CHECK_EQUAL(netState, true);

    BOOST_CHECK_NO_THROW(CallRPC("setnetworkactive false"));
    r = CallRPC("getnetworkinfo");
    int numConnection = find_value(r.get_obj(), "connections").get_int();
    BOOST_CHECK_EQUAL(numConnection, 0);

    netState = find_value(r.get_obj(), "networkactive").get_bool();
    BOOST_CHECK_EQUAL(netState, false);

    BOOST_CHECK_NO_THROW(CallRPC("setnetworkactive true"));
    r = CallRPC("getnetworkinfo");
    netState = find_value(r.get_obj(), "networkactive").get_bool();
    BOOST_CHECK_EQUAL(netState, true);
}

/*BOOST_AUTO_TEST_CASE(rpc_rawsign)
{
    UniValue r;
    // input is a 1-of-2 multisig (so is output):
    std::string prevout =
      "[{\"txid\":\"b4cc287e58f87cdae59417329f710f3ecd75a4ee1d2872b7248f50977c8493f3\","
      "\"vout\":1,\"scriptPubKey\":\"a914b10c9df5f7edf436c697f02f1efdba4cf399615187\","
      "\"redeemScript\":\"512103debedc17b3df2badbcdd86d5feb4562b86fe182e5998abd8bcd4f122c6155b1b21027e940bb73ab8732bfdf7f9216ecefca5b94d6df834e77e108f68e66f126044c052ae\"}]";
    r = CallRPC(std::string("createrawtransaction ")+prevout+" "+
      bitcoin_address_to_firo("{\"3HqAe9LtNBjnsfM4CyYaWTnvCaUYT7v4oZ\":11}"));
    std::string notsigned = r.get_str();
    std::string privkey1 = "\"KzsXybp9jX64P5ekX1KUxRQ79Jht9uzW7LorgwE65i5rWACL6LQe\"";
    std::string privkey2 = "\"Kyhdf5LuKTRx4ge69ybABsiUAWjVRK4XGxAKk2FQLp2HjGMy87Z4\"";
    r = CallRPC(std::string("signrawtransaction ")+notsigned+" "+prevout+" "+"[]");
    BOOST_CHECK(find_value(r.get_obj(), "complete").get_bool() == false);
    r = CallRPC(std::string("signrawtransaction ")+notsigned+" "+prevout+" "+"["+privkey1+","+privkey2+"]");
    BOOST_CHECK(find_value(r.get_obj(), "complete").get_bool() == true);
}*/

BOOST_AUTO_TEST_CASE(rpc_createraw_op_return)
{
    BOOST_CHECK_NO_THROW(CallRPC("createrawtransaction [{\"txid\":\"a3b807410df0b60fcb9736768df5823938b2f838694939ba45f3c0a1bff150ed\",\"vout\":0}] {\"data\":\"68656c6c6f776f726c64\"}"));

    // Allow more than one data transaction output
    BOOST_CHECK_NO_THROW(CallRPC("createrawtransaction [{\"txid\":\"a3b807410df0b60fcb9736768df5823938b2f838694939ba45f3c0a1bff150ed\",\"vout\":0}] {\"data\":\"68656c6c6f776f726c64\",\"data\":\"68656c6c6f776f726c64\"}"));

    // Key not "data" (bad address)
    BOOST_CHECK_THROW(CallRPC("createrawtransaction [{\"txid\":\"a3b807410df0b60fcb9736768df5823938b2f838694939ba45f3c0a1bff150ed\",\"vout\":0}] {\"somedata\":\"68656c6c6f776f726c64\"}"), std::runtime_error);

    // Bad hex encoding of data output
    BOOST_CHECK_THROW(CallRPC("createrawtransaction [{\"txid\":\"a3b807410df0b60fcb9736768df5823938b2f838694939ba45f3c0a1bff150ed\",\"vout\":0}] {\"data\":\"12345\"}"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("createrawtransaction [{\"txid\":\"a3b807410df0b60fcb9736768df5823938b2f838694939ba45f3c0a1bff150ed\",\"vout\":0}] {\"data\":\"12345g\"}"), std::runtime_error);

    // Data 81 bytes long
    BOOST_CHECK_NO_THROW(CallRPC("createrawtransaction [{\"txid\":\"a3b807410df0b60fcb9736768df5823938b2f838694939ba45f3c0a1bff150ed\",\"vout\":0}] {\"data\":\"010203040506070809101112131415161718192021222324252627282930313233343536373839404142434445464748495051525354555657585960616263646566676869707172737475767778798081\"}"));
}

BOOST_AUTO_TEST_CASE(rpc_format_monetary_values)
{
    BOOST_CHECK(ValueFromAmount(0LL).write() == "0.00000000");
    BOOST_CHECK(ValueFromAmount(1LL).write() == "0.00000001");
    BOOST_CHECK(ValueFromAmount(17622195LL).write() == "0.17622195");
    BOOST_CHECK(ValueFromAmount(50000000LL).write() == "0.50000000");
    BOOST_CHECK(ValueFromAmount(89898989LL).write() == "0.89898989");
    BOOST_CHECK(ValueFromAmount(100000000LL).write() == "1.00000000");
    BOOST_CHECK(ValueFromAmount(2099999999999990LL).write() == "20999999.99999990");
    BOOST_CHECK(ValueFromAmount(2099999999999999LL).write() == "20999999.99999999");

    BOOST_CHECK_EQUAL(ValueFromAmount(0).write(), "0.00000000");
    BOOST_CHECK_EQUAL(ValueFromAmount((COIN/10000)*123456789).write(), "12345.67890000");
    BOOST_CHECK_EQUAL(ValueFromAmount(-COIN).write(), "-1.00000000");
    BOOST_CHECK_EQUAL(ValueFromAmount(-COIN/10).write(), "-0.10000000");

    BOOST_CHECK_EQUAL(ValueFromAmount(COIN*100000000).write(), "100000000.00000000");
    BOOST_CHECK_EQUAL(ValueFromAmount(COIN*10000000).write(), "10000000.00000000");
    BOOST_CHECK_EQUAL(ValueFromAmount(COIN*1000000).write(), "1000000.00000000");
    BOOST_CHECK_EQUAL(ValueFromAmount(COIN*100000).write(), "100000.00000000");
    BOOST_CHECK_EQUAL(ValueFromAmount(COIN*10000).write(), "10000.00000000");
    BOOST_CHECK_EQUAL(ValueFromAmount(COIN*1000).write(), "1000.00000000");
    BOOST_CHECK_EQUAL(ValueFromAmount(COIN*100).write(), "100.00000000");
    BOOST_CHECK_EQUAL(ValueFromAmount(COIN*10).write(), "10.00000000");
    BOOST_CHECK_EQUAL(ValueFromAmount(COIN).write(), "1.00000000");
    BOOST_CHECK_EQUAL(ValueFromAmount(COIN/10).write(), "0.10000000");
    BOOST_CHECK_EQUAL(ValueFromAmount(COIN/100).write(), "0.01000000");
    BOOST_CHECK_EQUAL(ValueFromAmount(COIN/1000).write(), "0.00100000");
    BOOST_CHECK_EQUAL(ValueFromAmount(COIN/10000).write(), "0.00010000");
    BOOST_CHECK_EQUAL(ValueFromAmount(COIN/100000).write(), "0.00001000");
    BOOST_CHECK_EQUAL(ValueFromAmount(COIN/1000000).write(), "0.00000100");
    BOOST_CHECK_EQUAL(ValueFromAmount(COIN/10000000).write(), "0.00000010");
    BOOST_CHECK_EQUAL(ValueFromAmount(COIN/100000000).write(), "0.00000001");
}

static UniValue ValueFromString(const std::string &str)
{
    UniValue value;
    BOOST_CHECK(value.setNumStr(str));
    return value;
}

BOOST_AUTO_TEST_CASE(rpc_parse_monetary_values)
{
    BOOST_CHECK_THROW(AmountFromValue(ValueFromString("-0.00000001")), UniValue);
    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("0")), 0LL);
    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("0.00000000")), 0LL);
    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("0.00000001")), 1LL);
    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("0.17622195")), 17622195LL);
    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("0.5")), 50000000LL);
    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("0.50000000")), 50000000LL);
    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("0.89898989")), 89898989LL);
    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("1.00000000")), 100000000LL);
    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("20999999.9999999")), 2099999999999990LL);
    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("20999999.99999999")), 2099999999999999LL);

    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("1e-8")), COIN/100000000);
    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("0.1e-7")), COIN/100000000);
    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("0.01e-6")), COIN/100000000);
    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("0.0000000000000000000000000000000000000000000000000000000000000000000000000001e+68")), COIN/100000000);
    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("10000000000000000000000000000000000000000000000000000000000000000e-64")), COIN);
    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("0.000000000000000000000000000000000000000000000000000000000000000100000000000000000000000000000000000000000000000000000e64")), COIN);

    BOOST_CHECK_THROW(AmountFromValue(ValueFromString("1e-9")), UniValue); //should fail
    BOOST_CHECK_THROW(AmountFromValue(ValueFromString("0.000000019")), UniValue); //should fail
    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("0.00000001000000")), 1LL); //should pass, cut trailing 0
    BOOST_CHECK_THROW(AmountFromValue(ValueFromString("19e-9")), UniValue); //should fail
    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("0.19e-6")), 19); //should pass, leading 0 is present

    BOOST_CHECK_THROW(AmountFromValue(ValueFromString("92233720368.54775808")), UniValue); //overflow error
    BOOST_CHECK_THROW(AmountFromValue(ValueFromString("1e+11")), UniValue); //overflow error
    BOOST_CHECK_THROW(AmountFromValue(ValueFromString("1e11")), UniValue); //overflow error signless
    BOOST_CHECK_THROW(AmountFromValue(ValueFromString("93e+9")), UniValue); //overflow error
}

BOOST_AUTO_TEST_CASE(json_parse_errors)
{
    // Valid
    BOOST_CHECK_EQUAL(ParseNonRFCJSONValue("1.0").get_real(), 1.0);
    // Valid, with leading or trailing whitespace
    BOOST_CHECK_EQUAL(ParseNonRFCJSONValue(" 1.0").get_real(), 1.0);
    BOOST_CHECK_EQUAL(ParseNonRFCJSONValue("1.0 ").get_real(), 1.0);

    BOOST_CHECK_THROW(AmountFromValue(ParseNonRFCJSONValue(".19e-6")), std::runtime_error); //should fail, missing leading 0, therefore invalid JSON
    BOOST_CHECK_EQUAL(AmountFromValue(ParseNonRFCJSONValue("0.00000000000000000000000000000000000001e+30 ")), 1);
    // Invalid, initial garbage
    BOOST_CHECK_THROW(ParseNonRFCJSONValue("[1.0"), std::runtime_error);
    BOOST_CHECK_THROW(ParseNonRFCJSONValue("a1.0"), std::runtime_error);
    // Invalid, trailing garbage
    BOOST_CHECK_THROW(ParseNonRFCJSONValue("1.0sds"), std::runtime_error);
    BOOST_CHECK_THROW(ParseNonRFCJSONValue("1.0]"), std::runtime_error);
    // BTC addresses should fail parsing
    BOOST_CHECK_THROW(ParseNonRFCJSONValue("175tWpb8K1S7NmH4Zx6rewF9WQrcZv245W"), std::runtime_error);
    BOOST_CHECK_THROW(ParseNonRFCJSONValue("3J98t1WpEZ73CNmQviecrnyiWrnqRhWNL"), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(rpc_ban)
{
    BOOST_CHECK_NO_THROW(CallRPC(std::string("clearbanned")));

    UniValue r;
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("setban 127.0.0.0 add")));
    BOOST_CHECK_THROW(r = CallRPC(std::string("setban 127.0.0.0:8334")), std::runtime_error); //portnumber for setban not allowed
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("listbanned")));
    UniValue ar = r.get_array();
    UniValue o1 = ar[0].get_obj();
    UniValue adr = find_value(o1, "address");
    BOOST_CHECK_EQUAL(adr.get_str(), "127.0.0.0/32");
    BOOST_CHECK_NO_THROW(CallRPC(std::string("setban 127.0.0.0 remove")));
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("listbanned")));
    ar = r.get_array();
    BOOST_CHECK_EQUAL(ar.size(), 0);

    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("setban 127.0.0.0/24 add 9907731200 true")));
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("listbanned")));
    ar = r.get_array();
    o1 = ar[0].get_obj();
    adr = find_value(o1, "address");
    UniValue banned_until = find_value(o1, "banned_until");
    BOOST_CHECK_EQUAL(adr.get_str(), "127.0.0.0/24");
    BOOST_CHECK_EQUAL(banned_until.get_int64(), 9907731200); // absolute time check

    BOOST_CHECK_NO_THROW(CallRPC(std::string("clearbanned")));

    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("setban 127.0.0.0/24 add 200")));
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("listbanned")));
    ar = r.get_array();
    o1 = ar[0].get_obj();
    adr = find_value(o1, "address");
    banned_until = find_value(o1, "banned_until");
    BOOST_CHECK_EQUAL(adr.get_str(), "127.0.0.0/24");
    int64_t now = GetTime();
    BOOST_CHECK(banned_until.get_int64() > now);
    BOOST_CHECK(banned_until.get_int64()-now <= 200);

    // must throw an exception because 127.0.0.1 is in already banned suubnet range
    BOOST_CHECK_THROW(r = CallRPC(std::string("setban 127.0.0.1 add")), std::runtime_error);

    BOOST_CHECK_NO_THROW(CallRPC(std::string("setban 127.0.0.0/24 remove")));
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("listbanned")));
    ar = r.get_array();
    BOOST_CHECK_EQUAL(ar.size(), 0);

    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("setban 127.0.0.0/255.255.0.0 add")));
    BOOST_CHECK_THROW(r = CallRPC(std::string("setban 127.0.1.1 add")), std::runtime_error);

    BOOST_CHECK_NO_THROW(CallRPC(std::string("clearbanned")));
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("listbanned")));
    ar = r.get_array();
    BOOST_CHECK_EQUAL(ar.size(), 0);


    BOOST_CHECK_THROW(r = CallRPC(std::string("setban test add")), std::runtime_error); //invalid IP

    //IPv6 tests
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("setban FE80:0000:0000:0000:0202:B3FF:FE1E:8329 add")));
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("listbanned")));
    ar = r.get_array();
    o1 = ar[0].get_obj();
    adr = find_value(o1, "address");
    BOOST_CHECK_EQUAL(adr.get_str(), "fe80::202:b3ff:fe1e:8329/128");

    BOOST_CHECK_NO_THROW(CallRPC(std::string("clearbanned")));
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("setban 2001:db8::/ffff:fffc:0:0:0:0:0:0 add")));
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("listbanned")));
    ar = r.get_array();
    o1 = ar[0].get_obj();
    adr = find_value(o1, "address");
    BOOST_CHECK_EQUAL(adr.get_str(), "2001:db8::/30");

    BOOST_CHECK_NO_THROW(CallRPC(std::string("clearbanned")));
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("setban 2001:4d48:ac57:400:cacf:e9ff:fe1d:9c63/128 add")));
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("listbanned")));
    ar = r.get_array();
    o1 = ar[0].get_obj();
    adr = find_value(o1, "address");
    BOOST_CHECK_EQUAL(adr.get_str(), "2001:4d48:ac57:400:cacf:e9ff:fe1d:9c63/128");
}

BOOST_AUTO_TEST_CASE(rpc_convert_values_generatetoaddress)
{
    UniValue result;

    BOOST_CHECK_NO_THROW(result = RPCConvertValues("generatetoaddress", boost::assign::list_of("101")("mkESjLZW66TmHhiFX8MCaBjrhZ543PPh9a")));
    BOOST_CHECK_EQUAL(result[0].get_int(), 101);
    BOOST_CHECK_EQUAL(result[1].get_str(), "mkESjLZW66TmHhiFX8MCaBjrhZ543PPh9a");

    BOOST_CHECK_NO_THROW(result = RPCConvertValues("generatetoaddress", boost::assign::list_of("101")("mhMbmE2tE9xzJYCV9aNC8jKWN31vtGrguU")));
    BOOST_CHECK_EQUAL(result[0].get_int(), 101);
    BOOST_CHECK_EQUAL(result[1].get_str(), "mhMbmE2tE9xzJYCV9aNC8jKWN31vtGrguU");

    BOOST_CHECK_NO_THROW(result = RPCConvertValues("generatetoaddress", boost::assign::list_of("1")("mkESjLZW66TmHhiFX8MCaBjrhZ543PPh9a")("9")));
    BOOST_CHECK_EQUAL(result[0].get_int(), 1);
    BOOST_CHECK_EQUAL(result[1].get_str(), "mkESjLZW66TmHhiFX8MCaBjrhZ543PPh9a");
    BOOST_CHECK_EQUAL(result[2].get_int(), 9);

    BOOST_CHECK_NO_THROW(result = RPCConvertValues("generatetoaddress", boost::assign::list_of("1")("mhMbmE2tE9xzJYCV9aNC8jKWN31vtGrguU")("9")));
    BOOST_CHECK_EQUAL(result[0].get_int(), 1);
    BOOST_CHECK_EQUAL(result[1].get_str(), "mhMbmE2tE9xzJYCV9aNC8jKWN31vtGrguU");
    BOOST_CHECK_EQUAL(result[2].get_int(), 9);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE(popblocks_rpc_tests, TestChain100Setup)

BOOST_AUTO_TEST_CASE(popblocks_forgets_block_data)
{
    CBlockIndex* oldTip;
    CBlockIndex* oldParent;
    CBlock oldTipBlock;
    CBlock oldParentBlock;
    {
        LOCK(cs_main);
        oldTip = chainActive.Tip();
        oldParent = oldTip->pprev;
        BOOST_REQUIRE_EQUAL(chainActive.Height(), 100);
        BOOST_REQUIRE(ReadBlockFromDisk(oldTipBlock, oldTip, Params().GetConsensus()));
        BOOST_REQUIRE(ReadBlockFromDisk(oldParentBlock, oldParent, Params().GetConsensus()));
    }

    ScopedBool checkBlockIndex(fCheckBlockIndex, true);
    BOOST_CHECK_EQUAL(CallRPC("popblocks 2").get_int(), 98);

    {
        LOCK(cs_main);
        BOOST_CHECK_EQUAL(chainActive.Height(), 98);
        for (CBlockIndex* pindex : {oldParent, oldTip}) {
            BOOST_CHECK_EQUAL(pindex->nStatus & (BLOCK_HAVE_DATA | BLOCK_HAVE_UNDO), 0U);
            BOOST_CHECK_EQUAL(pindex->nStatus & BLOCK_FAILED_MASK, 0U);
            BOOST_CHECK_EQUAL(pindex->nStatus & BLOCK_VALID_MASK, BLOCK_VALID_TREE);
            BOOST_CHECK_EQUAL(pindex->nFile, 0);
            BOOST_CHECK_EQUAL(pindex->nDataPos, 0U);
            BOOST_CHECK_EQUAL(pindex->nUndoPos, 0U);
            BOOST_CHECK_EQUAL(pindex->nTx, 0U);
            BOOST_CHECK_EQUAL(pindex->nChainTx, 0U);
            BOOST_CHECK_EQUAL(pindex->nSequenceId, 0);
        }
    }

    bool newBlock = false;
    BOOST_REQUIRE(ProcessNewBlock(Params(), std::make_shared<const CBlock>(oldParentBlock), true, &newBlock));
    BOOST_CHECK(newBlock);
    BOOST_REQUIRE(ProcessNewBlock(Params(), std::make_shared<const CBlock>(oldTipBlock), true, &newBlock));
    BOOST_CHECK(newBlock);
    BOOST_CHECK_EQUAL(chainActive.Height(), 100);
}

BOOST_AUTO_TEST_CASE(popblocks_rejects_invalid_counts)
{
    BOOST_CHECK_THROW(CallRPC("popblocks 0"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("popblocks -1"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("popblocks 101"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("popblocks true"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("popblocks 2147483648"), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(disconnect_mask_skips_later_duplicate_txids)
{
    CMutableTransaction transaction;
    transaction.vin.emplace_back(
        COutPoint(uint256S("01"), 0));
    transaction.vout.emplace_back(
        CENT, CScript() << OP_TRUE);
    const CTransactionRef duplicate =
        MakeTransactionRef(transaction);

    CBlock block;
    block.vtx = {duplicate, duplicate};
    const std::vector<bool> undoTransaction =
        GetBlockUndoTransactionMask(block);
    BOOST_REQUIRE_EQUAL(undoTransaction.size(), 2U);
    BOOST_CHECK(undoTransaction[0]);
    BOOST_CHECK(!undoTransaction[1]);
}

BOOST_AUTO_TEST_CASE(instantsend_input_request_id_uses_only_prevout)
{
    const COutPoint prevout(uint256S("01"), 2);
    CTxIn first(prevout);
    CTxIn second(prevout);
    first.scriptSig = CScript() << 1;
    second.scriptSig = CScript() << 2;
    first.nSequence = 1;
    second.nSequence = 2;

    BOOST_CHECK(
        llmq::GetInstantSendInputRequestId(first) ==
        llmq::GetInstantSendInputRequestId(second));
    BOOST_CHECK(
        llmq::GetInstantSendInputRequestId(first) ==
        llmq::GetInstantSendInputRequestId(prevout));
}

BOOST_AUTO_TEST_CASE(popblocks_recovery_repairs_evodb_after_split_flush)
{
    uint256 initialTipHash;
    CBlockIndex* forgottenIndex;
    {
        LOCK(cs_main);
        forgottenIndex = chainActive.Tip();
        initialTipHash =
            forgottenIndex->GetBlockHash();
    }

    BOOST_REQUIRE(DisconnectBlocks(1));
    BOOST_REQUIRE(pcoinsTip->Flush());
    {
        auto dbTx = evoDb->BeginTransaction();
        evoDb->WriteBestBlock(initialTipHash);
        dbTx->Commit();
    }
    BOOST_REQUIRE(evoDb->CommitRootTransaction());
    BOOST_REQUIRE(
        evoDb->WritePopBlocksRecovery(
            initialTipHash,
            pcoinsTip->GetBestBlock(),
            false,
            {}));

    BOOST_REQUIRE(
        RecoverInterruptedPopBlocks(Params()));
    uint256 evoTipHash;
    BOOST_REQUIRE(evoDb->ReadBestBlock(evoTipHash));
    {
        LOCK(cs_main);
        BOOST_CHECK(
            evoTipHash ==
            chainActive.Tip()->GetBlockHash());
    }
    BOOST_CHECK(evoDb->HasPopBlocksRecovery());

    BOOST_REQUIRE(
        FinishInterruptedPopBlocks(Params()));
    BOOST_CHECK(!evoDb->HasPopBlocksRecovery());
    BOOST_CHECK_EQUAL(
        forgottenIndex->nStatus & BLOCK_HAVE_MASK,
        0U);
}

BOOST_AUTO_TEST_CASE(popblocks_recovery_finishes_partial_durable_rollback)
{
    CreateAndProcessBlock({}, coinbaseKey);
    CreateAndProcessBlock({}, coinbaseKey);

    CBlockIndex* initialTip;
    CBlockIndex* targetTip;
    {
        LOCK(cs_main);
        initialTip = chainActive.Tip();
        targetTip = chainActive[100];
    }

    BOOST_REQUIRE(DisconnectBlocks(1));
    FlushStateToDisk();
    BOOST_REQUIRE(
        evoDb->WritePopBlocksRecovery(
            initialTip->GetBlockHash(),
            targetTip->GetBlockHash(),
            false,
            {}));

    BOOST_REQUIRE(
        RecoverInterruptedPopBlocks(Params()));
    uint256 evoTipHash;
    BOOST_REQUIRE(evoDb->ReadBestBlock(evoTipHash));
    {
        LOCK(cs_main);
        BOOST_CHECK_EQUAL(chainActive.Tip(), targetTip);
        BOOST_CHECK(
            pcoinsTip->GetBestBlock() ==
            targetTip->GetBlockHash());
        BOOST_CHECK(
            evoTipHash ==
            targetTip->GetBlockHash());
    }
    BOOST_CHECK(evoDb->HasPopBlocksRecovery());

    BOOST_REQUIRE(
        FinishInterruptedPopBlocks(Params()));
    BOOST_CHECK(!evoDb->HasPopBlocksRecovery());
}

BOOST_AUTO_TEST_CASE(popblocks_recovery_finishes_metadata_phase)
{
    CBlockIndex* forgottenIndex;
    CBlockIndex* targetTip;
    {
        LOCK(cs_main);
        forgottenIndex = chainActive.Tip();
        targetTip = forgottenIndex->pprev;
    }

    BOOST_REQUIRE(DisconnectBlocks(1));
    FlushStateToDisk();
    BOOST_REQUIRE(
        evoDb->WritePopBlocksMempoolCleanup());
    BOOST_REQUIRE(
        evoDb->WritePopBlocksRecovery(
            forgottenIndex->GetBlockHash(),
            targetTip->GetBlockHash(),
            true,
            {}));

    BOOST_REQUIRE(
        RecoverInterruptedPopBlocks(Params()));
    BOOST_CHECK(!evoDb->HasPopBlocksRecovery());
    BOOST_CHECK_EQUAL(
        forgottenIndex->nStatus & BLOCK_HAVE_MASK,
        0U);
    BOOST_CHECK_EQUAL(
        forgottenIndex->nTx,
        0U);
}

BOOST_AUTO_TEST_CASE(popblocks_recovery_clears_completed_marker_after_resync)
{
    CBlock oldTipBlock;
    CBlockIndex* initialTip;
    CBlockIndex* targetTip;
    {
        LOCK(cs_main);
        initialTip = chainActive.Tip();
        targetTip = initialTip->pprev;
        BOOST_REQUIRE(ReadBlockFromDisk(
            oldTipBlock,
            initialTip,
            Params().GetConsensus()));
    }

    BOOST_CHECK_EQUAL(
        CallRPC("popblocks 1").get_int(),
        targetTip->nHeight);
    BOOST_REQUIRE(
        evoDb->WritePopBlocksRecovery(
            initialTip->GetBlockHash(),
            targetTip->GetBlockHash(),
            true,
            {}));

    bool newBlock = false;
    BOOST_REQUIRE(ProcessNewBlock(
        Params(),
        std::make_shared<const CBlock>(oldTipBlock),
        true,
        &newBlock));
    BOOST_REQUIRE(newBlock);
    BOOST_REQUIRE_EQUAL(chainActive.Tip(), initialTip);

    BOOST_REQUIRE(
        RecoverInterruptedPopBlocks(Params()));
    BOOST_CHECK(!evoDb->HasPopBlocksRecovery());
    BOOST_CHECK_EQUAL(chainActive.Tip(), initialTip);
}

BOOST_AUTO_TEST_CASE(popblocks_rejects_missing_undo_without_disconnect)
{
    CBlockIndex* oldTip;
    unsigned int oldStatus;
    {
        LOCK(cs_main);
        oldTip = chainActive.Tip();
        oldStatus = oldTip->nStatus;
        oldTip->nStatus &= ~BLOCK_HAVE_UNDO;
    }

    BOOST_CHECK_EXCEPTION(
        CallRPC("popblocks 1"),
        std::runtime_error,
        HasReason("local block and undo data"));
    LOCK(cs_main);
    oldTip->nStatus = oldStatus;
    BOOST_CHECK_EQUAL(chainActive.Tip(), oldTip);
    BOOST_CHECK_EQUAL(chainActive.Height(), 100);
}

BOOST_AUTO_TEST_CASE(popblocks_rejects_unreadable_undo_without_disconnect)
{
    CreateAndProcessBlock({}, coinbaseKey);
    CreateAndProcessBlock({}, coinbaseKey);

    CBlockIndex* corruptIndex;
    unsigned int undoPos;
    {
        LOCK(cs_main);
        corruptIndex = chainActive[101];
        undoPos = corruptIndex->nUndoPos;
        corruptIndex->nUndoPos = std::numeric_limits<unsigned int>::max();
    }

    BOOST_CHECK_EXCEPTION(
        CallRPC("popblocks 2"),
        std::runtime_error,
        HasReason("failed to read local undo data"));

    LOCK(cs_main);
    corruptIndex->nUndoPos = undoPos;
    BOOST_CHECK_EQUAL(chainActive.Height(), 102);
}

BOOST_AUTO_TEST_CASE(popblocks_rejects_wrong_transaction_count_without_disconnect)
{
    CBlockIndex* oldTip;
    CBlockIndex* corruptIndex;
    unsigned int transactionCount;
    {
        LOCK(cs_main);
        oldTip = chainActive.Tip();
        corruptIndex = chainActive[99];
        transactionCount = corruptIndex->nTx;
        ++corruptIndex->nTx;
    }

    BOOST_CHECK_EXCEPTION(
        CallRPC("popblocks 2"),
        std::runtime_error,
        HasReason("transaction count mismatch"));

    LOCK(cs_main);
    corruptIndex->nTx = transactionCount;
    BOOST_CHECK_EQUAL(chainActive.Tip(), oldTip);
    BOOST_CHECK_EQUAL(chainActive.Height(), 100);
}

BOOST_AUTO_TEST_CASE(popblocks_clears_failed_side_branch_metadata)
{
    CBlock parentBlock;
    CBlockIndex* parentIndex;
    {
        LOCK(cs_main);
        parentIndex = chainActive.Tip();
        BOOST_REQUIRE(ReadBlockFromDisk(parentBlock, parentIndex, Params().GetConsensus()));
    }

    const CBlock invalidatedBlock = CreateAndProcessBlock({}, coinbaseKey);
    CBlockIndex* invalidatedIndex;
    {
        LOCK(cs_main);
        invalidatedIndex = mapBlockIndex.at(invalidatedBlock.GetHash());
        CValidationState state;
        BOOST_REQUIRE(InvalidateBlock(state, Params(), invalidatedIndex));
        BOOST_REQUIRE(invalidatedIndex->nStatus & BLOCK_FAILED_MASK);
        BOOST_REQUIRE_EQUAL(chainActive.Height(), 100);
    }

    BOOST_CHECK_EQUAL(CallRPC("popblocks 1").get_int(), 99);
    {
        LOCK(cs_main);
        for (CBlockIndex* pindex : {parentIndex, invalidatedIndex}) {
            BOOST_CHECK_EQUAL(pindex->nStatus & BLOCK_FAILED_MASK, 0U);
            BOOST_CHECK_EQUAL(pindex->nStatus & BLOCK_HAVE_MASK, 0U);
            BOOST_CHECK_EQUAL(pindex->nStatus & BLOCK_VALID_MASK, BLOCK_VALID_TREE);
            BOOST_CHECK_EQUAL(pindex->nFile, 0);
            BOOST_CHECK_EQUAL(pindex->nDataPos, 0U);
            BOOST_CHECK_EQUAL(pindex->nUndoPos, 0U);
            BOOST_CHECK_EQUAL(pindex->nTx, 0U);
            BOOST_CHECK_EQUAL(pindex->nChainTx, 0U);
            BOOST_CHECK_EQUAL(pindex->nSequenceId, 0);
        }
    }

    bool newBlock = false;
    BOOST_REQUIRE(ProcessNewBlock(Params(), std::make_shared<const CBlock>(parentBlock), true, &newBlock));
    BOOST_CHECK(newBlock);
    BOOST_REQUIRE(ProcessNewBlock(Params(), std::make_shared<const CBlock>(invalidatedBlock), true, &newBlock));
    BOOST_CHECK(newBlock);
    BOOST_CHECK_EQUAL(chainActive.Height(), 101);
}

BOOST_AUTO_TEST_CASE(popblocks_preserves_transaction_index)
{
    ScopedBool txIndex(fTxIndex, true);
    const CBlock block = CreateAndProcessBlock({}, coinbaseKey);
    const uint256 txid = block.vtx[0]->GetHash();
    CDiskTxPos oldPos;
    BOOST_REQUIRE(pblocktree->ReadTxIndex(txid, oldPos));

    BOOST_CHECK_EQUAL(CallRPC("popblocks 1").get_int(), 100);
    CDiskTxPos newPos;
    BOOST_REQUIRE(pblocktree->ReadTxIndex(txid, newPos));
    BOOST_CHECK_EQUAL(newPos.nFile, oldPos.nFile);
    BOOST_CHECK_EQUAL(newPos.nPos, oldPos.nPos);
    BOOST_CHECK_EQUAL(newPos.nTxOffset, oldPos.nTxOffset);
}

BOOST_AUTO_TEST_CASE(popblocks_rejects_optional_indexes_without_disconnect)
{
    CBlockIndex* oldTip;
    {
        LOCK(cs_main);
        oldTip = chainActive.Tip();
    }

    {
        ScopedBool addressIndex(fAddressIndex, true);
        BOOST_CHECK_EXCEPTION(
            CallRPC("popblocks 1"),
            std::runtime_error,
            HasReason("address or spent index"));
        LOCK(cs_main);
        BOOST_CHECK_EQUAL(chainActive.Tip(), oldTip);
    }

    {
        ScopedBool spentIndex(fSpentIndex, true);
        BOOST_CHECK_EXCEPTION(
            CallRPC("popblocks 1"),
            std::runtime_error,
            HasReason("address or spent index"));
        LOCK(cs_main);
        BOOST_CHECK_EQUAL(chainActive.Tip(), oldTip);
    }
}

#ifdef ENABLE_WALLET
BOOST_AUTO_TEST_CASE(popblocks_rejects_disabled_wallet_without_disconnect)
{
    CBlockIndex* oldTip;
    {
        LOCK(cs_main);
        oldTip = chainActive.Tip();
    }

    ForceSetArg("-disablewallet", "1");
    BOOST_CHECK_EXCEPTION(
        CallRPC("popblocks 1"),
        std::runtime_error,
        HasReason("wallet loading is disabled"));
    ForceSetArg("-disablewallet", "0");

    LOCK(cs_main);
    BOOST_CHECK_EQUAL(chainActive.Tip(), oldTip);
}

BOOST_AUTO_TEST_CASE(popblocks_recovery_rejects_a_different_wallet)
{
    CBlockIndex* initialTip;
    CBlockIndex* targetTip;
    {
        LOCK(cs_main);
        initialTip = chainActive.Tip();
        targetTip = initialTip->pprev;
    }

    BOOST_REQUIRE(DisconnectBlocks(1));
    FlushStateToDisk();

    uint256 wrongViewKeyHash =
        pwalletMain->sparkWallet->GetFullViewKeyHash();
    wrongViewKeyHash.begin()[0] ^= 1;
    std::vector<uint256> mintHashes;
    std::vector<GroupElement> lTags;
    CDataStream cleanupStream(
        SER_DISK, CLIENT_VERSION);
    cleanupStream << uint8_t{2};
    cleanupStream << true;
    cleanupStream << wrongViewKeyHash;
    cleanupStream << mintHashes;
    cleanupStream << lTags;
    const std::vector<unsigned char> cleanupData(
        cleanupStream.begin(),
        cleanupStream.end());
    BOOST_REQUIRE(
        evoDb->WritePopBlocksRecovery(
            initialTip->GetBlockHash(),
            targetTip->GetBlockHash(),
            false,
            cleanupData));

    BOOST_CHECK(
        !FinishInterruptedPopBlocks(Params()));
    BOOST_CHECK(evoDb->HasPopBlocksRecovery());
    BOOST_REQUIRE(evoDb->ErasePopBlocksRecovery());
}
#endif

BOOST_AUTO_TEST_CASE(normal_disconnect_updates_spent_index)
{
    ScopedBool spentIndex(fSpentIndex, true);
    const CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    CBasicKeyStore keyStore;
    BOOST_REQUIRE(keyStore.AddKeyPubKey(coinbaseKey, coinbaseKey.GetPubKey()));

    CMutableTransaction spend;
    spend.vin.emplace_back(COutPoint(coinbaseTxns.front().GetHash(), 0));
    spend.vout.emplace_back(coinbaseTxns.front().vout[0].nValue - CENT, scriptPubKey);
    BOOST_REQUIRE(SignSignature(keyStore, coinbaseTxns.front(), spend, 0, SIGHASH_ALL));
    CreateAndProcessBlock({spend}, coinbaseKey);

    CSpentIndexKey key(coinbaseTxns.front().GetHash(), 0);
    CSpentIndexValue value;
    BOOST_REQUIRE(pblocktree->ReadSpentIndex(key, value));
    BOOST_CHECK(value.txid == spend.GetHash());

    BOOST_REQUIRE(DisconnectBlocks(1));
    BOOST_CHECK(!pblocktree->ReadSpentIndex(key, value));
}

BOOST_AUTO_TEST_CASE(popblocks_clears_both_transaction_pools)
{
    const CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    CBasicKeyStore keyStore;
    BOOST_REQUIRE(keyStore.AddKeyPubKey(coinbaseKey, coinbaseKey.GetPubKey()));

    CMutableTransaction parent;
    parent.vin.emplace_back(COutPoint(coinbaseTxns.front().GetHash(), 0));
    parent.vout.emplace_back(coinbaseTxns.front().vout[0].nValue - CENT, scriptPubKey);
    BOOST_REQUIRE(SignSignature(keyStore, coinbaseTxns.front(), parent, 0, SIGHASH_ALL));
    CreateAndProcessBlock({parent}, coinbaseKey);

    CMutableTransaction child;
    child.vin.emplace_back(COutPoint(parent.GetHash(), 0));
    child.vout.emplace_back(parent.vout[0].nValue - CENT, scriptPubKey);
    BOOST_REQUIRE(SignSignature(keyStore, parent, child, 0, SIGHASH_ALL));

    CMutableTransaction unrelated;
    unrelated.vin.emplace_back(COutPoint(coinbaseTxns[1].GetHash(), 0));
    unrelated.vout.emplace_back(coinbaseTxns[1].vout[0].nValue - CENT, scriptPubKey);
    BOOST_REQUIRE(SignSignature(keyStore, coinbaseTxns[1], unrelated, 0, SIGHASH_ALL));

    TestMemPoolEntryHelper entry;
    mempool.addUnchecked(child.GetHash(), entry.FromTx(child, &mempool));
    mempool.addUnchecked(unrelated.GetHash(), entry.FromTx(unrelated, &mempool));
    CTxMemPool& stemPool = txpools.getStemTxPool();
    stemPool.addUnchecked(child.GetHash(), entry.FromTx(child, &stemPool));
    stemPool.addUnchecked(unrelated.GetHash(), entry.FromTx(unrelated, &stemPool));
    BOOST_REQUIRE(mempool.exists(child.GetHash()));
    BOOST_REQUIRE(mempool.exists(unrelated.GetHash()));
    BOOST_REQUIRE(stemPool.exists(child.GetHash()));
    BOOST_REQUIRE(stemPool.exists(unrelated.GetHash()));

    BOOST_CHECK_EQUAL(CallRPC("popblocks 1").get_int(), 100);
    BOOST_CHECK_EQUAL(mempool.size(), 0U);
    BOOST_CHECK_EQUAL(stemPool.size(), 0U);
    BOOST_CHECK(mempool.vTxHashes.empty());
    BOOST_CHECK(stemPool.vTxHashes.empty());
}

BOOST_AUTO_TEST_CASE(popblocks_publishes_one_coherent_final_tip)
{
    CBlockIndex* expectedTip;
    CBlockIndex* firstTransactionTip;
    std::size_t firstBlockTransactions;
    std::size_t secondBlockTransactions;
    {
        LOCK(cs_main);
        expectedTip = chainActive[98];
        firstTransactionTip = chainActive[99];
        firstBlockTransactions = chainActive[100]->nTx;
        secondBlockTransactions = chainActive[99]->nTx;
    }

    PopBlocksNotificationRecorder recorder;
    ScopedValidationRegistration registration(recorder);
    BOOST_CHECK_EQUAL(CallRPC("popblocks 2").get_int(), 98);

    BOOST_CHECK_EQUAL(recorder.tipNotifications, 1);
    BOOST_CHECK_EQUAL(recorder.newTip, expectedTip);
    BOOST_CHECK_EQUAL(recorder.fork, expectedTip);
    BOOST_REQUIRE_EQUAL(
        recorder.transactionTips.size(),
        firstBlockTransactions + secondBlockTransactions);
    BOOST_REQUIRE_EQUAL(
        recorder.transactionPositions.size(),
        recorder.transactionTips.size());
    for (std::size_t i = 0; i < recorder.transactionTips.size(); ++i) {
        const CBlockIndex* expectedTransactionTip =
            i < firstBlockTransactions ? firstTransactionTip : expectedTip;
        BOOST_CHECK_EQUAL(
            recorder.transactionTips[i], expectedTransactionTip);
        BOOST_CHECK(
            recorder.transactionPositions[i] ==
            CMainSignals::SYNC_TRANSACTION_NOT_IN_BLOCK);
    }
}

BOOST_AUTO_TEST_CASE(invalidateblock_preserves_fork_notification_semantics)
{
    CBlockIndex* invalidatedTip;
    CBlockIndex* expectedTip;
    {
        LOCK(cs_main);
        invalidatedTip = chainActive.Tip();
        expectedTip = invalidatedTip->pprev;
    }

    PopBlocksNotificationRecorder recorder;
    ScopedValidationRegistration registration(recorder);
    CValidationState state;
    {
        LOCK(cs_main);
        BOOST_REQUIRE(
            InvalidateBlock(state, Params(), invalidatedTip));
    }

    BOOST_CHECK_EQUAL(recorder.tipNotifications, 1);
    BOOST_CHECK_EQUAL(recorder.newTip, expectedTip);
    BOOST_CHECK(recorder.fork == nullptr);
}

BOOST_AUTO_TEST_CASE(invalidateblock_notifies_for_a_side_branch)
{
    CKey sideKey;
    sideKey.MakeNewKey(true);
    const CScript sideScript =
        CScript() << ToByteVector(sideKey.GetPubKey()) << OP_CHECKSIG;
    const CBlock sideBlock = CreateBlock({}, sideScript);
    CreateAndProcessBlock({}, coinbaseKey);
    CreateAndProcessBlock({}, coinbaseKey);

    bool newBlock = false;
    BOOST_REQUIRE(ProcessNewBlock(
        Params(),
        std::make_shared<const CBlock>(sideBlock),
        true,
        &newBlock));
    BOOST_REQUIRE(newBlock);

    CBlockIndex* activeTip;
    CBlockIndex* sideIndex;
    {
        LOCK(cs_main);
        activeTip = chainActive.Tip();
        sideIndex = mapBlockIndex.at(sideBlock.GetHash());
        BOOST_REQUIRE(!chainActive.Contains(sideIndex));
    }

    PopBlocksNotificationRecorder recorder;
    ScopedValidationRegistration registration(recorder);
    CValidationState state;
    {
        LOCK(cs_main);
        BOOST_REQUIRE(
            InvalidateBlock(state, Params(), sideIndex));
    }

    BOOST_CHECK_EQUAL(recorder.tipNotifications, 1);
    BOOST_CHECK_EQUAL(recorder.newTip, activeTip);
    BOOST_CHECK(recorder.fork == nullptr);
}

BOOST_AUTO_TEST_CASE(reentrant_invalidations_publish_every_tip_event)
{
    CKey firstSideKey;
    CKey secondSideKey;
    firstSideKey.MakeNewKey(true);
    secondSideKey.MakeNewKey(true);
    const CBlock firstSideBlock = CreateBlock(
        {},
        CScript() <<
            ToByteVector(firstSideKey.GetPubKey()) <<
            OP_CHECKSIG);
    const CBlock secondSideBlock = CreateBlock(
        {},
        CScript() <<
            ToByteVector(secondSideKey.GetPubKey()) <<
            OP_CHECKSIG);
    CreateAndProcessBlock({}, coinbaseKey);
    CreateAndProcessBlock({}, coinbaseKey);

    bool newBlock = false;
    BOOST_REQUIRE(ProcessNewBlock(
        Params(),
        std::make_shared<const CBlock>(firstSideBlock),
        true,
        &newBlock));
    BOOST_REQUIRE(newBlock);
    BOOST_REQUIRE(ProcessNewBlock(
        Params(),
        std::make_shared<const CBlock>(secondSideBlock),
        true,
        &newBlock));
    BOOST_REQUIRE(newBlock);

    CBlockIndex* firstSideIndex;
    CBlockIndex* secondSideIndex;
    {
        LOCK(cs_main);
        firstSideIndex =
            mapBlockIndex.at(firstSideBlock.GetHash());
        secondSideIndex =
            mapBlockIndex.at(secondSideBlock.GetHash());
        BOOST_REQUIRE(!chainActive.Contains(firstSideIndex));
        BOOST_REQUIRE(!chainActive.Contains(secondSideIndex));
    }

    ReentrantInvalidationRecorder recorder(
        secondSideIndex);
    ScopedValidationRegistration registration(recorder);
    CValidationState state;
    {
        LOCK(cs_main);
        BOOST_REQUIRE(InvalidateBlock(
            state, Params(), firstSideIndex));
    }
    BOOST_CHECK(recorder.reentrantResult);
    BOOST_CHECK_EQUAL(recorder.tipNotifications, 2);
}

BOOST_AUTO_TEST_SUITE_END()
