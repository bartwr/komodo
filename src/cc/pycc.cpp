
#include <stdio.h>
#include <stdlib.h>
#include <Python.h>

#include <cryptoconditions.h>
#include "cc/eval.h"
#include "cc/utils.h"
#include "cc/pycc.h"
#include "primitives/transaction.h"
#include <univalue.h>
#include "CCinclude.h"

#include <vector>
#include <map>


Eval* getEval(PyObject* self)
{
    return ((PyBlockchain*) self)->eval;
}

static PyObject* PyBlockchainGetHeight(PyObject* self, PyObject* args)
{
    auto height = getEval(self)->GetCurrentHeight();
    return PyLong_FromLong(height);
}

static PyObject* PyBlockchainIsSapling(PyObject* self, PyObject* args)
{
    auto consensusBranchId = CurrentEpochBranchId(chainActive.Height() + 1, Params().GetConsensus());
    // 0x76b809bb sapling
    // 0x5ba81b19 overwinter
    // 0 sprout
    return (consensusBranchId == 0x76b809bb) ? Py_True : Py_False;
}

static PyObject* PyBlockchainRpc(PyObject* self, PyObject* args)
{
    char* request; UniValue valRequest;

    if (!PyArg_ParseTuple(args, "s", &request)) {
        PyErr_SetString(PyExc_TypeError, "argument error, expecting json");
        fprintf(stderr, "Parse error\n");
        return NULL;
    }
    valRequest.read(request);
    JSONRequest jreq;
    try {
        if (valRequest.isObject())
        {
            jreq.parse(valRequest);
            UniValue result = tableRPC.execute(jreq.strMethod, jreq.params);
            std::string valStr = result.write(0, 0);
            char* valChr = const_cast<char*> (valStr.c_str());
            return PyUnicode_FromString(valChr);
        }
    } catch (const UniValue& objError) {
        std::string valStr = objError.write(0, 0);
        char* valChr = const_cast<char*> (valStr.c_str());
        return PyUnicode_FromString(valChr);
    } catch (const std::exception& e) {
        return PyUnicode_FromString("RPC parse error2");
    }
    return PyUnicode_FromString("RPC parse error, must be object");
}


static PyObject* PyBlockchainGetTxConfirmed(PyObject* self, PyObject* args)
{
    char* txid_s;
    uint256 txid;
    CTransaction txOut;
    CBlockIndex block;

    if (!PyArg_ParseTuple(args, "s", &txid_s)) {
        PyErr_SetString(PyExc_TypeError, "argument error, expecting hex encoded txid");
        return NULL;
    }

    txid.SetHex(txid_s);

    if (!getEval(self)->GetTxConfirmed(txid, txOut, block)) {
        PyErr_SetString(PyExc_IndexError, "invalid txid");
        return NULL;
    }

    std::vector<uint8_t> txBin = E_MARSHAL(ss << txOut);
    return Py_BuildValue("y#", txBin.begin(), txBin.size());
}

static PyMethodDef PyBlockchainMethods[] = {
    {"get_height", PyBlockchainGetHeight, METH_NOARGS,
     "Get chain height.\n() -> int"},

    {"is_sapling", PyBlockchainIsSapling, METH_NOARGS,
     "Get is sapling active\n() -> bool"},

     {"rpc", PyBlockchainRpc, METH_VARARGS,
      "RPC interface\n({\"method\":method, \"params\":[param0,param1], \"id\":\"rpc_id\"}) -> json"},

    {"get_tx_confirmed", PyBlockchainGetTxConfirmed, METH_VARARGS,
     "Get confirmed transaction. Throws IndexError if not found.\n(txid_hex) -> tx_bin" },

    {NULL, NULL, 0, NULL} /* Sentinel */
};

static PyTypeObject PyBlockchainType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "komodod.PyBlockchain",    /* tp_name */
    sizeof(PyBlockchain),      /* tp_basicsize */
    0,                         /* tp_itemsize */
    0,                         /* tp_dealloc */
    0,                         /* tp_print */
    0,                         /* tp_getattr */
    0,                         /* tp_setattr */
    0,                         /* tp_reserved */
    0,                         /* tp_repr */
    0,                         /* tp_as_number */
    0,                         /* tp_as_sequence */
    0,                         /* tp_as_mapping */
    0,                         /* tp_hash */
    0,                         /* tp_call */
    0,                         /* tp_str */
    0,                         /* tp_getattro */
    0,                         /* tp_setattro */
    0,                         /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,        /* tp_flags */
    "Komodod PyBlockchain",    /* tp_doc */
    0, 0, 0, 0, 0, 0,
    PyBlockchainMethods,       /* tp_methods */
};


