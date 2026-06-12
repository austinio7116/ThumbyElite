/*
 * ThumbyElite — event pool + lore fragments (pure const data; the
 * interpreter lives in events.c). Conventions:
 *   - OP_BRANCH a b = a% chance to jump to op INDEX b of the same list.
 *   - Costs live on the Choice (deducted up-front; a wager's win op
 *     must therefore return stake + winnings).
 *   - $-tokens expand in body AND result texts ($N npc, $S system,
 *     $T station, $F faction, $G trade good).
 * Lore ids: 3 prospector's claim, 4 your file, 5 the cover.
 * Story flags: 1 stowaway rode along, 2 preacher's token, 3 purged file.
 */
#include "events.h"
#include <stddef.h>

/* --- 1 DISTRESS HAIL ---------------------------------------------------- */
static const Op e1_give[]   = { {OP_FUEL,-10,0}, {OP_REP,-1,4},
                                {OP_RESULT,0,0}, {OP_END,0,0} };
static const Op e1_demand[] = { {OP_BRANCH,60,3}, {OP_RESULT,1,0}, {OP_END,0,0},
                                {OP_FUEL,-10,0}, {OP_CR,8,0},
                                {OP_RESULT,2,0}, {OP_END,0,0} };
static const Op e1_turn[]   = { {OP_REP,-1,-2}, {OP_RESULT,3,0}, {OP_END,0,0} };
static const char *const e1_tx[] = {
    "THEY LIMP INTO $T BURNING YOUR GIFT. WORD OF IT TRAVELS.",
    "'NOTHING LEFT TO PAY WITH.' THE CHANNEL GOES QUIET.",
    "200 CR FOR A TANK OF HYDROGEN. FAIR IS FAIR.",
    "YOU CUT THE CHANNEL. THE BAY LIGHTS FEEL COLDER.",
};
static const Choice e1_ch[] = {
    { "GIVE THEM FUEL",  GATE_FUEL_SPARE, 0, e1_give },
    { "DEMAND PAYMENT",  GATE_FUEL_SPARE, 0, e1_demand },
    { "TURN AWAY",       0,               0, e1_turn },
};

/* --- 2 CUSTOMS SWEEP ---------------------------------------------------- */
static const Op e2_submit[] = { {OP_CONTRA,0,0}, {OP_CR,-6,0},
                                {OP_RESULT,0,0}, {OP_END,0,0} };
static const Op e2_bribe[]  = { {OP_BRANCH,70,5}, {OP_CONTRA,0,0},
                                {OP_LEGAL,1,0}, {OP_RESULT,2,0}, {OP_END,0,0},
                                {OP_RESULT,1,0}, {OP_END,0,0} };
static const Op e2_dump[]   = { {OP_CONTRA,0,0}, {OP_RESULT,3,0}, {OP_END,0,0} };
static const char *const e2_tx[] = {
    "THE HOLD IS STRIPPED AND A 150 CR FINE FILED. THE INSPECTOR ALMOST LOOKS SORRY.",
    "THE CREDITS VANISH INTO A GLOVE. 'PAPERWORK ERROR. HAPPENS.'",
    "WRONG OFFICER. THE HOLD IS SEIZED AND YOUR NAME GOES ON A LIST.",
    "CANISTERS TUMBLE OFF THE GANTRY INTO THE RECYCLER. PROOF GONE - PROFIT TOO.",
};
static const Choice e2_ch[] = {
    { "SUBMIT TO SEARCH",   0, 0,   e2_submit },
    { "BRIBE THE INSPECTOR",0, 300, e2_bribe },
    { "DUMP IT AND DENY",   0, 0,   e2_dump },
};

/* --- 3 THE PROSPECTOR'S CLAIM (oneshot, lore) --------------------------- */
static const Op e3_buy[]  = { {OP_BRANCH,70,3}, {OP_RESULT,1,0}, {OP_END,0,0},
                              {OP_LORE,3,0}, {OP_RESULT,0,0}, {OP_END,0,0} };
static const Op e3_wave[] = { {OP_RESULT,2,0}, {OP_END,0,0} };
static const char *const e3_tx[] = {
    "COORDINATES POINTING CLEAN OUT OF THE GALAXY. STAMPED ACROSS THEM: CLAIM RECORDED - THE INDEMNITY.",
    "JUNK TELEMETRY. THE MINER IS GONE BY THE TIME YOU LOOK UP.",
    "HE SHUFFLES OFF TO THE NEXT PILOT. SOMETHING IN HIS EYES STAYS WITH YOU.",
};
static const Choice e3_ch[] = {
    { "BUY THE DATA", 0, 150, e3_buy },
    { "WAVE HIM OFF", 0, 0,   e3_wave },
};

