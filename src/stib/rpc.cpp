
#include <key_io.h>
#include <config.h>
#include <base58.h>

#include <script/standard.h>

#include <script/script.h>
#include <util/strencodings.h>
#include <rpc/blockchain.h>
#include <rpc/server.h>

#include <stib/index/addressindex.h>
#include <index/txindex.h>


void RecoverFromXPUB(std::string xpubkey, UniValue& out);
void GenerateFromXPUB(std::string xpubkey, int from, int count, UniValue& out);
void RecoverTxsFromXPUB(std::string xpubkey, std::vector<uint256>& out);  // defined in src/stib/common.cpp

int GetLastUsedExternalSegWitIndex(std::string& xpubkey);
int GetFirstUsedBlock(std::string xpub);


// RPC
UniValue stbtsgenxpubaddresses(const Config &config, const JSONRPCRequest& request)
{
   if (request.fHelp || request.params.size() < 1  || request.params.size() > 1)
        throw std::runtime_error(
            "stbtsgenxpubaddresses\n"
            "\nReturns 'count' HD generated address for an 'xpub', starting  from 'start' index.\n"
            "\nArguments:\n"
            "{\n"
            "  \"xpubkey\",  account extended public key ExtPubKey\n"
            "  \"start\", (optional default to 0) index of the first address to generate\n"
            "  \"count\", (optional default to 100) numbre of addresses to generate\n"
            "}\n"
            "\nResult\n"
            "[\n"
            "      \"address\"  (string) The base58check encoded address\n"
            "      ,...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("stbtsgenxpubaddresses", "'{\"xpubkey\": \"xpub6Bgu572Y3EWgEq8gkVxmznPkb8hWkgYR9E6KTZN3pyM3hhC7WvwgHNchSCrC19a7nZ3ddyjwB26rbePuyATc55snUwWKkszRnvVwfmBshdS\"}'")
            + HelpExampleRpc("stbtsgenxpubaddresses", "{\"xpubkey\": \"xpub6Bgu572Y3EWgEq8gkVxmznPkb8hWkgYR9E6KTZN3pyM3hhC7WvwgHNchSCrC19a7nZ3ddyjwB26rbePuyATc55snUwWKkszRnvVwfmBshdS\"}")
            );
   
    std::string xpubkey;
    int  from = 0;
    int  count = 100;

    if (request.params[0].isObject()) {
        UniValue val = find_value(request.params[0].get_obj(), "xpubkey");
        if (val.isStr()) {
            xpubkey = val.get_str();
        }
        
        val = find_value(request.params[0].get_obj(), "from");
        if (val.isNum()) {
            from = val.get_int();
        }
                
        val = find_value(request.params[0].get_obj(), "count");
        if (val.isNum()) {
            count = val.get_int();
        }
    }
   
    UniValue addrs(UniValue::VARR);
    GenerateFromXPUB(xpubkey, from, count, addrs);

    return addrs;
}

UniValue stbtsgetxpubutxos(const Config &config, const JSONRPCRequest& request)
{
   if (request.fHelp || request.params.size() < 1  || request.params.size() > 1)
        throw std::runtime_error(
            "stbtsgetxpubutxos\n"
            "\nReturns 'count' HD generated address for an 'xpub', starting  from 'start' index.\n"
            "\nArguments:\n"
            "{\n"
            "  \"xpubkey\",  account extended public key ExtPubKey\n"
            "}\n"
            "\nResult\n"
            "[\n"
            "  {\n"
            "  \"addresses\"\n"
            "    [\n"
            "      \"address\"  (string) The base58check encoded address\n"
            "      ,...\n"
            "    ]\n"
            "  }\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("stbtsgetxpubutxos", "'{\"xpubkey\": \"xpub6Bgu572Y3EWgEq8gkVxmznPkb8hWkgYR9E6KTZN3pyM3hhC7WvwgHNchSCrC19a7nZ3ddyjwB26rbePuyATc55snUwWKkszRnvVwfmBshdS\"}'")
            + HelpExampleRpc("stbtsgetxpubutxos", "{\"xpubkey\": \"xpub6Bgu572Y3EWgEq8gkVxmznPkb8hWkgYR9E6KTZN3pyM3hhC7WvwgHNchSCrC19a7nZ3ddyjwB26rbePuyATc55snUwWKkszRnvVwfmBshdS\"}")
            );
   
    std::string xpubkey;

    if (request.params[0].isObject()) {
        UniValue val = find_value(request.params[0].get_obj(), "xpubkey");
        if (val.isStr()) {
            xpubkey = val.get_str();
        }
    }
    else
    if(request.params[0].isStr())
    {
        xpubkey = request.params[0].get_str();
    }
    else
    {
        throw JSONRPCError(-1, "xpub is missing!!");
    }

    if(xpubkey.size() < 4
       || xpubkey[1] != 'p' || xpubkey[2] != 'u' || xpubkey[3] != 'b')
    {
        throw JSONRPCError(-1, "xpub is missing or invalid!!!");
    }
    
    UniValue utxos(UniValue::VARR);
    RecoverFromXPUB(xpubkey, utxos);
    return utxos;

}


