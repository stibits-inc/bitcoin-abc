// Copyright (c) 2017-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <script/standard.h>
#include <stib/index/addressindex.h>
#include <index/txindex.h>

#include <ui_interface.h>
#include <util/system.h>
#include <validation.h>
#include <config.h>
#include <key_io.h>
#include <rpc/blockchain.h>
#include <rpc/server.h>
#include <rpc/util.h>
#include <pubkey.h>
#include <boost/thread.hpp>
#include <util/strencodings.h>

constexpr char DB_ADDRESSINDEX = 'a';
constexpr char DB_ADDRESSUNSPENTINDEX = 'u';

std::unique_ptr<CAddressIndex> g_addressindex;

CAddressIndex::CAddressIndex(size_t nCacheSize, bool fMemory, bool fWipe)
 : CDBWrapper( GetDataDir() / "indexes" / "addressindex", nCacheSize, fMemory, fWipe) {
}

bool CAddressIndex::ReadAddressUnspentIndex(uint160 addressHash, int type, std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > &vect)
{
    std::unique_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(std::make_pair(DB_ADDRESSUNSPENTINDEX, CAddressIndexIteratorKey(type, addressHash)));

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char,CAddressUnspentKey> key;
        if (pcursor->GetKey(key) && key.first == DB_ADDRESSUNSPENTINDEX && key.second.hashBytes == addressHash) {
            CAddressUnspentValue nValue;
            if (pcursor->GetValue(nValue)) {
                vect.push_back(std::make_pair(key.second, nValue));
                pcursor->Next();
            } else {
                return error("failed to get address unspent value");
            }
        } else {
            break;
        }
    }

    return true;
}

bool CAddressIndex::ReadAddressIndex(uint160 addressHash, int type, std::vector<std::pair<CAddressIndexKey, CAmount> > &addressIndex, unsigned int start, unsigned int end)
{
    std::unique_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(std::make_pair(DB_ADDRESSINDEX, CAddressIndexIteratorKey(type, addressHash)));

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char,CAddressIndexKey> key;
        if (pcursor->GetKey(key) && key.first == DB_ADDRESSINDEX && key.second.hashBytes == addressHash) {
            if (end > 0 && ((unsigned int)key.second.blockHeight) > end) {
                break;
            }
            CAmount nValue;
            if (pcursor->GetValue(nValue)) {
                addressIndex.push_back(std::make_pair(key.second, nValue));
                pcursor->Next();
            } else {
                return error("failed to get address index value");
            }
        } else {
            break;
        }
    }

    return true;
}

