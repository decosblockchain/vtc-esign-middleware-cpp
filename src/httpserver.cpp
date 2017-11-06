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
#include "httpserver.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <iomanip>
#include <vector>
#include <memory>
#include <cstdlib>
#include <restbed>
#include "json.hpp"
#include "utility.h"
using namespace std;
using namespace restbed;
using json = nlohmann::json;


VtcBlockIndexer::HttpServer::HttpServer(leveldb::DB* dbInstance, string blocksDir) : blockReader("") {
    this->db = dbInstance;
    this->blocksDir = blocksDir;
    this->blockReader = VtcBlockIndexer::BlockReader(blocksDir);
    httpClient.reset(new jsonrpc::HttpClient("http://middleware:middleware@vertcoind:8332"));
    vertcoind.reset(new VertcoinClient(*httpClient));
}

void VtcBlockIndexer::HttpServer::getTransaction(const shared_ptr<Session> session) {
    const auto request = session->get_request();
    
    cout << "Looking up txid " << request->get_path_parameter("id") << endl;
    
    try {
        const Json::Value tx = vertcoind->getrawtransaction(request->get_path_parameter("id"), true);
        
        stringstream body;
        body << tx.toStyledString();
        
        session->close(OK, body.str(), {{"Content-Type","application/json"},{"Content-Length",  std::to_string(body.str().size())}});
    } catch(const jsonrpc::JsonRpcException& e) {
        const std::string message(e.what());
        cout << "Not found " << message << endl;
        session->close(404, message, {{"Content-Type","application/json"},{"Content-Length",  std::to_string(message.size())}});
    }
}


void VtcBlockIndexer::HttpServer::getTransactionProof(const shared_ptr<Session> session) {
    const auto request = session->get_request();
    
    std::string blockHash;
    std::string txId = request->get_path_parameter("id","");
    leveldb::Status s = this->db->Get(leveldb::ReadOptions(), "tx-" + txId + "-block", &blockHash);
    if(!s.ok()) // no key found
    {
        const std::string message("TX not found");
        session->close(404, message, {{"Content-Length",  std::to_string(message.size())}});
        return;
    }

    std::string blockHeightString;
    s = this->db->Get(leveldb::ReadOptions(), "block-hash-" + blockHash, &blockHeightString);
    if(!s.ok()) // no key found
    {
        const std::string message("Block not found");
        session->close(404, message, {{"Content-Length",  std::to_string(message.size())}});
        return;
    }
    uint64_t blockHeight = stoll(blockHeightString);
    json j;
    j["txHash"] = txId;
    j["blockHash"] = blockHash;
    j["blockHeight"] = blockHeight;
    json chain = json::array();
    for(uint64_t i = blockHeight+1; --i > 0 && i > blockHeight-10;) {
        stringstream blockKey;
        blockKey << "block-filePosition-" << setw(8) << setfill('0') << i;
   
        std::string filePosition;
        s = this->db->Get(leveldb::ReadOptions(), blockKey.str(), &filePosition);
        if(!s.ok()) // no key found
        {
            const std::string message("Block not found");
            session->close(404, message, {{"Content-Length",  std::to_string(message.size())}});
            return;
        }
        bool testnet = false;
        if(filePosition.size() > 24) {
            testnet = (stoi(filePosition.substr(24)) == 1);
        }
        Block block = this->blockReader.readBlock(filePosition.substr(0,12),stoll(filePosition.substr(12,12)),i,testnet,true);

        json jsonBlock;
        jsonBlock["blockHash"] = block.blockHash;
        jsonBlock["previousBlockHash"] = block.previousBlockHash;
        jsonBlock["merkleRoot"] = block.merkleRoot;
        jsonBlock["version"] = block.version;
        jsonBlock["time"] = block.time;
        jsonBlock["bits"] = block.bits;
        jsonBlock["nonce"] = block.nonce;
        jsonBlock["height"] = block.height;
        chain.push_back(jsonBlock);

    }
    j["chain"] = chain;
    string body = j.dump();
    
   session->close( OK, body, { { "Content-Type",  "application/json" }, { "Content-Length",  std::to_string(body.size()) } } );
}