UniValue stbtsgetxpubtxs(const Config &config, const JSONRPCRequest& request)
{
	BCLog::LogFlags logFlag = BCLog::ALL; //::RPC;
    if (request.fHelp || request.params.size() < 1  || request.params.size() > 1)
         throw std::runtime_error(
             "stbtsgetxpubtxs\n"
             "\nReturns 'count' HD generated address for an 'xpub', starting  from 'start' index.\n"
             "\nArguments:\n"
             "{\n"
             "  \"xpubkey\",  account extended public key ExtPubKey\n"
             "}\n"
             "\nResult\n"
             "[\n"
             "  {\n"
             "  \"addresses\"\n"
             "    [\n"
             "      \"address\"  (string) The base58check encoded address\n"
             "      ,...\n"
             "    ]\n"
             "  }\n"
             "]\n"
             "\nExamples:\n"
             + HelpExampleCli("stbtsgetxpubtxs", "'{\"xpubkey\": \"xpub6Bgu572Y3EWgEq8gkVxmznPkb8hWkgYR9E6KTZN3pyM3hhC7WvwgHNchSCrC19a7nZ3ddyjwB26rbePuyATc55snUwWKkszRnvVwfmBshdS\"}'")
             + HelpExampleRpc("stbtsgetxpubtxs", "{\"xpubkey\": \"xpub6Bgu572Y3EWgEq8gkVxmznPkb8hWkgYR9E6KTZN3pyM3hhC7WvwgHNchSCrC19a7nZ3ddyjwB26rbePuyATc55snUwWKkszRnvVwfmBshdS\"}")
             );
    
    if(!g_txindex)
    {
        LogPrintf("stbtsgetxpubtxs: Error, bitcoind is not started with -txindex option.\n");
        return tinyformat::format(R"({"result":null,"error":"bitcoind is not started with -txindex option"})");
    }

    std::string xpubkey;

    if (request.params[0].isObject()) {
        UniValue val = find_value(request.params[0].get_obj(), "xpubkey");
        if (val.isStr()) {
            xpubkey = val.get_str();
        }
    }
    else
    if(request.params[0].isStr())
    {
        xpubkey = request.params[0].get_str();
    }
    else
    {
        throw JSONRPCError(-1, "xpub is missing!!");
    }

    if(xpubkey.size() < 4
       || xpubkey[1] != 'p' || xpubkey[2] != 'u' || xpubkey[3] != 'b')
    {
        throw JSONRPCError(-1, "xpub is missing or invalid!!!");
    }
     
	LogPrintf("xpub found.\n");
	
    std::vector<uint256> out;
    RecoverTxsFromXPUB(xpubkey, out);

    LogPrint(logFlag, "stbtsgetxpubtxs : %d transactions found.\n", out.size());
    
    UniValue txs(UniValue::VARR);
    
    // txs.reserve(out.size());

    for(auto txhash: out)
    {
        CTransactionRef tx;
        BlockHash hash_block;
        if (g_txindex && g_txindex->FindTx(TxId(txhash), hash_block, tx ))
        {
		    CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
		    ssTx << tx;
		    
		    std::string stx = EncodeBase58((const uint8_t*)ssTx.data(), (const uint8_t*)(ssTx.data() + ssTx.size()));
            txs.push_back(stx);
        }
        else
        {
            // throw : error Transaction not found!!!!!!
            
        }
    }

    return txs;
}