/* --- 4 STOWAWAY --------------------------------------------------------- */
static const Op e4_hand[] = { {OP_REP,-1,3}, {OP_CR,2,0},
                              {OP_RESULT,0,0}, {OP_END,0,0} };
static const Op e4_ride[] = { {OP_FLAG,1,0}, {OP_RESULT,1,0}, {OP_END,0,0} };
static const Op e4_fare[] = { {OP_CR,4,0}, {OP_RESULT,2,0}, {OP_END,0,0} };
static const char *const e4_tx[] = {
    "STATION SECURITY LOGS THE FARE-DODGER. A SMALL BOUNTY LANDS IN YOUR ACCOUNT.",
    "THEY SCRUB YOUR DECK PLATES TO A SHINE AND VANISH AT THE NEXT AIRLOCK. 'THE INDEMNITY KEEPS YOU,' THEY SAY. ODD BLESSING.",
    "A HUNDRED IN CRUMPLED CHITS. EVERYONE PAYS THEIR PREMIUM EVENTUALLY.",
};
static const Choice e4_ch[] = {
    { "HAND THEM OVER",    0, 0, e4_hand },
    { "LET THEM WORK PASSAGE", 0, 0, e4_ride },
    { "CHARGE THEM FARE",  0, 0, e4_fare },
};

/* --- 5 PIRATE TOLL ------------------------------------------------------ */
static const Op e5_pay[]    = { {OP_RESULT,0,0}, {OP_END,0,0} };
static const Op e5_refuse[] = { {OP_BRANCH,55,4}, {OP_AMBUSH,2,1},
                                {OP_RESULT,2,0}, {OP_END,0,0},
                                {OP_RESULT,1,0}, {OP_END,0,0} };
static const Op e5_threat[] = { {OP_RESULT,3,0}, {OP_END,0,0} };
static const char *const e5_tx[] = {
    "THE FACE GRINS. 'PLEASURE INSURING YOU.' THE CHANNEL DIES.",
    "SILENCE. MAYBE A BLUFF. MAYBE THEY'RE PATIENT.",
    "'WRONG ANSWER.' SCOPES PICK UP TWO CONTACTS TAKING POSITION OUTSIDE THE BAY.",
    "YOU QUOTE YOUR OWN BOUNTY AT THEM. THE FACE PALES AND CUTS THE LINK.",
};
static const Choice e5_ch[] = {
    { "PAY THE TOLL",      0,           200, e5_pay },
    { "REFUSE",            0,           0,   e5_refuse },
    { "THREATEN THEM BACK",GATE_WANTED, 0,   e5_threat },
};

/* --- 6 OVERSTOCK FIRE SALE ---------------------------------------------- */
static const Op e6_buy[]  = { {OP_CARGO,-1,4}, {OP_BRANCH,15,4},
                              {OP_RESULT,0,0}, {OP_END,0,0},
                              {OP_LEGAL,1,0}, {OP_RESULT,1,0}, {OP_END,0,0} };
static const Op e6_pass[] = { {OP_RESULT,2,0}, {OP_END,0,0} };
static const char *const e6_tx[] = {
    "FOUR CRATES, CLEAN PAPERS. A GENUINE BARGAIN FOR ONCE.",
    "THE CRATES SCAN AS STOLEN FREIGHT TWO SYSTEMS OVER. NOW IT'S YOUR PROBLEM.",
    "THE PALLET IS GONE WITHIN THE HOUR. SO IS THE TRADER.",
};
static const Choice e6_ch[] = {
    { "BUY THE LOT",        0, 200, e6_buy },
    { "TOO GOOD TO BE TRUE",0, 0,   e6_pass },
};

/* --- 7 PREACHER OF THE COVER (oneshot, lore) ----------------------------- */
static const Op e7_listen[] = { {OP_LORE,5,0}, {OP_RESULT,0,0}, {OP_END,0,0} };
static const Op e7_donate[] = { {OP_LORE,5,0}, {OP_FLAG,2,0},
                                {OP_RESULT,1,0}, {OP_END,0,0} };