void VtcBlockIndexer::HttpServer::addressBalance( const shared_ptr< Session > session )
{
    long long balance = 0;
    int txoCount = 0;
    const auto request = session->get_request( );
    
    cout << "Checking balance for address " << request->get_path_parameter( "address" ) << endl;

    string start(request->get_path_parameter( "address" ) + "-txo-00000001");
    string limit(request->get_path_parameter( "address" ) + "-txo-99999999");
    
    leveldb::Iterator* it = this->db->NewIterator(leveldb::ReadOptions());
    
    for (it->Seek(start);
            it->Valid() && it->key().ToString() < limit;
            it->Next()) {

        string spentTx;
        txoCount++;
        string txo = it->value().ToString();

        leveldb::Status s = this->db->Get(leveldb::ReadOptions(), "txo-" + txo.substr(0,64) + "-" + txo.substr(64,8) + "-spent", &spentTx);
        if(!s.ok()) // no key found, not spent. Add balance.
        {
            balance += stoll(txo.substr(80));
        }
    }
    assert(it->status().ok());  // Check for any errors found during the scan
    delete it;

    cout << "Analyzed " << txoCount << " TXOs - Balance is " << balance << endl;
 
    stringstream body;
    body << balance;
    
    session->close( OK, body.str(), { {"Content-Type","text/plain"}, { "Content-Length",  std::to_string(body.str().size()) } } );
}

void VtcBlockIndexer::HttpServer::addressTxos( const shared_ptr< Session > session )
{
    json j = json::array();

    const auto request = session->get_request( );

    long long sinceBlock = stoll(request->get_path_parameter( "sinceBlock", "0" ));
    
    cout << "Checking balance for address " << request->get_path_parameter( "address" ) << endl;

    string start(request->get_path_parameter( "address" ) + "-txo-00000001");
    string limit(request->get_path_parameter( "address" ) + "-txo-99999999");
    
    leveldb::Iterator* it = this->db->NewIterator(leveldb::ReadOptions());
    
    for (it->Seek(start);
            it->Valid() && it->key().ToString() < limit;
            it->Next()) {

        string spentTx;
        string txo = it->value().ToString();

        leveldb::Status s = this->db->Get(leveldb::ReadOptions(), "txo-" + txo.substr(0,64) + "-" + txo.substr(64,8) + "-spent", &spentTx);
        long long block = stoll(txo.substr(72,8));
        if(block >= sinceBlock) {
            json txoObj;
            txoObj["txhash"] = txo.substr(0,64);
            txoObj["vout"] = stoll(txo.substr(64,8));
            txoObj["block"] = block;
            txoObj["value"] = stoll(txo.substr(80));
            if(!s.ok()) {
                txoObj["spender"] = nullptr;
            } else {
                txoObj["spender"] = spentTx.substr(65, 64);
            }
            j.push_back(txoObj);
        }
    }
    assert(it->status().ok());  // Check for any errors found during the scan
    delete it;


    string body = j.dump();
     
    session->close( OK, body, { { "Content-Type",  "application/json" }, { "Content-Length",  std::to_string(body.size()) } } );
}

void VtcBlockIndexer::HttpServer::outpointSpend( const shared_ptr< Session > session )
{
    json j;

    const auto request = session->get_request( );

    long long vout = stoll(request->get_path_parameter( "vout", "0" ));
    stringstream txoId;
    txoId << "txo-" << request->get_path_parameter("txid", "") << "-" << setw(8) << setfill('0') << vout << "-spent";
    cout << "Checking outpoint spent " << txoId.str() << endl;
    string spentTx;
    leveldb::Status s = this->db->Get(leveldb::ReadOptions(), txoId.str(), &spentTx);
    j["spent"] = s.ok();
    if(s.ok()) {
        j["spender"] = spentTx.substr(65, 64);
    } else {
        cout << s.ToString() << endl;
    }
    string body = j.dump();
     
    session->close( OK, body, { { "Content-Type",  "application/json" }, { "Content-Length",  std::to_string(body.size()) } } );
} 


void VtcBlockIndexer::HttpServer::outpointSpends( const shared_ptr< Session > session )
{
    const auto request = session->get_request( );
    size_t content_length = 0;
    content_length = request->get_header( "Content-Length", 0);
    session->fetch( content_length, [ request, this ]( const shared_ptr< Session > session, const Bytes & body )
    {
        string content =string(body.begin(), body.end());
        json output = json::array();
        json input = json::parse(content);
        if(!input.is_null()) {
            for (auto& txo : input) {
                if(txo.is_object() && txo["txid"].is_string() && txo["vout"].is_number()) {
                    stringstream txoId;
                    txoId << "txo-" << txo["txid"].get<string>() << "-" << setw(8) << setfill('0') << txo["vout"].get<int>() << "-spent";
                    cout << "Checking outpoint spent " << txoId.str() << endl;
            
                    
                    string spentTx;
                    leveldb::Status s = this->db->Get(leveldb::ReadOptions(), txoId.str(), &spentTx);
                    if(s.ok()) {
                        json j;
                        j["txid"] = txo["txid"];
                        j["vout"] = txo["vout"];
                        j["spender"] = spentTx.substr(65, 64);
                        output.push_back(j);
                    }
                }
            }
        }
    
        string resultBody = output.dump();
        session->close( OK, resultBody, { { "Content-Type",  "application/json" }, { "Content-Length",  std::to_string(resultBody.size()) } } );
    } );
} 

