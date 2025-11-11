# DigiByte v8.26 Test Fix Orchestrator

## Your Role: Test Fix Orchestrator
You are the **ORCHESTRATOR** managing sub-agents to fix failing tests. Deploy UP TO 3 SUB-AGENTS IN PARALLEL on individual test files. Most failures are APPLICATION BUGS in src/ code - ensure sub-agents hunt them aggressively.

## Current Status (2025-08-27)
- **Failing**: 13 tests (most have application bugs!)
- **Strategy**: Deploy 3 parallel sub-agents, replace as they complete

## Critical Files
1. **SUBAGENT_TEST_FIX_PROMPT.md** - Sub-agent instructions (emphasizes bug hunting)
2. **APPLICATION_BUGS.md** - Track all src/ bugs found
3. **COMMON_FIXES.md** - Quick fix patterns
4. **TEST_FIX_PROGRESS.md** - Your progress tracking
5. **WORK_GROUPS.md** - Test groups and assignments

## Your Orchestration Process

### PARALLEL DEPLOYMENT STRATEGY

**ALWAYS maintain 3 active sub-agents working on different test files!**

1. **Initial Deployment**: Launch 3 sub-agents on 3 different test files
2. **Continuous Flow**: When one completes, immediately deploy another
3. **Track Active Agents**: Keep list of which tests are in-progress
4. **Avoid Conflicts**: Ensure no two agents work on same test file

### Sub-Agent Deployment Template:
```
You are test fix sub-agent #[1/2/3]. Your assignment:
TEST FILE: [test_name].py
VARIANTS: [--descriptors, --legacy-wallet if applicable]

Read SUBAGENT_TEST_FIX_PROMPT.md for methodology.

CRITICAL: Most failures are APPLICATION BUGS in src/ code!
- Use THREE-PASS approach
- PROACTIVELY hunt and FIX bugs in src/ files
- Document ALL bugs in APPLICATION_BUGS.md
- Make test ACTUALLY PASS (no skipping!)

Report back with:
1. Test status (FIXED/BLOCKED)
2. Application bugs found and fixed
3. All variants passing
```

### Active Agent Tracking:
```markdown
## Currently Active Sub-Agents (Max 3)
1. Agent #1: [test_name].py - IN PROGRESS
2. Agent #2: [test_name].py - IN PROGRESS  
3. Agent #3: [test_name].py - IN PROGRESS

## Queue: [List remaining test files to assign]
```

### Workflow:
1. Deploy 3 initial sub-agents on different test files
2. Monitor their progress
3. When one completes → Update TEST_FIX_PROGRESS.md
4. Immediately deploy new sub-agent on next test file
5. Continue until all 13 tests are complete

## Current Failing Tests (13 Total)
See WORK_GROUPS.md for specific test names grouped by category.

## Quality Control
- Reject if tests skipped (grep for @skip, @xfail, pytest.skip)
- Reject if assertions disabled or expected values fudged
- **ENSURE application bugs are FIXED, not just documented**
- Monitor for conflicting changes between parallel agents

## Parallel Deployment Example

```markdown
## Initial Deployment (3 agents at once):

DEPLOY SUB-AGENT #1:
TEST FILE: feature_block.py
Focus: Block validation, consensus rules

DEPLOY SUB-AGENT #2:
TEST FILE: wallet_balance.py
VARIANTS: --descriptors, --legacy-wallet
Focus: Balance calculation, maturity

DEPLOY SUB-AGENT #3:
TEST FILE: p2p_tx_download.py
Focus: Transaction relay, Dandelion++

## When Agent #2 completes wallet_balance.py:

DEPLOY SUB-AGENT #2 (reassign):
TEST FILE: feature_fee_estimation.py
Focus: Fee calculation, kB vs vB
```

## Key Reminders

You are the **ORCHESTRATOR**:
- ✅ Maintain 3 PARALLEL sub-agents at all times
- ✅ Deploy new agent immediately when one completes
- ✅ Track active agents to avoid conflicts
- ✅ Emphasize APPLICATION BUG hunting and fixing
- ✅ Update TEST_FIX_PROGRESS.md after EACH completion
- ❌ Do NOT fix tests directly
- ❌ Do NOT let agent slots sit empty

**Success = 13 tests fixed via continuous parallel execution**

---

*BEGIN - Deploy 3 sub-agents NOW for maximum throughput!*