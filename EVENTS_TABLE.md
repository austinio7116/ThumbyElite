# Indemnity Run — Events, War Contracts & Lore (review copy, 2026-06-12)

44 events, 18 lore fragments, 5 war-contract tiers. `CR` credits, `REP`
faction reputation, `LORE n` unlocks DATABASE entry n, `FLAG n` story flag.
REP scales mission rewards and market prices in that faction's space; the
aftermath receipt prints every mechanical change so nothing is invisible.

Frequencies: dock hails 35% of dockings · bar 60% per visit (one approach)
· arrival hails 14% per drop · derelict boardings always deal (the 24%
spawn was the roll). Outcomes are visit-stable (no save-scum rerolls).

---

## WAR CONTRACTS (loyalty-gated ladder)

Offered near faction fronts, **only at local rep 2+**; the ladder climbs
with standing (max tier = rep/5, so ELITE wars need rep 20+ ≈ 3 completed
contracts). War pay scales with loyalty at double the normal rep rate.
The whole enemy force spawns on arrival (no reinforcements); the zone is
won when it's dead — **any** killer counts, allies included. Ranks BLEND:
the tier is the grunt ceiling, stepping down in thirds, with exactly one
leader a rank above. Combatants show faction names on the HUD, not PIRATE.

| Contract | Pay | Enemy force | Allies | Board odds |
|---|---|---|---|---|
| VOIDRAT WAR | 2.0–2.8k | 4–5 ships, ranks 0/0/0 + rank-1 leader | 3 | 30% |
| ROGUE WAR | 3.2–4.4k | 5–6, ranks 1/0/0 + rank-2 leader | 3 | 25% |
| MARAUDER WAR | 5.0–6.8k | 5–6, ranks 2/1/0 + rank-3 leader | 4 | 20% |
| REAVER WAR | 7.5–10k | 6–7, ranks 3/2/1 + rank-4 leader | 4 | 15% |
| ELITE WAR | 19–22k | 6–7, ranks 4/3/2 + rank-4 leader | 5 | 10% |

---

## DOCK HAILS (35%)

| # | Event | Offered when | Choice | Outcome |
|---|---|---|---|---|
| 1 | DISTRESS HAIL | always | GIVE THEM FUEL *(fuel≥2)* | −1.0 LY, +4 REP, +150 CR at next dock |
| | | | DEMAND PAYMENT *(fuel≥2)* | 60%: −1.0 LY +200 CR · 40%: nothing |
| | | | TURN AWAY | −2 REP |
| 2 | CUSTOMS SWEEP | lawful + illegal cargo | SUBMIT | lose contraband, −150 CR |
| | | | BRIBE (300) | 70% keep all · 30% lose it + flagged |
| | | | DUMP IT | lose contraband, no fine |
| 3 | THE PROSPECTOR ¹ | always | BUY THE DATA (150) | 70% LORE 3 · 30% junk |
| 4 | STOWAWAY | always | HAND OVER / RIDE / FARE | +50+3REP / FLAG→#26 / +100 |
| 5 | BERTHING TAX | threat≥2 | PAY 200 / REFUSE / THREATEN *(wanted)* | peace / 45% 2-ship ambush / free |
| 6 | FIRE SALE | cargo space | BUY THE LOT (200) | +4 goods, 15% stolen→flagged |
| 7 | THE PREACHER ¹ | always | LISTEN / DONATE 25 | LORE 5 / + FLAG→#34 |
| 8 | CLINIC SHORTFALL | always | DONATE MEDS / FUND 100 | +5REP +20% hull / +3REP +25% hull |
| 9 | REGISTRY ERROR ¹ | always | READ / DELETE | LORE 4 + FLAG→#38 / FLAG→#35 |
| 10 | DOCKSIDE WAGER | always | BET 150 | 50/50 net ±150 |
| 13 | CLAIM ADJUSTED ¹ ᵃ¹ | retainer taken | DEMAND / REPORT / SILENCE | LORE 2 / +3REP +200 / +500 hush |
| 30 | THE BENEFICIARY ¹ ᵃ² | ledger seen | READ / BURN / PAY HER 200 | LORE 10 (all paths; PAY refunds +200 later) |
| 33 | THE TERMS ¹ ᵃ³ | audit done | READ / DEMAND / REFUSE | LORE 13 / LORE 13 +1000 CR + full hull / +400 CR, terms unread |
| 35 | RESUBMITTED ¹ | purged the file | TAKE / REFUSE | +200 CR + LORE 15 / LORE 15 |
| 37 | UNION DUES | always | FEED LINE 100 / WORK CARGO / WALK | +4REP / +200 CR −3REP / — |
| 38 | THE MEMORIAL ¹ | read your file | TOKEN 25 / LOOK | LORE 17 +2REP / LORE 17 |
| 39 | HOT CARGO | cargo space | BUY UNSEEN (80) | 55% +2 goods · 30% +2 NARCOTICS (!) · 15% empty |
| 27 | THE RECRUITER | frontline + rep 2+ | SIGN ON | war contract + 100 CR bonus |