bool CAddressIndex::AddBlockToAddressIndex(const CBlock &block, const CBlockIndex *pindex, const CCoinsViewCache &view)
{
    if(block.vtx.size() == 0)
        return false;

    CDBBatch  batch(*this);

    // the coinbase transaction, indexing outputs
    {
        const CTransaction &tx = *(block.vtx[0]);
        uint256 hash = tx.GetHash();

        for (unsigned int k = tx.vout.size(); k-- > 0;) {
            const CTxOut &o = tx.vout[k];

            std::vector<std::vector<unsigned char>> vSolutionsRet;
            txnouttype type = Solver(o.scriptPubKey, vSolutionsRet);

            if (type == TX_PUBKEYHASH || type == TX_SCRIPTHASH) // || whichType == TX_WITNESS_V0_KEYHASH)
            {
                assert(vSolutionsRet.size() == 1);
                uint160 hashBytes(vSolutionsRet[0]);

                std::pair<char,CAddressIndexKey>   ka(DB_ADDRESSINDEX, CAddressIndexKey(type, hashBytes, pindex->nHeight, 0, hash, k, false));
                std::pair<char,CAddressUnspentKey> ku(DB_ADDRESSUNSPENTINDEX, CAddressUnspentKey(type, hashBytes, hash, k));
                batch.Write( ka, o.nValue);
                batch.Write(ku, CAddressUnspentValue(o.nValue, o.scriptPubKey, pindex->nHeight));
            };
        }
    }

    // others transactions, indexing outputs and inputs
    for (size_t i = 1; i < block.vtx.size(); i++) {
        const CTransaction &tx = *(block.vtx[i]);
        uint256 hash = tx.GetHash();

        for (unsigned int k = tx.vout.size(); k-- > 0;) {
            const CTxOut &o = tx.vout[k];

            std::vector<std::vector<unsigned char>> vSolutionsRet;
            txnouttype type = Solver(o.scriptPubKey, vSolutionsRet);

            if (type == TX_PUBKEYHASH || type == TX_SCRIPTHASH) // || whichType == TX_WITNESS_V0_KEYHASH)
            {
                assert(vSolutionsRet.size() == 1);
                uint160 hashBytes(vSolutionsRet[0]);

                std::pair<char,CAddressIndexKey>   ka(DB_ADDRESSINDEX, CAddressIndexKey(type, hashBytes, pindex->nHeight, i, hash, k, false));
                std::pair<char,CAddressUnspentKey> ku(DB_ADDRESSUNSPENTINDEX, CAddressUnspentKey(type, hashBytes, hash, k));

                batch.Write(ka, o.nValue);
                batch.Write(ku, CAddressUnspentValue(o.nValue, o.scriptPubKey, pindex->nHeight));
            };
        }

        for (size_t j = tx.vin.size(); j-- > 0;) {
            const CTxIn input = tx.vin[j];

            const Coin &coin = view.AccessCoin(tx.vin[j].prevout);
            const CTxOut &prevout = coin.GetTxOut();

            std::vector<std::vector<unsigned char>> vSolutionsRet;
            txnouttype type = Solver(prevout.scriptPubKey, vSolutionsRet);

            if (type == TX_PUBKEYHASH || type == TX_SCRIPTHASH) // || whichType == TX_WITNESS_V0_KEYHASH)
            {
                assert(vSolutionsRet.size() == 1);
                uint160 hashBytes(vSolutionsRet[0]);

                std::pair<char,CAddressIndexKey>   ka(DB_ADDRESSINDEX, CAddressIndexKey(type, hashBytes, pindex->nHeight, i, hash, j, true));
                std::pair<char,CAddressUnspentKey> ku(DB_ADDRESSUNSPENTINDEX, CAddressUnspentKey(type, hashBytes, input.prevout.GetTxId(), input.prevout.GetN()));

                batch.Write(ka, - prevout.nValue);
                batch.Erase(ku);
            };
        }
    }

    return WriteBatch(batch);
}

bool CAddressIndex::RevertBlockFromAddressIndex(const CBlockUndo &blockUndo, const CBlock &block, const CBlockIndex *pindex, const CCoinsViewCache &view)
{
    if(block.vtx.size() == 0)
        return false;

    CDBBatch  batch(*this);

    // the coinbase transaction, indexing outputs
    {
        const CTransaction &tx = *(block.vtx[0]);
        uint256 hash = tx.GetHash();

        for (unsigned int k = tx.vout.size(); k-- > 0;) {
            const CTxOut &o = tx.vout[k];

            std::vector<std::vector<unsigned char>> vSolutionsRet;
            txnouttype type = Solver(o.scriptPubKey, vSolutionsRet);
            if (type == TX_PUBKEYHASH || type == TX_SCRIPTHASH) // || whichType == TX_WITNESS_V0_KEYHASH)
            {
                assert(vSolutionsRet.size() == 1);
                uint160 hashBytes(vSolutionsRet[0]);

                std::pair<char,CAddressIndexKey>   ka(DB_ADDRESSINDEX, CAddressIndexKey(type, hashBytes, pindex->nHeight, 0, hash, k, false));
                std::pair<char,CAddressUnspentKey> ku(DB_ADDRESSUNSPENTINDEX, CAddressUnspentKey(type, hashBytes, hash, k));


                batch.Erase(ka);
                batch.Erase(ku);
            };
        }
    }

    // others transactions, indexing outputs and inputs
    for (size_t i = 1; i < block.vtx.size(); i++) {
        const CTransaction &tx = *(block.vtx[i]);
        uint256 hash = tx.GetHash();

        for (unsigned int k = tx.vout.size(); k-- > 0;) {
            const CTxOut &o = tx.vout[k];
            std::vector<std::vector<unsigned char>> vSolutionsRet;
            txnouttype type = Solver(o.scriptPubKey, vSolutionsRet);

            if (type == TX_PUBKEYHASH || type == TX_SCRIPTHASH) // || whichType == TX_WITNESS_V0_KEYHASH)
            {
                assert(vSolutionsRet.size() == 1);
                uint160 hashBytes(vSolutionsRet[0]);

                std::pair<char,CAddressIndexKey>   ka(DB_ADDRESSINDEX, CAddressIndexKey(type, hashBytes, pindex->nHeight, i, hash, k, false));
                std::pair<char,CAddressUnspentKey> ku(DB_ADDRESSUNSPENTINDEX, CAddressUnspentKey(type, hashBytes, hash, k));

                batch.Erase(ka);
                batch.Erase(ku);
            };
        }

        const CTxUndo &txundo = blockUndo.vtxundo[i - 1];
        for (size_t j = tx.vin.size(); j-- > 0;) {
            int undoHeight = txundo.vprevout[j].GetHeight();
            const CTxIn input = tx.vin[j];

            const Coin &coin = view.AccessCoin(tx.vin[j].prevout);
            const CTxOut &prevout = coin.GetTxOut();

            std::vector<std::vector<unsigned char>> vSolutionsRet;
            txnouttype type = Solver(prevout.scriptPubKey, vSolutionsRet);

            if (type == TX_PUBKEYHASH || type == TX_SCRIPTHASH) // || whichType == TX_WITNESS_V0_KEYHASH)
            {
                assert(vSolutionsRet.size() == 1);
                uint160 hashBytes(vSolutionsRet[0]);

                std::pair<char,CAddressIndexKey>   ka(DB_ADDRESSINDEX, CAddressIndexKey(type, hashBytes, pindex->nHeight, i, hash, j, true));
                std::pair<char,CAddressUnspentKey> ku(DB_ADDRESSUNSPENTINDEX, CAddressUnspentKey(type, hashBytes, input.prevout.GetTxId(), input.prevout.GetN()));

                batch.Erase(ka);
                batch.Write(ku, CAddressUnspentValue(prevout.nValue, prevout.scriptPubKey, undoHeight));
            };
        }
    }

    return WriteBatch(batch);
}


