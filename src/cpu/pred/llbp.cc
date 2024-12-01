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
  : LTAGE(params)
{
}

void
LLBP::init()
{
  DPRINTF(LLBP, "Using experimental LLBP\n");
  LTAGE::init();
}

} // namespace branch_prediction
} // namespace gem5