## BAR ENCOUNTERS (60%, one approach per visit)

| # | Event | Offered when | Choice | Outcome |
|---|---|---|---|---|
| 11 | A STRANGER IN GREY ᵃ¹ | — | HEAR OUT / BRUSH OFF | LORE 0 + arc starts / returns later |
| 12 | THE RETAINER ᵃ¹ | met the stranger | TAKE / PRESS / WALK | +300 CR + LORE 1 / LORE 1 / offer returns |
| 14 | CARD GAME | always | SIT IN (100) | 55% net +100 · 45% net −100 |
| 15 | THE NAVIGATOR | always | ROUND (40) | +5.0 LY (≈60 CR at the pump) |
| 16 | OLD WAR STORY | always | LISTEN / ROUND 25 | +2REP / +4REP +1 LIQUOR |
| 17 | THE FIXER | rough + wanted | PAY 400 | 80% record wiped · 20% scammed |
| 26 | A FAMILIAR FACE ¹ | stowaway rode | WAGES / KEEP | +150 CR / +3REP |
| 28 | THE ARCHIVIST ᵃ² | act 1 done | HEAR HER OUT | LORE 8, meets Vessa |
| 29 | THE LEDGER ᵃ² | met Vessa | LOOK / FUND 200 / LATER | LORE 9 (FUND: +2REP too) |
| 34 | THE COLLECTOR ¹ | preacher's token | SELL / KEEP | +300 CR / LORE 14 |
| 36 | THE OTHER YOU ¹ | opened the pod | PRESS / LAUGH | LORE 16 |
| 40 | THE CARTOGRAPHER | always | SELL LOGS / HAGGLE / NO | +150 / 50%: +300 or 0 / — |
| 41 | LAST ROUND ¹ | always | ACCEPT GUN / DRINK | military-floor weapon to rack / +2REP |

## DERELICT BOARDINGS (fly within 180 m, LB-targetable, pale scanner blip)

| # | Event | Offered when | Choice | Outcome |
|---|---|---|---|---|
| 18 | COLD HULL | always | STRIP / RECORDER / LEAVE | 60% +2 goods (35% +hardware) · 15% lure ambush / 55% LORE 1 · 45% +100 / — |
| 19 | THE LAST POD ¹ | always | OPEN / SCAN / LEAVE | LORE 6 + FLAG→#36 / LORE 6 / — |
| 20 | STILL WARM | threat≥2 | GRAB / BURN AWAY | +2 goods, 40% tier-2 ambush / — |
| 21 | GREY PAINT ¹ ᵃ¹ | act 1 done | BOARD / KEEP CLEAR | LORE 7 / — |
| 32 | THE AUDIT ¹ ᵃ³ | recall acknowledged | LET IT SPEAK / STRIP | LORE 12 (STRIP: + grey-issue weapon) |
| 42 | GRAVE MARKER | always | SALUTE / LOOT | +3REP / +150 CR, 35% kin ambush |