PyBlockchain* CreatePyBlockchainAPI(Eval *eval)
{
    PyBlockchain* obj = PyObject_New(PyBlockchain, &PyBlockchainType);
    obj->eval = eval;
    // This does not seem to be neccesary
    // return (PyBlockchain*) PyObject_Init((PyObject*) obj, &PyBlockchainType);
    return obj;
}


void __attribute__ ((constructor)) premain()
{
    Py_InitializeEx(0);

    if (!Py_IsInitialized()) {
      printf("Python failed to initialize\n");
      exit(1);
    }

    if (PyType_Ready(&PyBlockchainType)) {
      printf("PyBlockchainType failed to initialize\n");
      exit(1);
    }
}


PyObject* PyccLoadModule(std::string moduleName)
{
    PyObject* pName = PyUnicode_DecodeFSDefault(&moduleName[0]);
    PyObject* module = PyImport_Import(pName);
    Py_DECREF(pName);
    return module;
}

PyObject* PyccGetFunc(PyObject* pyccModule, std::string funcName)
{
    PyObject* pyccEval = PyObject_GetAttrString(pyccModule, &funcName[0]);
    if (!PyCallable_Check(pyccEval)) {
        if (pyccEval != NULL) Py_DECREF(pyccEval);
        return NULL;
    }
    return pyccEval;
}


PyObject* pyccGlobalEval = NULL;
PyObject* pyccGlobalBlockEval = NULL;
PyObject* pyccGlobalRpc = NULL;

UniValue PyccRunGlobalCCRpc(Eval* eval, UniValue params)
{
    UniValue result(UniValue::VOBJ);
    std::string valStr = params.write(0, 0);
    char* valChr = const_cast<char*> (valStr.c_str());

    PyBlockchain* chain = CreatePyBlockchainAPI(eval);
    PyObject* out = PyObject_CallFunction(
            pyccGlobalRpc,
            "Os", chain, valChr); // FIXME possibly use {items} instead of string

    if (PyErr_Occurred() != NULL) {
        PyErr_PrintEx(0);
        fprintf(stderr, "pycli PyErr_Occurred\n");
        return result;
    }

    if (PyUnicode_Check(out)) {
        long len;
        const char* resp_s = PyUnicode_AsUTF8AndSize(out, &len);
        result.read(resp_s);
    } else { // FIXME test case
        fprintf(stderr, "FIXME?\n");
    }
    Py_DECREF(out);
    return(result);
}


bool PyccRunGlobalCCEval(Eval* eval, const CTransaction& txTo, unsigned int nIn, uint8_t* code, size_t codeLength)
{
    PyBlockchain* chain = CreatePyBlockchainAPI(eval);
    std::vector<uint8_t> txBin = E_MARSHAL(ss << txTo);
    PyObject* out = PyObject_CallFunction(
            pyccGlobalEval,
            "Oy#iy#", chain,
                      txBin.begin(), txBin.size(),
                      nIn,
                      code, codeLength);

    bool valid;

    if (PyErr_Occurred() != NULL) {
        PyErr_PrintEx(0);
        return eval->Error("PYCC module raised an exception");
    }

    if (out == Py_None) {
        valid = eval->Valid();
    } else if (PyUnicode_Check(out)) {
        long len;
        const char* err_s = PyUnicode_AsUTF8AndSize(out, &len);
        valid = eval->Invalid(std::string(err_s, len));
    } else {
        valid = eval->Error("PYCC validation returned invalid type. "
                            "Should return None on success or a unicode error message on failure");
    }
    
    Py_DECREF(out);
    return valid;
}