UniValue stbtsgetlastusedhdindex(const Config &config, const JSONRPCRequest& request)
{
   if (request.fHelp || request.params.size() < 1  || request.params.size() > 1)
        throw std::runtime_error(
            "stbtsgetlastusedhdindex\n"
            "\nReturns the last used index, the index of the last used address.\n"
            "\nReturns -1 if no address is used.\n"
            "\nArguments:\n"
            "{\n"
            "  \"xpubkey\",  account extended public key ExtPubKey\n"
            "}\n"
            "\nResult\n"
            "[\n"
            "  {lastindex:val}\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("stbtsgetlastusedhdindex", "'{\"xpubkey\": \"xpub6Bgu572Y3EWgEq8gkVxmznPkb8hWkgYR9E6KTZN3pyM3hhC7WvwgHNchSCrC19a7nZ3ddyjwB26rbePuyATc55snUwWKkszRnvVwfmBshdS\"}'")
            + HelpExampleRpc("stbtsgetlastusedhdindex", "{\"xpubkey\": \"xpub6Bgu572Y3EWgEq8gkVxmznPkb8hWkgYR9E6KTZN3pyM3hhC7WvwgHNchSCrC19a7nZ3ddyjwB26rbePuyATc55snUwWKkszRnvVwfmBshdS\"}")
            );
   
    std::string xpubkey;

    if (request.params[0].isObject()) {
        UniValue val = find_value(request.params[0].get_obj(), "xpubkey");
        if (val.isStr()) {
            xpubkey = val.get_str();
        }
    }
    else
    if(request.params[0].isStr())
    {
        xpubkey = request.params[0].get_str();
    }
    else
    {
        throw JSONRPCError(-1, "xpub is missing or invalid!!!");
    }

    if(xpubkey.size() == 0 || xpubkey[0] != 'x')
    {
        throw JSONRPCError(-1, "xpub is missing or invalid!!!");
    }
    
    if(xpubkey.size() == 0 || xpubkey[0] != 'x')
    {
        throw JSONRPCError(-1, "xpub is missing or invalid!!!");
    }

    int r = GetLastUsedExternalSegWitIndex(xpubkey);

    if(r == -1)
    {
        throw JSONRPCError(-1, "xpub is missing or invalid!!!");
    }
        
    UniValue obj(UniValue::VOBJ);
    obj.pushKV("lastindex", r);
    
    return obj;
}


UniValue stbtsgetfirstusedblock(const Config &config, const JSONRPCRequest& request)
{
   if (request.fHelp || request.params.size() < 1  || request.params.size() > 1)
        throw std::runtime_error(
            "stbtsgetfirstusedblock\n"
            "\nReturns the last used index, the index of the last used address.\n"
            "\nReturns -1 if no address is used.\n"
            "\nArguments:\n"
            "{\n"
            "  \"xpubkey\",  account extended public key ExtPubKey\n"
            "}\n"
            "\nResult\n"
            "[\n"
            "  {firstusedblock:val}\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("stbtsgetfirstusedblock", "'{\"xpubkey\": \"xpub6Bgu572Y3EWgEq8gkVxmznPkb8hWkgYR9E6KTZN3pyM3hhC7WvwgHNchSCrC19a7nZ3ddyjwB26rbePuyATc55snUwWKkszRnvVwfmBshdS\"}'")
            + HelpExampleRpc("stbtsgetfirstusedblock", "{\"xpubkey\": \"xpub6Bgu572Y3EWgEq8gkVxmznPkb8hWkgYR9E6KTZN3pyM3hhC7WvwgHNchSCrC19a7nZ3ddyjwB26rbePuyATc55snUwWKkszRnvVwfmBshdS\"}")
            );
   
    std::string xpubkey;

    if (request.params[0].isObject()) {
        UniValue val = find_value(request.params[0].get_obj(), "xpubkey");
        if (val.isStr()) {
            xpubkey = val.get_str();
        }
    }
    else
    if(request.params[0].isStr())
    {
        xpubkey = request.params[0].get_str();
    }
    else
    {
        throw JSONRPCError(-1, "xpubkey is missing!!");
    }

    if(xpubkey.size() == 0 || xpubkey[0] != 'x')
    {
        throw JSONRPCError(-1, "xpub is missing or invalid!!!");
    }

    int r = GetFirstUsedBlock(xpubkey);
    
    if(r == -1)
    {
        throw JSONRPCError(-1, "xpub is invalid!!!");
    }
    
    UniValue obj(UniValue::VOBJ);
    obj.pushKV("firstusedblock", r);
    
    return obj;
}