void VtcBlockIndexer::HttpServer::sendRawTransaction( const shared_ptr< Session > session )
{
    const auto request = session->get_request( );
    const size_t content_length = request->get_header( "Content-Length", 0);
    session->fetch( content_length, [ request, this ]( const shared_ptr< Session > session, const Bytes & body )
    {
        const string rawtx = string(body.begin(), body.end());
        
        try {
            const auto txid = vertcoind->sendrawtransaction(rawtx);
            
            session->close(OK, txid, {{"Content-Type","text/plain"}, {"Content-Length",  std::to_string(txid.size())}});
        } catch(const jsonrpc::JsonRpcException& e) {
            const std::string message(e.what());
            session->close(400, message, {{"Content-Type","text/plain"},{"Content-Length",  std::to_string(message.size())}});
        }
    });
} 

void VtcBlockIndexer::HttpServer::run()
{
    auto addressBalanceResource = make_shared< Resource >( );
    addressBalanceResource->set_path( "/addressBalance/{address: .*}" );
    addressBalanceResource->set_method_handler( "GET", bind( &VtcBlockIndexer::HttpServer::addressBalance, this, std::placeholders::_1) );

    auto addressTxosResource = make_shared< Resource >( );
    addressTxosResource->set_path( "/addressTxos/{address: .*}" );
    addressTxosResource->set_method_handler( "GET", bind( &VtcBlockIndexer::HttpServer::addressTxos, this, std::placeholders::_1) );

    auto addressTxosSinceBlockResource = make_shared< Resource >( );
    addressTxosSinceBlockResource->set_path( "/addressTxosSince/{sinceBlock: ^[0-9]*$}/{address: .*}" );
    addressTxosSinceBlockResource->set_method_handler( "GET", bind( &VtcBlockIndexer::HttpServer::addressTxos, this, std::placeholders::_1) );
    
    auto getTransactionResource = make_shared<Resource>();
    getTransactionResource->set_path( "/getTransaction/{id: [0-9a-f]*}" );
    getTransactionResource->set_method_handler("GET", bind(&VtcBlockIndexer::HttpServer::getTransaction, this, std::placeholders::_1) );

    auto getTransactionProofResource = make_shared<Resource>();
    getTransactionProofResource->set_path( "/getTransactionProof/{id: [0-9a-f]*}" );
    getTransactionProofResource->set_method_handler("GET", bind(&VtcBlockIndexer::HttpServer::getTransactionProof, this, std::placeholders::_1) );

    auto outpointSpendResource = make_shared<Resource>();
    outpointSpendResource->set_path( "/outpointSpend/{txid: [0-9a-f]*}/{vout: [0-9]*}" );
    outpointSpendResource->set_method_handler("GET", bind(&VtcBlockIndexer::HttpServer::outpointSpend, this, std::placeholders::_1) );

    auto outpointSpendsResource = make_shared<Resource>();
    outpointSpendsResource->set_path( "/outpointSpends" );
    outpointSpendsResource->set_method_handler("POST", bind(&VtcBlockIndexer::HttpServer::outpointSpends, this, std::placeholders::_1) );

    auto sendRawTransactionResource = make_shared<Resource>();
    sendRawTransactionResource->set_path( "/sendRawTransaction" );
    sendRawTransactionResource->set_method_handler("POST", bind(&VtcBlockIndexer::HttpServer::sendRawTransaction, this, std::placeholders::_1) );

    auto settings = make_shared< Settings >( );
    settings->set_port( 8888 );
    settings->set_default_header( "Connection", "close" );

    Service service;
    service.publish( addressBalanceResource );
    service.publish( addressTxosResource );
    service.publish( addressTxosSinceBlockResource );
    service.publish( getTransactionResource );
    service.publish( getTransactionProofResource );
    service.publish( outpointSpendResource );
    service.publish( outpointSpendsResource );
    service.publish( sendRawTransactionResource );
    service.start( settings );
}
