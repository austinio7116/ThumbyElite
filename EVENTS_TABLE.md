# Indemnity Run — Event & Outcome Table (as of Phase 3, 2026-06-12)

All 26 events, every choice, every outcome with branch odds. `CR` credits,
`REP` faction reputation (local = the faction owning the current system),
`LORE n` unlocks DATABASE entry n, `FLAG n` story flag (gates later events).

**What REP actually does:** it scales mission rewards and market prices in
that faction's space (mission.c). It is a real economy lever — but nothing
in the event UI says so, which is why rep-only outcomes read as "nothing
happened". See "Known thin spots" at the bottom.

Dock hails fire on 35% of dockings; bar encounters 60% per visit; arrival
hails 14% per drop; derelict boardings always deal (the 24% spawn was the
roll). Same-visit outcomes are fixed (no save-scum rerolls).

---

## Dock hails (TRIG_DOCK)

| # | Event | Offered when | Choice | Outcome |
|---|-------|--------------|--------|---------|
| 1 | DISTRESS HAIL | always | GIVE THEM FUEL *(needs ≥2.0 LY)* | −1.0 LY fuel, +4 REP local, **+150 CR wired to your next dock** |
| | | | DEMAND PAYMENT *(needs ≥2.0 LY)* | 60%: −1.0 LY, **+200 CR** · 40%: nothing ("can't pay") |
| | | | TURN AWAY | −2 REP local |
| 2 | CUSTOMS SWEEP | lawful gov + carrying illegal | SUBMIT TO SEARCH | lose all illegal cargo, −150 CR |
| | | | BRIBE THE INSPECTOR (300 CR) | 70%: keep everything · 30%: lose illegal cargo + LEGAL worsens |
| | | | DUMP IT AND DENY | lose all illegal cargo (no fine) |
| 3 | THE PROSPECTOR *(oneshot)* | always | BUY THE DATA (150 CR) | 70%: **LORE 3** · 30%: junk (money gone) |
| | | | WAVE HIM OFF | nothing |
| 4 | STOWAWAY | always | HAND THEM OVER | +50 CR, +3 REP local |
| | | | LET THEM WORK PASSAGE | FLAG 1 → unlocks #26 later (repaid +150 CR or +3 REP) |
| | | | CHARGE THEM FARE | +100 CR |
| 5 | BERTHING TAX | threat ≥2 | PAY THE TOLL (200 CR) | nothing else (peace) |
| | | | REFUSE | 55%: bluff, nothing · 45%: **2-ship ambush waits outside** |
| | | | THREATEN THEM BACK *(wanted only)* | they back down, free |
| 6 | FIRE SALE | ≥2 cargo space | BUY THE LOT (200 CR) | +4 random legal good; 15%: goods are stolen → LEGAL worsens |
| | | | TOO GOOD TO BE TRUE | nothing |
| 7 | THE PREACHER *(oneshot)* | always | LISTEN | **LORE 5** |
| | | | DONATE (25 CR) | LORE 5 + FLAG 2 (future hook) |
| | | | MOVE ALONG | nothing |
| 8 | CLINIC SHORTFALL | always | DONATE MEDICINE *(needs MEDICINE)* | −1 MEDICINE, +5 REP local, **+20% hull patched** |
| | | | FUND THE WARD (100 CR) | +3 REP local, **+25% hull patched** (~50 CR repair value) |
| | | | NOT YOUR PROBLEM | nothing |
| 9 | REGISTRY ERROR *(oneshot)* | always | READ THE FILE | **LORE 4** |
| | | | DELETE IT | FLAG 3 (future hook) |
| 10 | DOCKSIDE WAGER | always | TAKE THE BET (150 CR) | 50%: +300 CR back (net **+150**) · 50%: net −150 |
| | | | DECLINE | nothing |
| 13 | CLAIM ADJUSTED *(arc finale, oneshot)* | took the retainer (FLAG 10) | DEMAND ANSWERS | **LORE 2**, FLAG 11 → unlocks #21 |
| | | | REPORT THE SHIP | +3 REP local, **+200 CR bounty voucher**, FLAG 11 |
| | | | SAY NOTHING | **+500 CR** hush money, FLAG 11 |

## Bar encounters (TRIG_BAR)