template <typename T, typename... Args>
std::unique_ptr<T> MakeUnique(Args&&... args)
{
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

// RPC interface

bool GetAddressUnspent(uint160 addressHash, int type,
                       std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > &unspentOutputs)
{
    if (!g_addressindex)
        return error("address index not enabled");

    if (!g_addressindex->ReadAddressUnspentIndex(addressHash, type, unspentOutputs))
        return error("unable to get txids for address");

    return true;
}

bool GetAddressIndex(uint160 addressHash, int type,
                     std::vector<std::pair<CAddressIndexKey, CAmount> > &addressIndex)
{
    if (!g_addressindex)
        return error("address index not enabled");

    if (!g_addressindex->ReadAddressIndex(addressHash, type, addressIndex))
        return error("unable to get txids for address");

    return true;
}

bool AddressToHashType(std::string addr_str, uint160& hashBytes, int& type)
{
    CTxDestination dest = DecodeDestination(addr_str, GetConfig().GetChainParams());
    //typedef boost::variant<CNoDestination, CKeyID, CScriptID, WitnessV0ScriptHash, WitnessV0KeyHash, WitnessUnknown> CTxDestination;
    switch(dest.which())
    {
        case 1: // CKeyID
        {
            CKeyID k = boost::get<CKeyID> (dest);
            memcpy(&hashBytes, k.begin(), 20);
            type = TX_PUBKEYHASH;
            break;
        }

        case 2: // CScriptID
        {
            CScriptID k = boost::get<CScriptID> (dest);
            memcpy(&hashBytes, k.begin(), 20);
            type = TX_SCRIPTHASH;
            break;
        }
        /*
        case 4: // WitnessV0KeyHash
        {
            WitnessV0KeyHash w = boost::get<WitnessV0KeyHash>(dest);
            memcpy(&hashBytes, w.begin(), 20);
            type = TX_WITNESS_V0_KEYHASH;
           break;
        }
        */
        default :
            return false;
    }

    return true;
}


bool HashTypeToAddress(const uint160 &hash, const int &type, std::string &address)
{
    // (whichType == TX_PUBKEYHASH || whichType == TX_SCRIPTHASH || whichType == TX_WITNESS_V0_KEYHASH)

    if (type == TX_SCRIPTHASH) {
        address = EncodeDestination(CScriptID(hash), GetConfig());
    } else if (type == TX_PUBKEYHASH) {
        address = EncodeDestination(CKeyID(hash), GetConfig());
    } /*else if (type == TX_WITNESS_V0_KEYHASH) {
        address = EncodeDestination(WitnessV0KeyHash(hash), GetConfig());
    }*/ else {
        return false;
    }
    return true;
}

bool getAddressesFromParams(const UniValue& params, std::vector<std::pair<uint160, int> > &addresses)
{
    if (params[0].isStr()) {
        uint160 hashBytes;
        int type = 0;
        if (!AddressToHashType(params[0].get_str(), hashBytes, type)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,  params[0].get_str() + "Invalid address 1");
        }
        addresses.push_back(std::make_pair(hashBytes, type));
    } else if (params[0].isObject()) {

        UniValue addressValues = find_value(params[0].get_obj(), "addresses");
        if (!addressValues.isArray()) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Addresses is expected to be an array");
        }

        std::vector<UniValue> values = addressValues.getValues();

        for (std::vector<UniValue>::iterator it = values.begin(); it != values.end(); ++it) {

            uint160 hashBytes;
            int type = 0;
            if (!AddressToHashType(it->get_str(), hashBytes, type)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address 2");
            }
            addresses.push_back(std::make_pair(hashBytes, type));
        }
    } else {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address 3");
    }

    return true;
}

