/*
    TODO: Copyright?
*/

/* @file
 * Implementation of the last-level branch predictor (LLBP).
 *
 * TODO: More detailed description and atribution.
 *
 */

#ifndef __CPU_PRED_LLBP_HH__
#define __CPU_PRED_LLBP_HH__

#include "base/types.hh"
#include "cpu/pred/ltage.hh"
#include "params/LLBP.hh"

namespace gem5
{

namespace branch_prediction
{

class LLBP : public LTAGE
{
  public:
    LLBP(const LLBPParams &params);
    void init() override;
};

} // namespace branch_prediction
} // namespace gem5

 #endif // __CPU_PRED_LLBP_HH__
