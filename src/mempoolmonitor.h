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

#include "vertcoinrpc.h"
#include <jsonrpccpp/client/connectors/httpclient.h>
#include <memory>
#include "blockreader.h"
#include "scriptsolver.h"
#include "leveldb/db.h"
#include "leveldb/write_batch.h"
#include <unordered_map>
#ifndef MEMPOOLMONITOR_H_INCLUDED
#define MEMPOOLMONITOR_H_INCLUDED


namespace VtcBlockIndexer {

/**
 * The BlockFileWatcher class provides methods to watch and scan a blocks directory
 * and process the blockfiles when changes occur.
 */

class MempoolMonitor {
public:
    /** Constructs a MempoolMonitor instance
     */
    MempoolMonitor(leveldb::DB* dbInstance);

    /** Starts watching the mempool for new transactions */
    void startWatcher();

    /** Notify a transaction has been indexed - remove it from the mempool */
    void transactionIndexed(std::string txid);

    /** Returns the spender txid if an outpoint is spent */
    std::string outpointSpend(std::string txid, uint32_t vout);

    /** Returns TXOs in the memorypool matching an address */
    std::vector<VtcBlockIndexer::TransactionOutput> getTxos(std::string address);

    string getTxoAddress(string txid, uint32_t vout);

    std::vector<VtcBlockIndexer::EsignatureTransaction> getEsignTransactionsFrom(std::string address);
    std::vector<VtcBlockIndexer::EsignatureTransaction> getEsignTransactionsTo(std::string address);
    std::vector<VtcBlockIndexer::IdentityTransaction> getIdentityTransactions(std::string address);
    
    bool testnet;
private:
    leveldb::DB* db;
    std::unique_ptr<VertcoinClient> vertcoind;
    std::unique_ptr<jsonrpc::HttpClient> httpClient;
    std::vector <VtcBlockIndexer::EsignatureTransaction> mempoolEsignTransactions;
    std::vector <VtcBlockIndexer::IdentityTransaction> mempoolIdentityTransactions;
    unordered_map<string, VtcBlockIndexer::Transaction> mempoolTransactions;
    unordered_map<string, std::vector<VtcBlockIndexer::TransactionOutput>> addressMempoolTransactions;
    std::unique_ptr<VtcBlockIndexer::BlockReader> blockReader;
    std::unique_ptr<VtcBlockIndexer::ScriptSolver> scriptSolver;
}; 

}

#endif // MEMPOOLMONITOR_H_INCLUDED