bool heightSort(std::pair<CAddressUnspentKey, CAddressUnspentValue> a,
                std::pair<CAddressUnspentKey, CAddressUnspentValue> b) {
    return a.second.blockHeight < b.second.blockHeight;
}

std::vector<uint256> GetAddressesTxs(std::vector<std::pair<uint160, int>> &addresses)
{

    std::vector<std::pair<CAddressIndexKey, CAmount> > addressIndex;

    for (auto it = addresses.begin(); it != addresses.end(); it++) {

        if (!GetAddressIndex((*it).first, (*it).second, addressIndex)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for address");
        }
    }

    std::set<std::pair<int, uint256> > txids;
    std::vector<uint256> result;

    for (auto it = addressIndex.begin(); it != addressIndex.end(); it++) {
        int height = it->first.blockHeight;
        txids.insert(std::make_pair(height, it->first.txhash));
    }

    for (auto it=txids.begin(); it!=txids.end(); it++) {
        result.push_back(it->second);
    }

    return result;
}


bool IsAddressesHasTxs(std::vector<std::pair<uint160, int>> &addresses)
{

    std::vector<std::pair<CAddressIndexKey, CAmount> > addressIndex;

    for (auto it = addresses.begin(); it != addresses.end(); it++) {

        if (!GetAddressIndex((*it).first, (*it).second, addressIndex)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for address");
        }

        if(addressIndex.size() > 0) return true;
    }

    return addressIndex.size() > 0;

}

UniValue GetAddressesUtxos(std::vector<std::pair<uint160, int>> &addresses)
{
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;

    for (std::vector<std::pair<uint160, int> >::iterator it = addresses.begin(); it != addresses.end(); it++) {

            if (!GetAddressUnspent((*it).first, (*it).second, unspentOutputs)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for address");
            }
    }

    std::sort(unspentOutputs.begin(), unspentOutputs.end(), heightSort);

    UniValue utxos(UniValue::VARR);

    for (auto it=unspentOutputs.begin(); it!=unspentOutputs.end(); it++) {
        UniValue output(UniValue::VOBJ);

        std::string address;

        if (!HashTypeToAddress(it->first.hashBytes, it->first.type, address)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unknown address type");
        }

        output.pushKV("address", address);
        output.pushKV("txid", it->first.txhash.GetHex());
        output.pushKV("outputIndex", (int)it->first.index);
        output.pushKV("script", HexStr(it->second.script.begin(), it->second.script.end()));
        output.pushKV("satoshis", it->second.satoshis/SATOSHI);
        output.pushKV("height", it->second.blockHeight);
        utxos.push_back(output);
    }

    return utxos;
}

bool GetAddressesUtxos(std::vector<std::pair<uint160, int>> &addresses, CDataStream& ss, uint32_t& count)
{
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;

    for (std::vector<std::pair<uint160, int> >::iterator it = addresses.begin(); it != addresses.end(); it++) {

            if (!GetAddressUnspent((*it).first, (*it).second, unspentOutputs)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for address");
            }
    }

    std::sort(unspentOutputs.begin(), unspentOutputs.end(), heightSort);

    count += unspentOutputs.size();

    for (auto it=unspentOutputs.begin(); it!=unspentOutputs.end(); it++) {

        std::string address;

        if (!HashTypeToAddress(it->first.hashBytes, it->first.type, address)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unknown address type");
        }
        ss << address;

        it->first.txhash.Serialize(ss);
        ser_writedata32(ss, it->first.index);

        ss << it->second;
    }

    return unspentOutputs.size() > 0;
}

