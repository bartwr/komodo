// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/******************************************************************************
 * Copyright © 2014-2019 The SuperNET Developers.                             *
 *                                                                            *
 * See the AUTHORS, DEVELOPER-AGREEMENT and LICENSE files at                  *
 * the top-level directory of this distribution for the individual copyright  *
 * holder information and the developer policies on copyright and licensing.  *
 *                                                                            *
 * Unless otherwise agreed in a custom licensing agreement, no part of the    *
 * SuperNET software, including this file may be copied, modified, propagated *
 * or distributed except according to the terms contained in the LICENSE file *
 *                                                                            *
 * Removal or modification of this copyright notice is prohibited.            *
 *                                                                            *
 ******************************************************************************/

#include <stdexcept>

#include "amount.h"
#include "chain.h"
#include "chainparams.h"
#include "checkpoints.h"
#include "crosschain.h"
#include "base58.h"
#include "consensus/validation.h"
#include "cc/eval.h"
#include "main.h"
#include "primitives/transaction.h"
#include "rpc/server.h"
#include "streams.h"
#include "sync.h"
#include "util.h"
#include "script/script.h"
#include "script/script_error.h"
#include "script/sign.h"
#include "script/standard.h"

#include <stdint.h>

#include <univalue.h>

#include <regex>


#include "cc/CCinclude.h"
#include "cc/CCPrices.h"

using namespace std;

int32_t ensure_CCrequirements(uint8_t evalcode);

UniValue test_burntx(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    // make fake token tx: 
    struct CCcontract_info *cp, C;

    if (fHelp || (params.size() != 1))
        throw runtime_error("incorrect params\n");
    if (ensure_CCrequirements(EVAL_TOKENS) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);

    uint256 tokenid = Parseuint256((char *)params[0].get_str().c_str());

    CPubKey myPubkey = pubkey2pk(Mypubkey());
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());

    int64_t normalInputs = AddNormalinputs(mtx, myPubkey, 10000, 60);
    if (normalInputs < 10000)
        throw runtime_error("not enough normals\n");

    CPubKey burnpk = pubkey2pk(ParseHex(CC_BURNPUBKEY));

    mtx.vin.push_back(CTxIn(tokenid, 0));
    mtx.vin.push_back(CTxIn(tokenid, 1));
    mtx.vout.push_back(MakeTokensCC1vout(EVAL_TOKENS, 1, burnpk));

    std::vector<CPubKey> voutPubkeys;
    voutPubkeys.push_back(burnpk);

    cp = CCinit(&C, EVAL_TOKENS);

    std::vector<uint8_t> vopret;
    GetNonfungibleData(tokenid, vopret);
    if (vopret.size() > 0)
        cp->additionalTokensEvalcode2 = vopret.begin()[0];

    uint8_t tokenpriv[33];
    char unspendableTokenAddr[64];
    CPubKey unspPk = GetUnspendable(cp, tokenpriv);
    GetCCaddress(cp, unspendableTokenAddr, unspPk);
    CCaddr2set(cp, EVAL_TOKENS, unspPk, tokenpriv, unspendableTokenAddr);
    return(FinalizeCCTx(0, cp, mtx, myPubkey, 10000, EncodeTokenOpRet(tokenid, voutPubkeys, std::make_pair(0, vscript_t()))));
}

UniValue test_proof(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    UniValue result(UniValue::VOBJ);
    std::vector<uint8_t>proof;

    if (fHelp || (params.size() != 2))
        throw runtime_error("incorrect params\n");


    proof = ParseHex(params[0].get_str());
    uint256 cointxid = Parseuint256((char *)params[1].get_str().c_str());

    std::vector<uint256> txids;

    CMerkleBlock merkleBlock;
    if (!E_UNMARSHAL(proof, ss >> merkleBlock)) {
        result.push_back(Pair("error", "could not unmarshal proof"));
        return result;
    }
    uint256 merkleRoot = merkleBlock.txn.ExtractMatches(txids);

    result.push_back(Pair("source_root", merkleRoot.GetHex()));

    for (int i = 0; i < txids.size(); i++)
        std::cerr << "merkle block txid=" << txids[0].GetHex() << std::endl;


    std::vector<bool> vMatches(txids.size());
    for (auto v : vMatches) v = true;
    CPartialMerkleTree verifTree(txids, vMatches);

    result.push_back(Pair("verif_root", verifTree.ExtractMatches(txids).GetHex()));

    if (std::find(txids.begin(), txids.end(), cointxid) == txids.end()) {
        fprintf(stderr, "invalid proof for this cointxid\n");
    }

    std::vector<uint256> vMerkleTree;
    bool f;
    ::BuildMerkleTree(&f, txids, vMerkleTree);

    std::vector<uint256> vMerkleBranch = ::GetMerkleBranch(0, txids.size(), vMerkleTree);

    uint256 ourResult = SafeCheckMerkleBranch(zeroid, vMerkleBranch, 0);
    result.push_back(Pair("SafeCheckMerkleBranch", ourResult.GetHex()));

    return result;
}

extern CScript prices_costbasisopret(uint256 bettxid, CPubKey mypk, int32_t height, int64_t costbasis);
UniValue test_pricesmarker(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    // make fake token tx: 
    struct CCcontract_info *cp, C;

    if (fHelp || (params.size() != 1))
        throw runtime_error("incorrect params\n");
    if (ensure_CCrequirements(EVAL_PRICES) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);

    uint256 bettxid = Parseuint256((char *)params[0].get_str().c_str());

    cp = CCinit(&C, EVAL_PRICES);
    CPubKey myPubkey = pubkey2pk(Mypubkey());
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());

    int64_t normalInputs = AddNormalinputs(mtx, myPubkey, 10000, 60);
    if (normalInputs < 10000)
        throw runtime_error("not enough normals\n");

    mtx.vin.push_back(CTxIn(bettxid, 1));
    mtx.vout.push_back(CTxOut(1000, CScript() << ParseHex(HexStr(myPubkey)) << OP_CHECKSIG));

    return(FinalizeCCTx(0, cp, mtx, myPubkey, 10000, prices_costbasisopret(bettxid, myPubkey, 100, 100)));
}


static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         okSafeMode
  //  --------------------- ------------------------  -----------------------  ----------

    /* Not shown in help */
    { "hidden",             "test_proof",             &test_proof,              true },
    { "hidden",             "test_burntx",            &test_burntx,             true },
    { "hidden",             "test_pricesmarker",      &test_pricesmarker,       true }
};

void RegisterTesttransactionsRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