std::map< std::vector<uint8_t>, UniValue> DecodePrevMinerTx(const std::vector<CTxOut> vouts)
{
    std::vector<uint8_t> vopret;
    std::map< std::vector<uint8_t>, UniValue> result;
    std::string valStr;

    for (std::vector<CTxOut>::const_iterator it=vouts.begin(); it!=vouts.end(); it++)
    {
        UniValue OpretState(UniValue::VOBJ);
        const CTxOut &vout = *it;
        std::vector<uint8_t> eval_code;
        GetOpReturnData(vout.scriptPubKey,vopret);
        if ( E_UNMARSHAL(vopret,ss >> eval_code; ss >> valStr ) != 0 )
        {
            char* valChr = const_cast<char*> (valStr.c_str());
            OpretState.read(valChr);
            result.emplace(eval_code, OpretState);
        }
    }
    return(result);
}

std::map< std::vector<uint8_t>, UniValue> ProcessStateChanges(const std::vector<CTransaction> txs)
{
    std::map< std::vector<uint8_t>, UniValue> mapOprets;
    for (std::vector<CTransaction>::const_iterator it=txs.begin(); it!=txs.end(); it++)
    {
        const CTransaction &tx = *it;
        for (std::vector<CTxIn>::const_iterator vit=tx.vin.begin(); vit!=tx.vin.end(); vit++){
            const CTxIn &vin = *vit;
            if (tx.vout.size() > 0 && tx.vout.back().scriptPubKey.IsOpReturn() && IsCCInput(vin.scriptSig))
            {
                std::vector< std::vector<uint8_t> > vevalcode;
                auto findEval = [](CC *cond, struct CCVisitor _) {
                    bool r = false; 
                    // pyCCs with FSMs can use 2 byte eval code and we filter out ones without here
                    if (cc_typeId(cond) == CC_Eval && cond->codeLength == 2) {
                        std::vector<uint8_t> evalcode;
                        for(int i = 0; i < cond->codeLength; i ++)
                            evalcode.push_back(cond->code[i]);
                        ((std::vector< std::vector<uint8_t> >*)(_.context))->push_back(evalcode);  // store eval code in cc_visitor context which is & of vector of unit8_t var
                        r = true;
                    }
                    // false for a match, true for continue
                    return 1;
                };

                CC *cond = GetCryptoCondition(vin.scriptSig);

                if (cond) {
                    CCVisitor visitor = { findEval, (uint8_t*)"", 0, &vevalcode };
                    bool out = !cc_visit(cond, visitor);  // yes, inverted
                    cc_free(cond);
                    if (vevalcode.size() > 0)
                    {
                        for (auto const &e : vevalcode)  {
                            if (mapOprets.find(e) == mapOprets.end())
                                mapOprets.emplace(e, UniValue(UniValue::VARR));

                            std::string strTx = EncodeHexTx(tx).c_str();
                            const std::vector<UniValue> &vuni = mapOprets[e].getValues();
                            if (std::find_if(vuni.begin(), vuni.end(), [&](UniValue el) { return el.getValStr() == strTx; }) == vuni.end())
                                mapOprets[e].push_back(strTx);
                        }
                    }
                }
            }
        }
    }
    return(mapOprets);
}


// this is decoding a block that is not yet in the index, therefore is a limited version of blocktoJSON function from rpc/blockchain.cpp
// can add any additional data to this result UniValue and it will be passed to cc_block_eval every time komodod validates a block
// FIXME determine if anything else is needed; remove or use txDetails
UniValue tempblockToJSON(const CBlock& block, bool txDetails = true)
{
    UniValue result(UniValue::VOBJ), cc_spends(UniValue::VOBJ);
    std::map< std::vector<uint8_t>, UniValue> changed_states;

    result.push_back(Pair("hash", block.GetHash().GetHex()));
    result.push_back(Pair("minerstate_tx", EncodeHexTx(block.vtx.back())));
    result.push_back(Pair("time", block.GetBlockTime()));


    UniValue txs(UniValue::VARR);
    BOOST_FOREACH(const CTransaction&tx, block.vtx)
    {
        txs.push_back(EncodeHexTx(tx));
    }
    result.push_back(Pair("tx", txs));

    changed_states = ProcessStateChanges(block.vtx);
    for (auto &m : changed_states)
    {
        cc_spends.push_back(Pair(HexStr(m.first), m.second));
    }
    result.push_back(Pair("cc_spends", cc_spends));
    return result;
}