| # | Event | Offered when | Choice | Outcome |
|---|-------|--------------|--------|---------|
| 11 | A STRANGER IN GREY *(arc 1)* | not yet met (FLAG 8 clear) | HEAR THEM OUT | **LORE 0**, FLAG 8 → unlocks #12 |
| | | | BRUSH THEM OFF | nothing (they return another bar) |
| 12 | THE RETAINER *(arc 2)* | FLAG 8 set, FLAG 10 clear | TAKE THE RETAINER | **+300 CR**, LORE 1, FLAG 10 → unlocks #13 |
| | | | PRESS FOR ANSWERS | LORE 1, FLAG 10 (no pay) |
| | | | WALK AWAY | nothing (offer returns) |
| 14 | CARD GAME | always | SIT IN (100 CR) | 55%: +200 CR back (net **+100**) · 45%: net −100 |
| | | | JUST WATCH | nothing |
| 15 | THE NAVIGATOR | always | BUY HER A ROUND (40 CR) | **+5.0 LY fuel** (60 CR at the pump — a real deal now; was 50 CR for 18 CR of fuel, user-caught) |
| | | | NOT TONIGHT | nothing |
| 16 | OLD WAR STORY | always | LISTEN | +2 REP local |
| | | | BUY THE ROUND (25 CR) | +4 REP local, **+1 LIQUOR** ("for the road") |
| | | | SLIP AWAY | nothing |
| 17 | THE FIXER | rough gov + wanted | PAY THE FIXER (400 CR) | 80%: **record wiped clean** · 20%: scammed |
| | | | WALK ON | nothing |
| 26 | A FAMILIAR FACE *(oneshot)* | let the stowaway ride (FLAG 1) | TAKE THE WAGES | **+150 CR**, FLAG 13 |
| | | | KEEP THEM | +3 REP local, FLAG 13 |

## Derelict boardings (TRIG_SPACE — fly within 180 m of a cold hulk)

| # | Event | Offered when | Choice | Outcome |
|---|-------|--------------|--------|---------|
| 18 | COLD HULL | always | STRIP THE HOLD *(needs cargo space)* | 60%: **+2 random good**, 35% of those **+ salvaged hardware into the rack** · 25%: nothing · 15%: **lure — 2-ship ambush** |
| | | | PULL THE RECORDER | 55%: LORE 1 · 45%: +100 CR (sold logs) |
| | | | LEAVE IT BE | nothing |
| 19 | THE LAST POD *(oneshot)* | always | OPEN IT | **LORE 6**, FLAG 12 (future hook) |
| | | | SCAN AND SEAL | LORE 6 |
| | | | LEAVE - NOW | nothing |
| 20 | STILL WARM | threat ≥2 | GRAB AND GO *(needs cargo space)* | **+2 goods**; 40%: killers return — tier-2 ambush |
| | | | BURN AWAY | nothing |
| 21 | GREY PAINT *(oneshot)* | arc complete (FLAG 11) | BOARD HER | **LORE 7** |
| | | | KEEP CLEAR | nothing |

## Arrival hails (TRIG_ARRIVAL — on supercruise drop / jump-in)

| # | Event | Offered when | Choice | Outcome |
|---|-------|--------------|--------|---------|
| 22 | PATROL CHALLENGE | lawful + carrying illegal | HEAVE TO | lose illegal cargo, −150 CR |
| | | | RUN FOR IT | 50%: escape clean · 50%: LEGAL worsens |
| 23 | ROUTINE SWEEP | lawful + hold clean | TRANSMIT MANIFEST | +1 REP local |
| | | | IGNORE THEM | nothing |
| 24 | WAYLAID | threat ≥2 | PAY THE ESCORT FEE (150 CR) | safe passage |
| | | | REFUSE | 50%: they break off · 50%: **2-ship ambush** |
| 25 | DRIFTING TRADER | ≥2 cargo space | TAKE THE DEAL (120 CR) | **+2 random good** (typically worth more) |
| | | | FLY ON | nothing |

## Lore index (DATABASE)

0 THE COVER CHARGE · 1 THE ADJUSTERS · 2 CLAIM DENIED · 3 THE PROSPECTOR'S
CLAIM · 4 YOUR FILE · 5 DOCTRINE OF THE COVER · 6 THE OTHER POLICYHOLDER ·
7 RECALLED