## ARRIVAL HAILS (14%)

| # | Event | Offered when | Choice | Outcome |
|---|---|---|---|---|
| 22 | PATROL CHALLENGE | lawful + illegal | HEAVE TO / RUN | lose contraband −150 / 50% flagged |
| 23 | ROUTINE SWEEP | lawful + clean | TRANSMIT / IGNORE | +1REP / — |
| 24 | WAYLAID | threat≥2 | PAY 150 / REFUSE | safe / 50% 2-ship ambush |
| 25 | DRIFTING TRADER | cargo space | DEAL (120) | +2 goods |
| 31 | RECALL NOTICE ¹ ᵃ³ | act 2 done | ACKNOWLEDGE / RUN | LORE 11 (both — running is noted, in your voice) |
| 43 | TOLL GATE | lawful | PAY 50 / JAM | through / 50%: free · 50%: flagged −100 |
| 44 | PILGRIM CONVOY | always | ESCORT / FLY ON | +3REP +100 CR at next dock / — |

¹ = oneshot · ᵃ¹ᵃ²ᵃ³ = THE POLICY acts 1/2/3 (ordered by story flags)

---

# THE POLICY — campaign structure

Three acts; each answers a question and raises a worse one. Recurring
faces persist (the Adjuster, Vessa) — same portrait every encounter.