UniValue tempblockindexToJSON(CBlockIndex* blockindex, CBlock &block){
    UniValue result(UniValue::VOBJ);
    if (!ReadBlockFromDisk(block, blockindex, 1)){
        fprintf(stderr, "Can't read previous block from Disk!");
        return(result);
    }
    result = tempblockToJSON(block, 1);
    result.push_back(Pair("height", blockindex->GetHeight()));
    return(result);
}


// the MakeState special case for pycli is expecting ["MakeState", prevblockhash, cc_spendopret0, cc_spendopret1, ...]
// as a result of this, CC validation must ensure that each CC spend has a valid OP_RETURN
// FIXME think it may do this already, but double check. If a CC spend with unparseable OP_RETURN can enter the mempool
// it will cause miners to be unable to produce valid blocks 
bool MakeFauxImportOpret(std::vector<CTransaction> &txs, CBlockIndex* blockindex, CMutableTransaction &minertx)
{
    UniValue resp(UniValue::VOBJ);
    Eval eval;
    CScript result;
    CBlock prevblock;
    
    UniValue prevblockJSON(UniValue::VOBJ);
    prevblockJSON = tempblockindexToJSON(blockindex, prevblock);

    if ( prevblockJSON.empty() ) {
        fprintf(stderr, "PyCC block db error, probably daemon needs rescan or resync");
        return false;
    }
    std::string prevblockStr = prevblockJSON.write(0, 0);
    //char* prevblockChr = const_cast<char*> (prevvalStr.c_str());


    // FIXME sensible bootstrap mechanism 
    if ( 0 ) {//blockindex->GetHeight() == 2 ){
        minertx.vout = prevblock.vtx[prevblock.vtx.size()-1].vout;
        minertx.vout.resize(1);
        std::vector<uint8_t> bootstrap_eval;
        //std::vector<uint8_t> bootstrap_eval2;
        std::string valStr;
        //std::string valStr2;
        //CScript result2;
        valStr = "{\"doom\":\"submit\"}";
        //valStr2 = "{\"stupidstate2\":\"on\"}";

        bootstrap_eval.push_back(0x64);
        bootstrap_eval.push_back(0x64);
        //bootstrap_eval2.push_back(0x66);
        //bootstrap_eval2.push_back(0x66);
        result = CScript() <<  OP_RETURN << E_MARSHAL(ss << bootstrap_eval << valStr);
        //result2 = CScript() <<  OP_RETURN << E_MARSHAL(ss << bootstrap_eval2 << valStr2);
        minertx.vout[0] = CTxOut(0, result);
        //minertx.vout[1] = CTxOut(0, result2);
        return true;
    } else minertx.vout = prevblock.vtx[prevblock.vtx.size()-1].vout;
    
    std::map< std::vector<uint8_t>, UniValue> prevstates;
    prevstates = DecodePrevMinerTx(prevblock.vtx[prevblock.vtx.size()-1].vout);

    std::map< std::vector<uint8_t>, UniValue> changed_states;

    changed_states = ProcessStateChanges(txs);
    
    
    // this will ensure that MakeState is done for every eval code even if a CC spend did not happen within this block
    // is neccesary for "special_events" function 
    for (auto &e : prevstates)  {
        if (changed_states.find(e.first) == changed_states.end())
            changed_states.emplace(e.first, UniValue(UniValue::VARR));
    }

    // this would be a sensible place to handle bootstrapping a new FSM eval code
    int vout_index = 0; // FIXME need a safer way of doing this, index could be thrown off with bugs
    for (auto &m : changed_states)
    {
        m.second.push_back(HexStr(m.first));
        m.second.push_back(prevblockStr);
        m.second.push_back("MakeState");

        resp = ExternalRunCCRpc(&eval, m.second); // this sends [*cc_spends, eval_code_ASCII, "prevblockJSON", "MakeState",] to cc_cli
        if (resp.empty()) return false; // this will make block creation fail as it indicates an issue in the python code

        std::string newstate = resp.write(0, 0);
        std::string prevstate = prevstates[m.first].write(0, 0);

        if ( newstate != prevstate ) {
            result = CScript() <<  OP_RETURN << E_MARSHAL(ss << m.first << newstate);
            minertx.vout[vout_index] = CTxOut(0, result);
        }
        vout_index++;
        //char* valChr = const_cast<char*> (newstate.c_str());
        //fprintf(stderr, "MY PY RESP %s\n", valChr);
    }
    return( true );
}