int GetLastUsedIndex(std::vector<std::pair<uint160, int>> &addresses)
{
    int r = -1;
    int index = 0;
    std::vector<std::pair<CAddressIndexKey, CAmount> > txOutputs;

    for (std::vector<std::pair<uint160, int> >::iterator it = addresses.begin(); it != addresses.end(); it++) {

        if (!GetAddressIndex((*it).first, (*it).second, txOutputs)) {
             throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for address");
        }
        if(txOutputs.size() > 0)
        {
            r = index;
            txOutputs.clear();
        }

        index++;
    }

    return r;
}

UniValue getaddressutxos(const Config &config, const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "getaddressutxos\n"
            "\nReturns all unspent outputs for an address (requires addressindex to be enabled).\n"
            "\nArguments:\n"
            "{\n"
            "  \"addresses\"\n"
            "    [\n"
            "      \"address\"  (string) The base58check encoded address\n"
            "      ,...\n"
            "    ],\n"
            "  \"chainInfo\",  (boolean, optional, default false) Include chain info with results\n"
            "}\n"
            "\nResult\n"
            "[\n"
            "  {\n"
            "    \"address\"  (string) The address base58check encoded\n"
            "    \"txid\"  (string) The output txid\n"
            "    \"height\"  (number) The block height\n"
            "    \"outputIndex\"  (number) The output index\n"
            "    \"script\"  (strin) The script hex encoded\n"
            "    \"satoshis\"  (number) The number of satoshis of the output\n"
            "  }\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("getaddressutxos", "'{\"addresses\": [\"12c6DSiU4Rq3P4ZxziKxzrL5LmMBrzjrJX\"]}'")
            + HelpExampleRpc("getaddressutxos", "{\"addresses\": [\"12c6DSiU4Rq3P4ZxziKxzrL5LmMBrzjrJX\"]}")
            );

    bool includeChainInfo = false;
    if (request.params[0].isObject()) {
        UniValue chainInfo = find_value(request.params[0].get_obj(), "chainInfo");
        if (chainInfo.isBool()) {
            includeChainInfo = chainInfo.get_bool();
        }
    }

    std::vector<std::pair<uint160, int> > addresses;

    if (!getAddressesFromParams(request.params, addresses)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    UniValue utxos = GetAddressesUtxos(addresses);


    if (includeChainInfo) {
        UniValue result(UniValue::VOBJ);
        result.pushKV("utxos", utxos);

        LOCK(cs_main);
        result.pushKV("hash", chainActive.Tip()->GetBlockHash().GetHex());
        result.pushKV("height", (int)chainActive.Height());
        return result;
    } else {
        return utxos;
    }
}

UniValue getaddresstxids(const Config &config, const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 2)
        throw std::runtime_error(
            "getaddresstxids\n"
            "\nReturns the txids for an address(es) (requires addressindex to be enabled).\n"
            "\nArguments:\n"
            "{\n"
            "  \"addresses\"\n"
            "    [\n"
            "      \"address\"  (string) The base58check encoded address\n"
            "      ,...\n"
            "    ]\n"
            "},\n"
            "\nResult:\n"
            "[\n"
            "  \"transactionid\"  (string) The transaction id\n"
            "  ,...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("getaddresstxids", "'{\"addresses\": [\"12c6DSiU4Rq3P4ZxziKxzrL5LmMBrzjrJX\"]}'")
            + HelpExampleRpc("getaddresstxids", "{\"addresses\": [\"12c6DSiU4Rq3P4ZxziKxzrL5LmMBrzjrJX\"]}")
        );

    std::vector<std::pair<uint160, int> > addresses;

    if (!getAddressesFromParams(request.params, addresses)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    std::vector<std::pair<CAddressIndexKey, CAmount> > addressIndex;

    for (std::vector<std::pair<uint160, int> >::iterator it = addresses.begin(); it != addresses.end(); it++) {

        if (!GetAddressIndex((*it).first, (*it).second, addressIndex)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for address");

        }
    }

    std::set<std::pair<int, std::string> > txids;
    UniValue result(UniValue::VARR);

    for (std::vector<std::pair<CAddressIndexKey, CAmount> >::const_iterator it=addressIndex.begin(); it!=addressIndex.end(); it++) {
        int height = it->first.blockHeight;
        std::string txid = it->first.txhash.GetHex();

        if (addresses.size() > 1) {
            txids.insert(std::make_pair(height, txid));
        } else {
            if (txids.insert(std::make_pair(height, txid)).second) {
                result.push_back(txid);
            }
        }
    }

    if (addresses.size() > 1) {
        for (std::set<std::pair<int, std::string> >::const_iterator it=txids.begin(); it!=txids.end(); it++) {
            result.push_back(it->second);
        }
    }

    return result;

}
