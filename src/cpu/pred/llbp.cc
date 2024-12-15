/*
    TODO: Copyright?
*/

/* @file
 * Implementation of the last-level branch predictor (LLBP).
 */

#include "cpu/pred/llbp.hh"

#include "debug/LLBP.hh"

namespace gem5
{

namespace branch_prediction
{

LLBP::LLBP(const LLBPParams &params)
  : LTAGE(params),
    rcr(3, 8, 8, 2, params.CTWidth),
    contextCapacity(params.contextCapacity),
    patternBufferCapacity(params.patternBufferCapacity),
    // storageCapacity(100),
    storageCapacity(params.storageCapacity),
    ctxCounterBits(params.ctxCounterBits),
    ptnCounterBits(params.ptnCounterBits)
    // storage("llbpStorage", 1<<16, 8, mem::LRURP, )
{
  storagePriority.resize(1<<ctxCounterBits);
  DPRINTF(LLBP, "Using experimental LLBP\n");
  DPRINTF(LLBP, "RCR: T=%d,  W=%d,  D=%d,  S=%d,  CTWidth=%d\n",
                rcr.T, rcr.W, rcr.D, rcr.S, params.CTWidth);
  DPRINTF(LLBP, "Storage: cap=%d,  bits=%d,  PQ=%d\n",
                storageCapacity, ctxCounterBits, storagePriority.size());
  DPRINTF(LLBP, "Context: cap=%d,  bits=%d\n",
                contextCapacity, ptnCounterBits);
}

void
LLBP::init()
{
  LTAGE::init();
}

void
LLBP::squash(ThreadID tid, void * &bp_history)
{
  LTAGE::squash(tid, bp_history);
}

void
LLBP::update(ThreadID tid, Addr pc, bool taken,
            void * &bp_history, bool squashed,
            const StaticInstPtr & inst, Addr target)
{
  LLBPBranchInfo* bi = static_cast<LLBPBranchInfo*>(bp_history);
  if(!inst->isUncondCtrl()){
    storageUpdate(tid, pc, rcr.getCCID(), taken, bi);
    // counter = storage[ccid];
  }

  rcr.update(pc, inst, taken);
  LTAGE::update(tid, pc, taken, bp_history, squashed, inst, target);
  auto tag = tage->gtag(tid, pc, 0);
  auto ccid = rcr.getCCID();

  DPRINTF(LLBP, "[thread %d] Updated %s on %lld (%s): ccid=%llu, counter=%d, uncond=%s, sz=%d\n",
          tid,
          taken ? "true":"false",
          pc,
          inst->getName().c_str(),
          rcr.getCCID(),
          -1,
          inst->isUncondCtrl() ? "true":"false",
          storage.size());
}

//prediction
bool
LLBP::predict(ThreadID tid, Addr branch_pc, bool cond_branch, void* &b)
{
    Addr pc = branch_pc;
    bool tage_prediction = LTAGE::predict(tid, branch_pc, cond_branch, b);
    LLBPBranchInfo* bi = static_cast<LLBPBranchInfo*>(b);
    auto tage_bi = bi->tageBranchInfo;
    bi->overrided = false;
    bi->pred_taken = tage_prediction;

    int8_t llbp_confidence = 127;

    if (cond_branch) {
        int tage_bank = tage->nHistoryTables;
        if(tage_bi->provider == TAGEBase::TAGE_LONGEST_MATCH)
            tage_bank = tage_bi->hitBank;
        if(tage_bi->provider == TAGEBase::TAGE_ALT_MATCH)
            tage_bank = tage_bi->altBank;
        auto ccid = rcr.getCCID();
        if(storage.count(ccid)){
            int i = findBestPattern(tid, pc, storage[ccid]);
            if(i >= 0){
                llbp_hits++;
                llbp_confidence = storage[ccid].patterns[i].counter;
                bool llbp_prediction = llbp_confidence >= 0;
                if(i > tage_bank){
                    bi->overrided = true;
                    bi->pred_taken = llbp_prediction;
                }
            }
        }
        if(bi->overrided) overides++;
        DPRINTF(LLBP, "[thread %d] Predicted %s%s on %lld: overides=%d, llbp_hits=%d, confidence=%d, hitBank=%d, ccid=%llu\n",
                    tid,
                    bi->pred_taken ? "true":"false",
                    bi->overrided ? "'" : "",
                    branch_pc,
                    overides,
                    llbp_hits,
                    llbp_confidence,
                    bi->tageBranchInfo->hitBank,
                    ccid);
    }
    return bi->pred_taken;
}


/* Begin LLBP DS */

int8_t LLBP::normalize(int8_t counter){
    return counter >= 0 ? counter : -counter-1;
}


void LLBP::storageUpdate(ThreadID tid, Addr pc, uint64_t cid, bool taken, LLBPBranchInfo* bi){
    if(storage.count(cid)) {
        storagePriority[storage[cid].replace].erase(cid);
        int i = findBestPattern(tid, pc, storage[cid]);
        if(i >= 0) {
            TAGEBase::ctrUpdate(storage[cid].patterns[i].counter, taken, ptnCounterBits);
            if (storage[cid].patterns[i].counter == (taken ? 1 : -2)) {
                // entry became medium confident
                TAGEBase::unsignedCtrUpdate(storage[cid].replace, true, ctxCounterBits);
            }
            else if (storage[cid].patterns[i].counter == (taken ? -1 : 0)) {
                // entry became low confident
                TAGEBase::unsignedCtrUpdate(storage[cid].replace, false, ctxCounterBits);
            }
        }
        i = findVictimPattern(i, storage[cid]);
        if(i >=0 && bi->overrided && bi->pred_taken != taken){
            storage[cid].patterns[i].tag = tage->gtag(tid, pc, i);
            storage[cid].patterns[i].counter = taken ? 0 : -1;
        }
    }else{
        while(storage.size() >= storageCapacity){
            uint64_t i = findVictimContext();
            if(i >= 0){
                storagePriority[storage[i].replace].erase(i);
                storage.erase(i);
            }else{
                storage = {};
            }
        }
        storage[cid] = {};
        storage[cid].patterns.resize(tage->nHistoryTables + 1);
        for(int i=1; i<=tage->nHistoryTables; i++){
            storage[cid].patterns[i].tag = tage->gtag(tid, pc, i);
            storage[cid].patterns[i].counter = taken ? 0 : -1;
        }
    }
    if(std::find(patternBuffer.begin(), patternBuffer.end(), cid) == patternBuffer.end()){
        patternBuffer.push_back(cid);
        if(patternBuffer.size() > patternBufferCapacity) patternBuffer.pop_front();
    }
    storagePriority[storage[cid].replace].insert(cid);
    // storagePriority[normalize(storage[cid])].insert(cid);
}

int LLBP::findBestPattern(ThreadID tid, Addr pc, Context& ctx){
    for(int i=tage->nHistoryTables; i>0; i--){
        if(ctx.patterns[i].tag == tage->gtag(tid, pc, i)){
            return i;
        }
    }
    return -1;
}

int LLBP::findVictimPattern(int min, Context& ctx){
    int min_conf = 1<<(ptnCounterBits-2);
    for(int i=(min>=0 ? min+1 : 1); i<=tage->nHistoryTables; i++){
        if(normalize(ctx.patterns[i].counter) < min_conf){
            return i;
        }
    }
    return -1;
}

uint64_t LLBP::findVictimContext(){
    for(int i=0; i<storagePriority.size(); i++){
        if(storagePriority.size()){
            return *storagePriority[i].begin();
        }
    }
    return -1;
}

/* from LLBP source code: */

LLBP::RCR::RCR(int _T, int _W, int _D, int _shift, int _CTWidth)
: CTWidth(_CTWidth), T(_T), W(_W), D(_D), S(_shift)
{
    bb[0].resize(maxwindow);
    bb[1].resize(maxwindow);
    ctxs = {0, 0};
}

/*
* Given the {n} number of branches staring from vec[end-start]
* to vec[end-start-n-1] we create the hash function by shifting
* each PC by {shift} number if bits i.e.
*
*   000000000000|  PC  |    :vec[end-start]
* ^ 0000000000|  PC  |00    :vec[end-start-1]
* ^ 00000000|  PC  |0000    :vec[end-start-2]
*           .                     .
*           .                     .
*           .                     .
* ^ |  PC  |000000000000    :vec[end-start-n-1]
* ----------------------
*       final hash value
* */
uint64_t LLBP::RCR::calcHash(std::list<uint64_t> &vec, int n, int start, int shift) {
    uint64_t hash = 0;
    if (vec.size() < (start + n)) {
        return 0;
    }
    uint64_t sh = 0;
    auto it = vec.begin();
    std::advance(it, start);
    for (; (it != vec.end()) && (n > 0); it++, n--) {
        uint64_t val = *it;

        // Shift the value
        hash ^= val << uint64_t(sh);

        sh += shift;
        if (sh >= CTWidth) {
            sh -= uint64_t(CTWidth);
        }
    }
    return hash & ((1 << CTWidth) - 1);
}

uint64_t LLBP::RCR::getCCID() {
    return ctxs.ccid & ((1 << CTWidth) - 1);
}

uint64_t LLBP::RCR::getPCID() {
    return ctxs.pcid & ((1 << CTWidth) - 1);
}


bool LLBP::RCR::update(Addr pc, const StaticInstPtr & inst, bool taken) {
    branchCount++;
    // Hash of all branches


    switch (T) {
        case 0: // All branches
        bb[0].push_front(pc);
        bb[1].push_front(branchCount);
        break;

        case 1: // Only calls
        if (inst->isCall()) {
            bb[0].push_front(pc);
            bb[1].push_front(branchCount);
        }
        break;

        case 2: // Only calls and returns
        if (inst->isCall() || inst->isReturn()) {
            bb[0].push_front(pc);
            bb[1].push_front(branchCount);
        }
        break;

        case 3: // Only unconditional branches
        if (inst->isUncondCtrl()) {
            bb[0].push_front(pc);
            bb[1].push_front(branchCount);
        }
        break;

        case 4: // All taken branches
        if (taken) {
            bb[0].push_front(pc);
            bb[1].push_front(branchCount);
        }
        break;
    }

    // PRINTIF(COND,"UH:%llx, %i, %i\n", pc, opType, taken);
    // If the size has changed the hash has changed
    bool changed = false;
    if (bb[0].size() > maxwindow) {
        changed = true;

        // Resize the history
        bb[0].pop_back();
        bb[1].pop_back();

        // The current context.
        ctxs.ccid = calcHash(bb[0], W, D, S);
        // The prefetch context.
        ctxs.pcid = calcHash(bb[0], W, 0, S);
    }
    return changed;
}

/* End LLBP DS*/


} // namespace branch_prediction
} // namespace gem5