bool PyccRunGlobalBlockEval(const CBlock& block, CBlockIndex* prevblock_index)
{
    UniValue blockJSON(UniValue::VOBJ);
    UniValue prevblockJSON(UniValue::VOBJ);

    CBlock prevblock;
    prevblockJSON = tempblockindexToJSON(prevblock_index, prevblock);

    //prevblockJSON = tempblockToJSON(prevblock); // FIXME this could maybe use typical blockToJSON instead, gives more data
    std::string prevvalStr = prevblockJSON.write(0, 0);
    char* prevblockChr = const_cast<char*> (prevvalStr.c_str());

    blockJSON = tempblockToJSON(block);





    std::map< std::vector<uint8_t>, UniValue> prev_states;
//result.push_back(Pair("cc_spends", cc_spends));
    prev_states = ProcessStateChanges(prevblock.vtx);
    // is neccesary for "special_events" function 
    //for (auto &e : prevstates)  {
      //  if (blockJSON.(e.first) == changed_states.end())
        //    changed_states.emplace(e.first, UniValue(UniValue::VARR));
    //}
    //const std::string huuuh = "cc_spends";
    //fprintf(stderr, "WHAT %d \n", blockJSON(huuuh).exists());





    std::string valStr = blockJSON.write(0, 0);
    char* blockChr = const_cast<char*> (valStr.c_str());

    PyObject* out = PyObject_CallFunction(
            pyccGlobalBlockEval,
            "ss", blockChr, prevblockChr);
    bool valid;
    // FIXME do python defined DOS ban scores

    if (PyErr_Occurred() != NULL) {
        PyErr_PrintEx(0);
        fprintf(stderr, "PYCC module raised an exception\n");
        return false; //state.DoS(100, error("CheckBlock: PYCC module raised an exception"),
                                 //REJECT_INVALID, "invalid-pycc-block-eval"); 
    }
    if (out == Py_None) {
        valid = true;
    } else if (PyUnicode_Check(out)) {
        long len;
        const char* err_s = PyUnicode_AsUTF8AndSize(out, &len);
        //valid = eval->Invalid(std::string(err_s, len));
        fprintf(stderr, "PYCC module returned string: %s \n", err_s);
        valid = false;
    } else {
        fprintf(stderr, ("PYCC validation returned invalid type. "
                         "Should return None on success or a unicode error message on failure"));
        valid = false;
        //valid = eval->Error("PYCC validation returned invalid type. "
          //                  "Should return None on success or a unicode error message on failure");
    }
    Py_DECREF(out);
    return valid;
}



void PyccGlobalInit(std::string moduleName)
{
    PyObject* pyccModule = PyccLoadModule(moduleName);

    if (pyccModule == NULL) {
        printf("Python module \"%s\" is not importable (is it on PYTHONPATH?)\n", &moduleName[0]);
        exit(1);
    }

    pyccGlobalEval = PyccGetFunc(pyccModule, "cc_eval");
    pyccGlobalBlockEval = PyccGetFunc(pyccModule, "cc_block_eval");
    pyccGlobalRpc = PyccGetFunc(pyccModule, "cc_cli");

    if (!pyccGlobalEval) {
        printf("Python module \"%s\" does not export \"cc_eval\" or not callable\n", &moduleName[0]);
        exit(1);
    }
    if (!pyccGlobalRpc) {
        printf("Python module \"%s\" does not export \"cc_cli\" or not callable\n", &moduleName[0]);
        exit(1);
    }

    if ( ASSETCHAINS_PYCC_FSM > 0 && !pyccGlobalBlockEval) { // FIXME if ac_ param
        printf("Python module \"%s\" does not export \"cc_block_eval\" or not callable\n", &moduleName[0]);
        exit(1);
    }


    ExternalRunCCEval = &PyccRunGlobalCCEval;
    ExternalRunBlockEval = &PyccRunGlobalBlockEval;
    ExternalRunCCRpc = &PyccRunGlobalCCRpc;
}

