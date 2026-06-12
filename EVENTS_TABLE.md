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
| 1 | DISTRESS HAIL | always | GIVE THEM FUEL *(needs ≥2.0 LY)* | −1.0 LY fuel, +4 REP local |
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
| 8 | CLINIC SHORTFALL | always | DONATE MEDICINE *(needs MEDICINE)* | −1 MEDICINE, +5 REP local |
| | | | FUND THE WARD (100 CR) | +3 REP local |
| | | | NOT YOUR PROBLEM | nothing |
| 9 | REGISTRY ERROR *(oneshot)* | always | READ THE FILE | **LORE 4** |
| | | | DELETE IT | FLAG 3 (future hook) |
| 10 | DOCKSIDE WAGER | always | TAKE THE BET (150 CR) | 50%: +300 CR back (net **+150**) · 50%: net −150 |
| | | | DECLINE | nothing |
| 13 | CLAIM ADJUSTED *(arc finale, oneshot)* | took the retainer (FLAG 10) | DEMAND ANSWERS | **LORE 2**, FLAG 11 → unlocks #21 |
| | | | REPORT THE SHIP | +3 REP local, FLAG 11 |
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
| 15 | THE NAVIGATOR | always | BUY HER A ROUND (50 CR) | **+1.5 LY fuel** |
| | | | NOT TONIGHT | nothing |
| 16 | OLD WAR STORY | always | LISTEN | +2 REP local |
| | | | BUY THE ROUND (25 CR) | +4 REP local |
| | | | SLIP AWAY | nothing |
| 17 | THE FIXER | rough gov + wanted | PAY THE FIXER (400 CR) | 80%: **record wiped clean** · 20%: scammed |
| | | | WALK ON | nothing |
| 26 | A FAMILIAR FACE *(oneshot)* | let the stowaway ride (FLAG 1) | TAKE THE WAGES | **+150 CR**, FLAG 13 |
| | | | KEEP THEM | +3 REP local, FLAG 13 |

## Derelict boardings (TRIG_SPACE — fly within 180 m of a cold hulk)

| # | Event | Offered when | Choice | Outcome |
|---|-------|--------------|--------|---------|
| 18 | COLD HULL | always | STRIP THE HOLD *(needs cargo space)* | 60%: **+2 random good** · 25%: nothing · 15%: **lure — 2-ship ambush** |
| | | | PULL THE RECORDER | 55%: LORE 1 · 45%: +100 CR (sold logs) |
| | | | LEAVE IT BE | nothing |
| 19 | THE LAST POD *(oneshot)* | always | OPEN IT | **LORE 6**, FLAG 12 (future hook) |
| | | | SCAN AND SEAL | LORE 6 |
| | | | LEAVE - NOW | nothing |
| 20 | STILL WARM | threat ≥2 | GRAB AND GO *(needs cargo space)* | +1 good; 40%: **killers return — tier-2 ambush** |
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

## Known thin spots (user feedback 2026-06-12: "a lot appear to have no
real outcome — funding the hospital never does anything")

Rep-only outcomes are real but **invisible** — nothing tells the player
rep moved or what rep buys. Candidates and proposed fixes, pending
approval:

| Event/choice | Today | Proposal |
|---|---|---|
| #8 FUND THE WARD | +3 rep | + the clinic patches your ship: **free hull repair (+15%)**; meds donation also earns a **crate of MEDICINE-grade supplies back later** or a fee waiver |
| #16 OLD WAR STORY | rep only | the veteran's tip: **+1 LIQUOR** ("for the road") or a bounty-mark intel hint |
| #23 ROUTINE SWEEP | +1 rep | patrol escort: **shield tops up free** on next dock, or simply drop the event |
| #1 GIVE THEM FUEL | rep for real fuel | they remember you: small **deferred CR transfer** at next dock ("the $S run paid its debt") |
| #13 REPORT THE SHIP | rep only | + **bounty voucher CR** for the report |

Two engine-level options (can do both):
- **A. Show the deltas.** The aftermath panel lists what actually changed:
  `+4 REP COALITION`, `-1.0 LY`, `+2 FOOD`, `RECORD CLEAN`. One UI change
  makes every existing outcome legible — likely the highest-value fix.
- **B. Item rewards (new op `OP_ITEM`).** Events can drop a salvaged
  weapon/equipment instance into the rack (like combat loot canisters):
  derelict strips and big favours can pay in hardware, not just credits.