## Story flags in play

1 stowaway rode (→ #26) · 2 preacher's token (unused yet) · 3 purged the
registry file (unused yet) · 8 met the stranger (→ #12) · 10 took the
retainer (→ #13) · 11 arc complete (→ #21) · 12 opened the pod (unused
yet) · 13 stowaway repaid

---

## Outcome visibility & tangible rewards — IMPLEMENTED 2026-06-12 (A+B)

- **A. The receipt.** The aftermath panel now lists every mechanical
  change: `+150 CR AT NEXT DOCK`, `-1.0 LY FUEL`, `+15% HULL`,
  `+3 REP DOMINION`, `+2 FOOD`, `RECORD: CLEANED`, `SALVAGED: AUTOCANNON`,
  `DATABASE UPDATED`, `2 HOSTILES INBOUND`. Diffed from real state, so
  clamps and confiscations report what actually happened.
- **B. New ops.** `OP_ITEM` drops salvaged hardware into the rack
  (combat-loot quality rolls; rack full → 100 CR scrap). `OP_LATER`
  defers credits to the next dock ("TRANSFER +150CR" toast), persisted
  in the save (v6). Buffs applied: #1, #8, #13, #16, #18 (bold above).
- #23 ROUTINE SWEEP stays rep-only — the receipt makes +1 REP visible,
  which was the actual complaint.


---

# THE POLICY — the main lore thread (added 2026-06-12)

Audit verdict that drove this: the old lore was a strong premise used as
wallpaper — 8 fragments all whispering "spooky" with no escalation, no
answers, no personal stakes. Restructured as three acts; each answers a
question and raises a worse one.

**Act 1 — THE ADJUSTER** *(existing #11/12/13)*: it exists and watches.
**Act 2 — THE BENEFICIARY** *(new, gated on act 1)*: it's about YOU.
| # | Event | Where | Reveals |
|---|---|---|---|
| 28 | THE ARCHIVIST | bar | Vessa (recurring): twelve unlapsed claims; yours is one. LORE 8 |
| 29 | THE LEDGER | bar | your own fees pay your own death claim. LORE 9 (option: fund a deeper trace, 200 CR) |
| 30 | THE BENEFICIARY | dock, oneshot | the payout was disbursed 40 years ago — AS you. "ONE (1) PILOT, CONTINUED." LORE 10 |

**Act 3 — THE TERMS** *(new, gated on act 2)*: the grey ships turn toward you.
| # | Event | Where | Reveals |
|---|---|---|---|
| 31 | RECALL NOTICE | arrival, oneshot | hailed by policy number: "your claim is under review". LORE 11 |
| 32 | THE AUDIT | derelict, oneshot | a grey wreck with your ship's bones; the recorder speaks in your voice. LORE 12 (option: strip it — grey-issue weapon) |
| 33 | THE TERMS | dock, climax, oneshot | READ: "you are not the policyholder. You are the payout." LORE 13 · DEMAND SETTLEMENT: +1000 CR, hull fully re-knit · REFUSE: +400 CR, the unread terms remain in force |

**Planted-flag payoffs** (every old dangling thread now lands):
| # | Event | Needs | Pays |
|---|---|---|---|
| 34 | THE COLLECTOR | preacher's token | sell 300 CR, or keep → LORE 14 |
| 35 | RESUBMITTED | purged the registry file | +200 CR "arrears", LORE 15 |
| 36 | THE OTHER YOU | opened the pod | the barkeep served "you" 3 weeks ago. LORE 16 |
| 38 | THE MEMORIAL | read your file | your hull on the wall, 40 years weathered, scratched: "PAID". LORE 17 |

**New standalone texture**: 37 UNION DUES (picket line: feed/cross/walk),
39 HOT CARGO (80 CR mystery crate: 55% goods / 30% sealed narcotics / 15%
empty), 40 THE CARTOGRAPHER (sell flight logs, haggle), 41 LAST ROUND
(retiring ace gifts his never-jammed gun — OP_ITEM), 42 GRAVE MARKER
(salute or rob a tomb-ship; kin may object), 43 TOLL GATE (pay the drone
or jam it), 44 PILGRIM CONVOY (escort the slow barges, tithe at next dock).

Totals: 44 events, 18 lore fragments, DATABASE screen now scrolls.