static const Op e7_move[]   = { {OP_RESULT,2,0}, {OP_END,0,0} };
static const char *const e7_tx[] = {
    "'NOBODY SIGNED. NOBODY READS THE TERMS. BUT MISS A PAYMENT AND SEE HOW FAST IT NOTICES YOU.' THE CROWD WON'T MEET YOUR EYES.",
    "THE PREACHER PRESSES A COLD TOKEN INTO YOUR PALM. 'A RECEIPT. IT REMEMBERS WHO KEEPS CURRENT.'",
    "THE SERMON FOLLOWS YOU DOWN THE GANTRY UNTIL THE AIRLOCK CUTS IT OFF.",
};
static const Choice e7_ch[] = {
    { "LISTEN",     0, 0,  e7_listen },
    { "DONATE",     0, 25, e7_donate },
    { "MOVE ALONG", 0, 0,  e7_move },
};

/* --- 8 CLINIC SHORTFALL -------------------------------------------------- */
static const Op e8_meds[] = { {OP_CARGO,5,-1}, {OP_REP,-1,5},
                              {OP_RESULT,0,0}, {OP_END,0,0} };
static const Op e8_fund[] = { {OP_REP,-1,3}, {OP_RESULT,1,0}, {OP_END,0,0} };
static const Op e8_walk[] = { {OP_RESULT,2,0}, {OP_END,0,0} };
static const char *const e8_tx[] = {
    "YOUR CRATE OF MEDICINE DISAPPEARS INTO THE WARD. THE MEDIC GRIPS YOUR ARM. PEOPLE HERE WILL REMEMBER YOUR HULL.",
    "CREDITS BUY WHAT THE NEXT FREIGHTER CARRIES. THE TRIAGE QUEUE SHUFFLES FORWARD.",
    "YOU STEP AROUND THE STRETCHERS. THE SMELL OF ANTISEPTIC FOLLOWS YOU TO THE BAY.",
};
static const Choice e8_ch[] = {
    { "DONATE MEDICINE", GATE_HAS_MEDS, 0,   e8_meds },
    { "FUND THE WARD",   0,             100, e8_fund },
    { "NOT YOUR PROBLEM",0,             0,   e8_walk },
};

/* --- 9 THE REGISTRY FILE (oneshot, lore, no portrait) -------------------- */
static const Op e9_read[]   = { {OP_LORE,4,0}, {OP_RESULT,0,0}, {OP_END,0,0} };
static const Op e9_delete[] = { {OP_FLAG,3,0}, {OP_RESULT,1,0}, {OP_END,0,0} };
static const char *const e9_tx[] = {
    "A SHIP OF YOUR EXACT CLASS, LOST WITH ALL HANDS - FILED FORTY YEARS AGO. THE REGISTERED OWNER IS YOU. POLICY STATUS: CURRENT.",
    "YOU PURGE THE RECORD. AT THE EDGE OF THE SCREEN, FOR HALF A SECOND: RESUBMITTED.",
};
static const Choice e9_ch[] = {
    { "READ THE FILE", 0, 0, e9_read },
    { "DELETE IT",     0, 0, e9_delete },
};

/* --- 10 DOCKSIDE WAGER --------------------------------------------------- */
static const Op e10_bet[]  = { {OP_BRANCH,50,3}, {OP_RESULT,1,0}, {OP_END,0,0},
                               {OP_CR,12,0}, {OP_RESULT,0,0}, {OP_END,0,0} };
static const Op e10_pass[] = { {OP_RESULT,2,0}, {OP_END,0,0} };
static const char *const e10_tx[] = {
    "YOU SHAVE THE BEACON SO CLOSE THE PROXIMITY ALARM SINGS. THE RACER PAYS UP, LAUGHING.",
    "THEIR BURN IS FILTHY AND PERFECT. YOU PAY UP. THE DOCK CREW SAW NOTHING, THEY PROMISE.",
    "'SMART. NOBODY BEATS ME.' THE RACER SAUNTERS OFF TO FIND BRAVER MONEY.",
};
static const Choice e10_ch[] = {
    { "TAKE THE BET", 0, 150, e10_bet },
    { "DECLINE",      0, 0,   e10_pass },
};

