/*  VTC Blockindexer - A utility to build additional indexes to the 
    Vertcoin blockchain by scanning and indexing the blockfiles
    downloaded by Vertcoin Core.
    
    Copyright (C) 2017  Gert-Jaap Glasbergen

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "mempoolmonitor.h"
#include "utility.h"
#include "scriptsolver.h"
#include "blockchaintypes.h"
#include <unordered_map>
#include <chrono>
#include <thread>
#include <time.h>
#include "byte_array_buffer.h"
using namespace std;

// This map keeps the memorypool transactions deserialized in memory.


VtcBlockIndexer::MempoolMonitor::MempoolMonitor(leveldb::DB* dbInstance) {
    this->db = dbInstance;
    httpClient.reset(new jsonrpc::HttpClient("http://middleware:middleware@" + std::string(std::getenv("VERTCOIND_HOST")) + ":8332"));
    vertcoind.reset(new VertcoinClient(*httpClient));
    blockReader.reset(new VtcBlockIndexer::BlockReader(""));
    scriptSolver.reset(new VtcBlockIndexer::ScriptSolver());
    mempoolEsignTransactions = {};
    mempoolIdentityTransactions = {};
}

void VtcBlockIndexer::MempoolMonitor::startWatcher() {
    while(true) {
        scriptSolver->testnet = testnet;
        
        try {
            const Json::Value mempool = vertcoind->getrawmempool();
            for ( uint index = 0; index < mempool.size(); ++index )
            {
                if(mempoolTransactions.find(mempool[index].asString()) == mempoolTransactions.end()) {
                    const Json::Value rawTx = vertcoind->getrawtransaction(mempool[index].asString(), false);
                    std::vector<unsigned char> rawTxBytes = VtcBlockIndexer::Utility::hexToBytes(rawTx.asString());

                    byte_array_buffer streambuf(&rawTxBytes[0], rawTxBytes.size());
                    std::istream stream(&streambuf);

                    VtcBlockIndexer::Transaction tx = blockReader->readTransaction(stream);
                    mempoolTransactions[mempool[index].asString()] = tx;

                    Block virtualBlock;
                    virtualBlock.transactions = {};
                    virtualBlock.transactions.push_back(tx);
                    virtualBlock.height = 0;
                    virtualBlock.time = 0;
                    vector<EsignatureTransaction> esignTxes = VtcBlockIndexer::Utility::parseEsignatureTransactions(virtualBlock, db, scriptSolver.get(), this );
                    for(EsignatureTransaction estx : esignTxes) {
                        cout << "Found mempool eSign transaction!" << endl;
                        
                        mempoolEsignTransactions.push_back(estx);
                    }

                    vector<IdentityTransaction> identityTxes = VtcBlockIndexer::Utility::parseIdentityTransactions(virtualBlock, db, scriptSolver.get(), this );
                     for(IdentityTransaction idtx : identityTxes) {
                        cout << "Found mempool identity transaction!" << endl;
                        
                        mempoolIdentityTransactions.push_back(idtx);
                    }
                  
                    for(VtcBlockIndexer::TransactionOutput out : tx.outputs) {
                        out.txHash = tx.txHash;
                        vector<string> addresses = scriptSolver->getAddressesFromScript(out.script);
                        for(string address : addresses) {
                            if(addressMempoolTransactions.find(address) == addressMempoolTransactions.end())
                            {
                                addressMempoolTransactions[address] = {};
                            }
                            addressMempoolTransactions[address].push_back(out);
                        }
                    }
                }
            }
        } catch(const jsonrpc::JsonRpcException& e) {
            const std::string message(e.what());
            cout << "Error reading mempool " << message << endl;
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

string VtcBlockIndexer::MempoolMonitor::outpointSpend(string txid, uint32_t vout) {
    for (auto kvp : mempoolTransactions) {
        VtcBlockIndexer::Transaction tx = kvp.second;
        for (VtcBlockIndexer::TransactionInput txi : tx.inputs) {
            if(txi.txHash.compare(txid) == 0 && txi.txoIndex == vout) {
                return tx.txHash;
            }
        }
    }
    return "";
}
 
vector<VtcBlockIndexer::TransactionOutput> VtcBlockIndexer::MempoolMonitor::getTxos(std::string address) {
    if(addressMempoolTransactions.find(address) == addressMempoolTransactions.end())
    {
        return {};
    } 
    return vector<VtcBlockIndexer::TransactionOutput>(addressMempoolTransactions[address]);
}

string VtcBlockIndexer::MempoolMonitor::getTxoAddress(string txid, uint32_t vout) {
    for (auto kvp : mempoolTransactions) {
        VtcBlockIndexer::Transaction tx = kvp.second;
        for (VtcBlockIndexer::TransactionOutput txo : tx.outputs) {
            if(tx.txHash.compare(txid) == 0 && txo.index == vout) {
                vector<string> addresses = scriptSolver->getAddressesFromScript(txo.script);
                if(addresses.size() > 0) return addresses.at(0);
            }
        }
    }
    return "";
}

vector<VtcBlockIndexer::EsignatureTransaction> VtcBlockIndexer::MempoolMonitor::getEsignTransactionsFrom(std::string address) {
    vector<EsignatureTransaction> newVector = {};
    for(EsignatureTransaction tx : mempoolEsignTransactions) {
        if(tx.fromAddress.compare(address) == 0) { 
            newVector.push_back(tx);
        }
    }
    return newVector;
}

vector<VtcBlockIndexer::EsignatureTransaction> VtcBlockIndexer::MempoolMonitor::getEsignTransactionsTo(std::string address) {
    vector<EsignatureTransaction> newVector = {};
    for(EsignatureTransaction tx : mempoolEsignTransactions) {
        if(tx.toAddress.compare(address) == 0) { 
            newVector.push_back(tx);
        }
    }
    return newVector; 
}
    
vector<VtcBlockIndexer::IdentityTransaction> VtcBlockIndexer::MempoolMonitor::getIdentityTransactions(std::string address) {
    vector<IdentityTransaction> newVector = {};
    for(IdentityTransaction tx : mempoolIdentityTransactions) {
        if(tx.toAddress.compare(address) == 0) { 
            newVector.push_back(tx);
        }
    }
    return newVector; 
}
    

void VtcBlockIndexer::MempoolMonitor::transactionIndexed(std::string txid) {
    if(mempoolTransactions.find(txid) != mempoolTransactions.end()) {
        mempoolTransactions.erase(txid);

        unordered_map<string, std::vector<VtcBlockIndexer::TransactionOutput>> changedMempoolAddressTxes;
        for (auto kvp : addressMempoolTransactions) {

            vector<VtcBlockIndexer::TransactionOutput> newVector = {};
            bool itemsRemoved = false;
            for (VtcBlockIndexer::TransactionOutput txo : kvp.second) {
                if(txo.txHash.compare(txid) != 0) {
                    newVector.push_back(txo);
                } else {
                    itemsRemoved = true;
                }
            }

            if(itemsRemoved) {
                changedMempoolAddressTxes[kvp.first] = newVector;
            }
        }

        for (auto kvp : changedMempoolAddressTxes) {
            addressMempoolTransactions[kvp.first] = kvp.second;
        }
    }

    vector<EsignatureTransaction> newVector = {};
    for(EsignatureTransaction tx : mempoolEsignTransactions) {
        if(tx.txId.compare(txid) != 0) { 
            newVector.push_back(tx);
        }
    }
    mempoolEsignTransactions = newVector;
}