- **Act 1 — THE ADJUSTER** (#11→12→13, bar/bar/dock): *what is it?* An
  institution that insures the galaxy and pays out before the loss.
- **Act 2 — THE BENEFICIARY** (#28→29→30, bar/bar/dock): *what is it to
  me?* Your own fees pay your own death claim — settled 40 years ago,
  disbursed as "ONE (1) PILOT, CONTINUED", in your handwriting.
- **Act 3 — THE TERMS** (#31→32→33, arrival/derelict/dock): *what am I?*
  The grey ships turn toward you; a wreck with your ship's bones speaks
  in your voice; the Adjuster hands you one page.

Side threads that feed it: GREY PAINT (#21), the pod (#19→36), the token
(#7→34), the purged file (#9→35), the memorial (#9→38).

---

# LORE — all 18 fragments (full text, as shown in the DATABASE)

| # | Title | Text |
|---|---|---|
| 0 | THE COVER CHARGE | EVERY DOCKING FEE, EVERY FUEL TITHE, EVERY FINE - A FRACTION ROUTES TO AN ACCOUNT NO AUDIT CAN FOLLOW. THE LEDGERS JUST CALL IT 'COVER'. |
| 1 | THE ADJUSTERS | PILOTS SWEAR THEY'VE SEEN PLAIN GREY SHIPS HOLDING STATION AT WRECK SITES - LOGGED HOURS BEFORE THE WRECK OCCURRED. THE BARS CALL THEM ADJUSTERS. |
| 2 | CLAIM DENIED | A COLONY ONCE STOPPED PAYING. EVERY CHART NOW AGREES THERE WAS NEVER A COLONY THERE AT ALL. |
| 3 | THE PROSPECTOR'S CLAIM | DEEP-ROCK COORDINATES POINTING BEYOND THE RIM, COUNTERSIGNED: CLAIM RECORDED - THE INDEMNITY. NOTHING IS CHARTED OUT THERE. |
| 4 | YOUR FILE | AN UNCLAIMED-VESSEL RECORD: YOUR HULL, LOST WITH ALL HANDS FORTY YEARS AGO. REGISTERED OWNER: YOU. POLICY STATUS: CURRENT. |
| 5 | DOCTRINE OF THE COVER | THE DOCTRINE OF THE COVER: NOBODY SIGNED, NOBODY READS THE TERMS. PAYMENT IS TAKEN IN FUEL, IN HULL, IN YEARS. MISS ONE AND IT NOTICES. |
| 6 | THE OTHER POLICYHOLDER | A CRYOPOD MANIFEST: YOUR NAME, YOUR PRINTS, YOUR BLOOD TYPE - ABOARD A SHIP YOU NEVER CREWED. THE POD WAS WARM, AND RECENTLY VACATED. |
| 7 | RECALLED | GREY PLATES, NO REGISTRY, SYSTEMS WIPED CLEANER THAN ANY SALVAGER WORKS. THE ADJUSTERS DECOMMISSION THEIR OWN - AND LEAVE NOTHING TO CLAIM. |
| 8 | THE UNLAPSED | SOME POLICIES NEVER LAPSE. THE ARCHIVE LISTS CLAIMS PAID CENTURIES AGO WHOSE PREMIUMS STILL ARRIVE - ON TIME, IN FULL, FROM ACCOUNTS THAT DIED WITH THEIR HOLDERS. TWELVE ARE KNOWN. ONE IS YOURS. |
| 9 | THE PREMIUM | TRACE ANY PILOT'S FEES FAR ENOUGH UP AND A FRACTION VANISHES INTO THE COVER. TRACE YOURS AND THE FRACTION IS LARGER. YOU ARE PAYING A DEATH CLAIM. THE NAME ON THE CLAIM IS YOURS. |
| 10 | SETTLEMENT | DISBURSEMENT RECORD, FORTY YEARS SEALED. CLAIM: ONE PILOT, LOST WITH ALL HANDS. STATUS: SETTLED IN FULL. SETTLEMENT: ONE (1) PILOT, CONTINUED. THE HANDWRITING ON THE RELEASE IS YOURS. |
| 11 | UNDER REVIEW | THE GREY SHIPS NO LONGER ARRIVE BEFORE OTHER PEOPLE'S DISASTERS. THEY ARRIVE WHERE YOU ARE GOING TO BE, AND WAIT, AND FILE YOUR PUNCTUALITY APPROVINGLY. |
| 12 | THE AUDIT | AUDIT LOG, RECOVERED FROM A GREY WRECK WITH YOUR SHIP'S BONES: 'ASSET PERFORMING WITHIN PARAMETERS. CONTINUITY HOLDING. RE-ISSUE NOT YET REQUIRED.' THE VOICE ON THE LOG IS YOURS. |
| 13 | THE TERMS | THE INDEMNITY DOES NOT INSURE AGAINST LOSS. IT INSURES CONTINUITY. NOTHING COVERED IS EVER LOST - ONLY REPLACED, PERFECTLY, AND BILLED. YOU ARE NOT THE POLICYHOLDER. YOU ARE THE PAYOUT. |
| 14 | THE RECEIPT | THE TOKEN IS A METAL NOBODY CAN NAME AND IT IS ALWAYS COLD. ELEVEN OTHERS EXIST; SOMEONE COLLECTS THEM. HELD TO THE EAR IT MAKES A SOUND LIKE A LEDGER TURNING. |
| 15 | ARREARS | DELETE THE RECORD AND THE RECORD RETURNS, WITH INTEREST PAID FOR THE INCONVENIENCE. THE SYSTEM DOES NOT MIND BEING DOUBTED. IT DOES NOT MIND ANYTHING AT ALL. |
| 16 | CONTINUED | A PILOT WITH YOUR FACE DRANK AT THIS BAR THREE WEEKS BEFORE YOU ARRIVED. TIPPED WELL. PAID IN EXACT CHANGE. WORE GREY. SIGNED THE TAB: 'CONTINUED'. |
| 17 | THE WALL | YOUR HULL'S NAME IS ON THE MEMORIAL, FORTY YEARS WEATHERED, AMONG THE HONESTLY DEAD. SOMEONE RECENT HAS SCRATCHED ONE WORD BENEATH IT: 'PAID'. |