/* --- pool ----------------------------------------------------------------*/
const Event k_events[] = {
    { 1, 12, 0,          NK_CIVILIAN, 0,
      "DISTRESS HAIL",
      "$N OF THE $S RUN HAILS YOU FROM A CRIPPLED FREIGHTER. 'TANKS DRY. FAMILY ABOARD. ANYTHING HELPS.'",
      e1_tx, e1_ch, 3 },
    { 2, 14, 0,          NK_OFFICIAL, GATE_LAWFUL | GATE_ILLEGAL,
      "CUSTOMS SWEEP",
      "$F CUSTOMS FLAGS YOUR MANIFEST. AN INSPECTOR IS ALREADY WALKING THE GANTRY TO YOUR BAY.",
      e2_tx, e2_ch, 3 },
    { 3, 8,  EV_ONESHOT, NK_DOCKHAND, 0,
      "THE PROSPECTOR",
      "AN OLD MINER BLOCKS YOUR PATH. 'FOUND SOMETHING IN THE DEEP ROCK. CHARTS SAY IT AIN'T THERE. 150 AND IT'S YOURS.'",
      e3_tx, e3_ch, 2 },
    { 4, 10, 0,          NK_CIVILIAN, 0,
      "STOWAWAY",
      "DOCK CREW FIND A STOWAWAY BEHIND YOUR CARGO RACKS - A KID, MAYBE SIXTEEN, CLUTCHING A TRANSIT PASS FOR $S.",
      e4_tx, e4_ch, 3 },
    { 5, 10, 0,          NK_PIRATE,   GATE_THREAT,
      "BERTHING TAX",
      "A SCARRED FACE FILLS YOUR COMM. 'THIS IS $N'S DOCK, FRIEND. BERTHING TAX IS 200. CHEAP, FOR PEACE OF MIND.'",
      e5_tx, e5_ch, 3 },
    { 6, 10, 0,          NK_DOCKHAND, GATE_CARGO_SPACE,
      "FIRE SALE",
      "A TRADER WAVES YOU OVER, PALLET STACKED HIGH. 'WAREHOUSE WANTS IT GONE TONIGHT. $G, 200 FLAT. STEAL AT TWICE THE PRICE.'",
      e6_tx, e6_ch, 2 },
    { 7, 8,  EV_ONESHOT, NK_MYSTIC,   0,
      "THE PREACHER",
      "A ROBED FIGURE PREACHES TO THE DOCK QUEUE. 'YOU ALL PAY. IN FUEL, IN HULL, IN YEARS. AND THE INDEMNITY COLLECTS.'",
      e7_tx, e7_ch, 3 },
    { 8, 9,  0,          NK_CIVILIAN, 0,
      "CLINIC SHORTFALL",
      "$T'S CLINIC IS TRIAGING IN THE CORRIDOR. A MEDIC FLAGS EVERY DOCKING PILOT: 'WE NEED SUPPLIES. ANYTHING.'",
      e8_tx, e8_ch, 3 },
    { 9, 6,  EV_ONESHOT, NK_NONE,     0,
      "REGISTRY ERROR",
      "THE HARBOURMASTER'S TERMINAL CHIMES AS YOU SIGN IN. AN UNCLAIMED-VESSEL RECORD HAS ATTACHED ITSELF TO YOUR DOCKET.",
      e9_tx, e9_ch, 2 },
    { 10, 9, 0,          NK_DOCKHAND, 0,
      "DOCKSIDE WAGER",
      "'THAT YOUR BIRD?' A RACER NODS AT YOUR BAY. 'BEACON AND BACK, 150 SAYS MINE'S FASTER. DOCK CAMS AS WITNESS.'",
      e10_tx, e10_ch, 2 },
};
const int k_n_events = (int)(sizeof k_events / sizeof k_events[0]);

/* --- lore fragments (codex lands in Phase 2; bits + reveal text now) ----- */
const char *const k_lore[] = {
    /* 0 */ "EVERY DOCKING FEE, EVERY FUEL TITHE, EVERY FINE - A FRACTION ROUTES TO AN ACCOUNT NO AUDIT CAN FOLLOW. THE LEDGERS JUST CALL IT 'COVER'.",
    /* 1 */ "PILOTS SWEAR THEY'VE SEEN PLAIN GREY SHIPS HOLDING STATION AT WRECK SITES - LOGGED HOURS BEFORE THE WRECK OCCURRED. THE BARS CALL THEM ADJUSTERS.",
    /* 2 */ "A COLONY ONCE STOPPED PAYING. EVERY CHART NOW AGREES THERE WAS NEVER A COLONY THERE AT ALL.",
    /* 3 */ "DEEP-ROCK COORDINATES POINTING BEYOND THE RIM, COUNTERSIGNED: CLAIM RECORDED - THE INDEMNITY. NOTHING IS CHARTED OUT THERE.",
    /* 4 */ "AN UNCLAIMED-VESSEL RECORD: YOUR HULL, LOST WITH ALL HANDS FORTY YEARS AGO. REGISTERED OWNER: YOU. POLICY STATUS: CURRENT.",
    /* 5 */ "THE DOCTRINE OF THE COVER: NOBODY SIGNED, NOBODY READS THE TERMS. PAYMENT IS TAKEN IN FUEL, IN HULL, IN YEARS. MISS ONE AND IT NOTICES.",
};
const int k_n_lore = (int)(sizeof k_lore / sizeof k_lore[0]);
