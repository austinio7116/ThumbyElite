/*
 * ThumbyElite — event pool + lore fragments (pure const data; the
 * interpreter lives in events.c). Conventions:
 *   - OP_BRANCH a b = a% chance to jump to op INDEX b of the same list.
 *   - Costs live on the Choice (deducted up-front; a wager's win op
 *     must therefore return stake + winnings).
 *   - $-tokens expand in body AND result texts ($N npc, $S system,
 *     $T station, $F faction, $G trade good).
 * Lore ids: 0/1/2 the Adjuster arc, 3 prospector's claim, 4 your file,
 * 5 the cover, 6 the other policyholder, 7 recalled.
 * Story flags: 1 stowaway rode along, 2 preacher's token, 3 purged file,
 * 8 met the stranger, 10 took the retainer, 11 Adjuster arc complete,
 * 12 opened the last pod, 13 stowaway repaid.
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

/* ======================= THE ADJUSTER (arc, bar/dock) ====================
 * Recurring grey-coat NPC (fixed_npc 1). Flags: 8 met them, 10 retained,
 * 11 arc complete. Reveals lore 0 -> 1 -> 2 in order. */

/* --- 11 A STRANGER IN GREY (step 1, bar) -------------------------------- */
static const Op e11_hear[]  = { {OP_FLAG,8,0}, {OP_LORE,0,0},
                                {OP_RESULT,0,0}, {OP_END,0,0} };
static const Op e11_brush[] = { {OP_RESULT,1,0}, {OP_END,0,0} };
static const char *const e11_tx[] = {
    "'EVERY FEE YOU PAY, A FRACTION GOES UP. CALL IT COVER. YOU'LL WANT IT, WHERE YOU FLY.' THEY LEAVE NO GLASS, NO NAME.",
    "THEY NOD AS IF YOU'D SIGNED SOMETHING ANYWAY, AND GO.",
};
static const Choice e11_ch[] = {
    { "HEAR THEM OUT",  0, 0, e11_hear },
    { "BRUSH THEM OFF", 0, 0, e11_brush },
};

/* --- 12 THE RETAINER (step 2, bar) --------------------------------------- */
static const Op e12_take[]  = { {OP_CR,12,0}, {OP_FLAG,10,0}, {OP_LORE,1,0},
                                {OP_RESULT,0,0}, {OP_END,0,0} };
static const Op e12_press[] = { {OP_FLAG,10,0}, {OP_LORE,1,0},
                                {OP_RESULT,1,0}, {OP_END,0,0} };
static const Op e12_walk[]  = { {OP_RESULT,2,0}, {OP_END,0,0} };
static const char *const e12_tx[] = {
    "CREDITS ARRIVE FROM AN ACCOUNT THAT DOESN'T EXIST. THE WRECK THEY NAMED ISN'T ON ANY SCHEDULE.",
    "'WE DON'T CAUSE THE LOSSES. WE COVER THEM.' BY THE TIME YOU BLINK, THE STOOL IS EMPTY.",
    "YOU LEAVE YOUR DRINK. SOME JOBS SMELL LIKE BAD WEATHER.",
};
static const Choice e12_ch[] = {
    { "TAKE THE RETAINER", 0, 0, e12_take },
    { "PRESS FOR ANSWERS", 0, 0, e12_press },
    { "WALK AWAY",         0, 0, e12_walk },
};

/* --- 13 CLAIM ADJUSTED (step 3, dock, oneshot) ---------------------------- */
static const Op e13_demand[] = { {OP_LORE,2,0}, {OP_FLAG,11,0},
                                 {OP_RESULT,0,0}, {OP_END,0,0} };
static const Op e13_report[] = { {OP_REP,-1,3}, {OP_FLAG,11,0},
                                 {OP_RESULT,1,0}, {OP_END,0,0} };
static const Op e13_quiet[]  = { {OP_CR,20,0}, {OP_FLAG,11,0},
                                 {OP_RESULT,2,0}, {OP_END,0,0} };
static const char *const e13_tx[] = {
    "'CLAIM SETTLED IN FULL,' IS ALL THE GREY PILOT SAYS. 'THE COLONY CASE? BEFORE MY TIME.' YOU DIDN'T MENTION ANY COLONY.",
    "SECURITY BOARDS THE GREY SHIP AND FINDS NOBODY ABOARD. NO LOGS. NO SEATS.",
    "AN UNSIGNED TRANSFER LANDS IN YOUR ACCOUNT: 'FOR DISCRETION.' THE GREY SHIP IS GONE BY MORNING.",
};
static const Choice e13_ch[] = {
    { "DEMAND ANSWERS",  0, 0, e13_demand },
    { "REPORT THE SHIP", 0, 0, e13_report },
    { "SAY NOTHING",     0, 0, e13_quiet },
};

/* ======================= bar pool ======================================== */

/* --- 14 CARD GAME --------------------------------------------------------- */
static const Op e14_play[] = { {OP_BRANCH,55,3}, {OP_RESULT,1,0}, {OP_END,0,0},
                               {OP_CR,8,0}, {OP_RESULT,0,0}, {OP_END,0,0} };
static const Op e14_pass[] = { {OP_RESULT,2,0}, {OP_END,0,0} };
static const char *const e14_tx[] = {
    "YOUR LAST CARD LANDS LIKE A DOCKING CLAMP. THE TABLE PAYS, GRUMBLING.",
    "THE DEALER'S SMILE NEVER MOVES. NEITHER DO YOUR CREDITS - AWAY FROM YOU.",
    "YOU KEEP YOUR CREDITS IN YOUR POCKET AND YOUR BACK TO THE WALL.",
};
static const Choice e14_ch[] = {
    { "SIT IN",   0, 100, e14_play },
    { "JUST WATCH", 0, 0, e14_pass },
};

/* --- 15 THE NAVIGATOR'S TRICK --------------------------------------------- */
static const Op e15_buy[]  = { {OP_FUEL,15,0}, {OP_RESULT,0,0}, {OP_END,0,0} };
static const Op e15_pass[] = { {OP_RESULT,1,0}, {OP_END,0,0} };
static const char *const e15_tx[] = {
    "SHE SKETCHES A SCOOP APPROACH ON A NAPKIN THAT SAVES YOU REAL HYDROGEN. CHEAPEST FUEL YOU EVER BOUGHT.",
    "SHE SHRUGS AND SELLS THE NAPKIN TO THE NEXT BOOTH.",
};
static const Choice e15_ch[] = {
    { "BUY HER A ROUND", 0, 50, e15_buy },
    { "NOT TONIGHT",     0, 0,  e15_pass },
};

/* --- 16 OLD WAR STORY ------------------------------------------------------ */
static const Op e16_listen[] = { {OP_REP,-1,2}, {OP_RESULT,0,0}, {OP_END,0,0} };
static const Op e16_round[]  = { {OP_REP,-1,4}, {OP_RESULT,1,0}, {OP_END,0,0} };
static const Op e16_leave[]  = { {OP_RESULT,2,0}, {OP_END,0,0} };
static const char *const e16_tx[] = {
    "BY THE THIRD TELLING THE ODDS WERE WORSE AND THE ESCAPE NARROWER. THE LOCALS APPROVE OF YOUR PATIENCE.",
    "THE WHOLE CORNER DRINKS TO $F AND, SOMEHOW, TO YOU. WORD GETS AROUND.",
    "THE STORY FOLLOWS YOU OUT THE DOOR. IT WAS LOUDER THAN THE MUSIC.",
};
static const Choice e16_ch[] = {
    { "LISTEN",        0, 0,  e16_listen },
    { "BUY THE ROUND", 0, 25, e16_round },
    { "SLIP AWAY",     0, 0,  e16_leave },
};

/* --- 17 THE FIXER (rough space, wanted pilots) ------------------------------ */
static const Op e17_pay[]  = { {OP_BRANCH,80,4}, {OP_RESULT,1,0}, {OP_END,0,0},
                               {OP_END,0,0},
                               {OP_LEGAL,-2,0}, {OP_RESULT,0,0}, {OP_END,0,0} };
static const Op e17_pass[] = { {OP_RESULT,2,0}, {OP_END,0,0} };
static const char *const e17_tx[] = {
    "BY MORNING YOUR WARRANTS HAVE BEEN 'MISFILED'. THE FIXER DOESN'T SAY WHERE. YOU DON'T ASK.",
    "THE FIXER AND YOUR CREDITS LEAVE BY DIFFERENT DOORS. YOUR RECORD STAYS EXACTLY WHERE IT WAS.",
    "'SUIT YOURSELF. THE LAW'S MEMORY IS LONG, AND I'M CHEAP AT TWICE THE PRICE.'",
};
static const Choice e17_ch[] = {
    { "PAY THE FIXER", 0, 400, e17_pay },
    { "WALK ON",       0, 0,   e17_pass },
};

/* ======================= in-space: boarding a derelict ==================== */

/* --- 18 COLD HULL (repeatable) -------------------------------------------- */
static const Op e18_strip[] = { {OP_BRANCH,60,5}, {OP_BRANCH,38,8},
                                {OP_RESULT,1,0}, {OP_END,0,0}, {OP_END,0,0},
                                {OP_CARGO,-1,2}, {OP_RESULT,0,0}, {OP_END,0,0},
                                {OP_AMBUSH,2,1}, {OP_RESULT,2,0}, {OP_END,0,0} };
static const Op e18_rec[]   = { {OP_BRANCH,55,4}, {OP_CR,4,0}, {OP_RESULT,4,0},
                                {OP_END,0,0},
                                {OP_LORE,1,0}, {OP_RESULT,3,0}, {OP_END,0,0} };
static const Op e18_leave[] = { {OP_RESULT,5,0}, {OP_END,0,0} };
static const char *const e18_tx[] = {
    "TWO CRATES COME FREE OF THE WRECKAGE, SEALS INTACT. THE REST IS SLAG AND SILENCE.",
    "THE HOLD WAS STRIPPED LONG BEFORE YOU GOT HERE. EVEN THE DECK PLATING IS GONE.",
    "THE 'WRECK' LIGHTS UP - A LURE. TWO CONTACTS BURN IN FROM THE SHADOW OF THE DEBRIS.",
    "THE RECORDER'S LAST ENTRY: A PLAIN GREY SHIP HOLDING STATION OFF THE BOW. LOGGED SIX HOURS BEFORE THE HULL BREACH.",
    "THE LOGS ARE MUNDANE - BUT COMPLETE, AND SALVAGE REGISTRIES PAY FOR CLOSURE. 100 CR.",
    "YOU LEAVE THE DEAD THEIR QUIET. THE HULK TUMBLES ON BEHIND YOU.",
};
static const Choice e18_ch[] = {
    { "STRIP THE HOLD",    GATE_CARGO_SPACE, 0, e18_strip },
    { "PULL THE RECORDER", 0,                0, e18_rec },
    { "LEAVE IT BE",       0,                0, e18_leave },
};

/* --- 19 THE LAST POD (oneshot) --------------------------------------------- */
static const Op e19_open[] = { {OP_LORE,6,0}, {OP_FLAG,12,0},
                               {OP_RESULT,0,0}, {OP_END,0,0} };
static const Op e19_scan[] = { {OP_LORE,6,0}, {OP_RESULT,1,0}, {OP_END,0,0} };
static const Op e19_go[]   = { {OP_RESULT,2,0}, {OP_END,0,0} };
static const char *const e19_tx[] = {
    "THE POD IS EMPTY. WARM. THE MANIFEST LISTS ONE OCCUPANT: YOUR NAME, YOUR PRINTS, YOUR BLOOD TYPE. VACATED, IT SAYS. RECENTLY.",
    "THE SCAN COMPLETES AND THE POD SEALS ITSELF. THE MANIFEST UPLOADS SOMEWHERE YOU CAN'T TRACE - MARKED 'CLAIM CONTINUED'.",
    "YOU BURN AWAY AND DON'T LOOK BACK. THE CYCLING LIGHT OF THE POD BLINKS IN YOUR MIRRORS FOR LONGER THAN PHYSICS ALLOWS.",
};
static const Choice e19_ch[] = {
    { "OPEN IT",       0, 0, e19_open },
    { "SCAN AND SEAL", 0, 0, e19_scan },
    { "LEAVE - NOW",   0, 0, e19_go },
};

/* --- 20 STILL WARM (dangerous space) ---------------------------------------- */
static const Op e20_grab[] = { {OP_CARGO,-1,1}, {OP_BRANCH,40,4},
                               {OP_RESULT,0,0}, {OP_END,0,0},
                               {OP_AMBUSH,2,2}, {OP_RESULT,1,0}, {OP_END,0,0} };
static const Op e20_burn[] = { {OP_RESULT,2,0}, {OP_END,0,0} };
static const char *const e20_tx[] = {
    "ONE CRATE, QUICK AND QUIET. THE HULL PINGS AS IT COOLS - THIS KILL IS MINUTES OLD.",
    "A CRATE ABOARD AND THE KILLERS COME BACK FOR THEIR LEAVINGS. THEY DON'T LOOK PLEASED TO SHARE.",
    "FRESH WRECK, NO BODIES, GUNS STILL COOLING SOMEWHERE CLOSE. YOU WERE NEVER HERE.",
};
static const Choice e20_ch[] = {
    { "GRAB AND GO", GATE_CARGO_SPACE, 0, e20_grab },
    { "BURN AWAY",   0,                0, e20_burn },
};

/* --- 21 GREY PAINT (post-arc, oneshot) --------------------------------------- */
static const Op e21_board[] = { {OP_LORE,7,0}, {OP_RESULT,0,0}, {OP_END,0,0} };
static const Op e21_leave[] = { {OP_RESULT,1,0}, {OP_END,0,0} };
static const char *const e21_tx[] = {
    "GREY PLATES. NO REGISTRY. SYSTEMS WIPED CLEANER THAN ANY SALVAGER WORKS. THE ADJUSTERS DECOMMISSION THEIR OWN - AND LEAVE NOTHING TO CLAIM.",
    "YOU KNOW THAT PAINT. YOU KEEP YOUR DISTANCE, AND THE DEAD GREY HULL KEEPS ITS SECRETS.",
};
static const Choice e21_ch[] = {
    { "BOARD HER",     0, 0, e21_board },
    { "KEEP CLEAR",    0, 0, e21_leave },
};

/* ======================= arrival hails ====================================== */

/* --- 22 PATROL CHALLENGE (lawful, carrying contraband) ----------------------- */
static const Op e22_comply[] = { {OP_CONTRA,0,0}, {OP_CR,-6,0},
                                 {OP_RESULT,0,0}, {OP_END,0,0} };
static const Op e22_run[]    = { {OP_BRANCH,50,3}, {OP_LEGAL,1,0},
                                 {OP_RESULT,2,0}, {OP_END,0,0},
                                 {OP_RESULT,1,0}, {OP_END,0,0} };
static const char *const e22_tx[] = {
    "THE PATROL TAKES THE CARGO AND 150 CR IN FINES. 'WISE CHOICE. SAFE LANES.'",
    "YOU CUT THRUST, DRIFT DARK, AND THE PATROL'S SWEEP SLIDES PAST. THIS TIME.",
    "THE SWEEP PINS YOUR TRANSPONDER MID-BURN. YOUR NAME GOES ON THE WIRE.",
};
static const Choice e22_ch[] = {
    { "HEAVE TO",   0, 0, e22_comply },
    { "RUN FOR IT", 0, 0, e22_run },
};

/* --- 23 ROUTINE SWEEP (lawful, clean) ----------------------------------------- */
static const Op e23_tx_ops[] = { {OP_REP,-1,1}, {OP_RESULT,0,0}, {OP_END,0,0} };
static const Op e23_ig[]     = { {OP_RESULT,1,0}, {OP_END,0,0} };
static const char *const e23_tx[] = {
    "'MANIFEST CLEAN. APPRECIATED, PILOT.' THE PATROL LOGS YOU AS A FRIENDLY AND PEELS AWAY.",
    "YOU LET THE REQUEST EXPIRE. THE PATROL SHADOWS YOU A WHILE LONGER THAN FEELS POLITE.",
};
static const Choice e23_ch[] = {
    { "TRANSMIT MANIFEST", 0, 0, e23_tx_ops },
    { "IGNORE THEM",       0, 0, e23_ig },
};

/* --- 24 WAYLAID (dangerous space) ----------------------------------------------*/
static const Op e24_pay[]    = { {OP_RESULT,0,0}, {OP_END,0,0} };
static const Op e24_refuse[] = { {OP_BRANCH,50,4}, {OP_AMBUSH,2,1},
                                 {OP_RESULT,2,0}, {OP_END,0,0},
                                 {OP_RESULT,1,0}, {OP_END,0,0} };
static const char *const e24_tx[] = {
    "THE ESCORT FEE CLEARS AND THE CONTACTS FADE OFF YOUR SCOPE. CHEAPER THAN A HULL.",
    "A LONG SILENCE, THEN THE CONTACTS BREAK OFF. SOMEBODY ELSE'S SCOPE LOOKED SOFTER.",
    "'YOUR FUNERAL.' THE CONTACTS TURN IN, BURNING HARD.",
};
static const Choice e24_ch[] = {
    { "PAY THE ESCORT FEE", 0, 150, e24_pay },
    { "REFUSE",             0, 0,   e24_refuse },
};

/* --- 25 DRIFTING TRADER ----------------------------------------------------------*/
static const Op e25_buy[]  = { {OP_CARGO,-1,2}, {OP_RESULT,0,0}, {OP_END,0,0} };
static const Op e25_pass[] = { {OP_RESULT,1,0}, {OP_END,0,0} };
static const char *const e25_tx[] = {
    "TWO CRATES OF $G CROSS ON A TETHER LINE, NO PAPERWORK, NO QUESTIONS. EVERYONE WAVES.",
    "THE TRADER SHRUGS ACROSS THE VOID AND DRIFTS BACK INTO THE DARK LANE TRAFFIC NEVER USES.",
};
static const Choice e25_ch[] = {
    { "TAKE THE DEAL", GATE_CARGO_SPACE, 120, e25_buy },
    { "FLY ON",        0,                0,   e25_pass },
};

/* ======================= continuity ============================================ */

/* --- 26 A FAMILIAR FACE (stowaway repaid, bar, oneshot) -------------------------- */
static const Op e26_take[] = { {OP_CR,6,0}, {OP_FLAG,13,0},
                               {OP_RESULT,0,0}, {OP_END,0,0} };
static const Op e26_wave[] = { {OP_REP,-1,3}, {OP_FLAG,13,0},
                               {OP_RESULT,1,0}, {OP_END,0,0} };
static const char *const e26_tx[] = {
    "'FIRST WAGES.' THE KID FROM YOUR CARGO RACKS - TALLER NOW, CREW PATCHES ON THE SLEEVE - COUNTS OUT 150 CR AND WON'T TAKE NO.",
    "YOU WAVE THE CREDITS OFF. THE KID GRINS AND BUYS THE WHOLE BAR A ROUND IN YOUR NAME INSTEAD.",
};
static const Choice e26_ch[] = {
    { "TAKE THE WAGES", 0, 0, e26_take },
    { "KEEP THEM",      0, 0, e26_wave },
};

/* --- pool ----------------------------------------------------------------*/
const Event k_events[] = {
    { .id = 1, .weight = 12, .npc_kind = NK_CIVILIAN, .trig = TRIG_DOCK,
      .title = "DISTRESS HAIL",
      .body = "$N OF THE $S RUN HAILS YOU FROM A CRIPPLED FREIGHTER. 'TANKS DRY. FAMILY ABOARD. ANYTHING HELPS.'",
      .texts = e1_tx, .choices = e1_ch, .n_choices = 3 },
    { .id = 2, .weight = 14, .npc_kind = NK_OFFICIAL, .trig = TRIG_DOCK,
      .gate = GATE_LAWFUL | GATE_ILLEGAL,
      .title = "CUSTOMS SWEEP",
      .body = "$F CUSTOMS FLAGS YOUR MANIFEST. AN INSPECTOR IS ALREADY WALKING THE GANTRY TO YOUR BAY.",
      .texts = e2_tx, .choices = e2_ch, .n_choices = 3 },
    { .id = 3, .weight = 8, .flags = EV_ONESHOT, .npc_kind = NK_DOCKHAND,
      .trig = TRIG_DOCK,
      .title = "THE PROSPECTOR",
      .body = "AN OLD MINER BLOCKS YOUR PATH. 'FOUND SOMETHING IN THE DEEP ROCK. CHARTS SAY IT AIN'T THERE. 150 AND IT'S YOURS.'",
      .texts = e3_tx, .choices = e3_ch, .n_choices = 2 },
    { .id = 4, .weight = 10, .npc_kind = NK_CIVILIAN, .trig = TRIG_DOCK,
      .title = "STOWAWAY",
      .body = "DOCK CREW FIND A STOWAWAY BEHIND YOUR CARGO RACKS - A KID, MAYBE SIXTEEN, CLUTCHING A TRANSIT PASS FOR $S.",
      .texts = e4_tx, .choices = e4_ch, .n_choices = 3 },
    { .id = 5, .weight = 10, .npc_kind = NK_PIRATE, .trig = TRIG_DOCK,
      .gate = GATE_THREAT,
      .title = "BERTHING TAX",
      .body = "A SCARRED FACE FILLS YOUR COMM. 'THIS IS $N'S DOCK, FRIEND. BERTHING TAX IS 200. CHEAP, FOR PEACE OF MIND.'",
      .texts = e5_tx, .choices = e5_ch, .n_choices = 3 },
    { .id = 6, .weight = 10, .npc_kind = NK_DOCKHAND, .trig = TRIG_DOCK,
      .gate = GATE_CARGO_SPACE,
      .title = "FIRE SALE",
      .body = "A TRADER WAVES YOU OVER, PALLET STACKED HIGH. 'WAREHOUSE WANTS IT GONE TONIGHT. $G, 200 FLAT. STEAL AT TWICE THE PRICE.'",
      .texts = e6_tx, .choices = e6_ch, .n_choices = 2 },
    { .id = 7, .weight = 8, .flags = EV_ONESHOT, .npc_kind = NK_MYSTIC,
      .trig = TRIG_DOCK,
      .title = "THE PREACHER",
      .body = "A ROBED FIGURE PREACHES TO THE DOCK QUEUE. 'YOU ALL PAY. IN FUEL, IN HULL, IN YEARS. AND THE INDEMNITY COLLECTS.'",
      .texts = e7_tx, .choices = e7_ch, .n_choices = 3 },
    { .id = 8, .weight = 9, .npc_kind = NK_CIVILIAN, .trig = TRIG_DOCK,
      .title = "CLINIC SHORTFALL",
      .body = "$T'S CLINIC IS TRIAGING IN THE CORRIDOR. A MEDIC FLAGS EVERY DOCKING PILOT: 'WE NEED SUPPLIES. ANYTHING.'",
      .texts = e8_tx, .choices = e8_ch, .n_choices = 3 },
    { .id = 9, .weight = 6, .flags = EV_ONESHOT, .npc_kind = NK_NONE,
      .trig = TRIG_DOCK,
      .title = "REGISTRY ERROR",
      .body = "THE HARBOURMASTER'S TERMINAL CHIMES AS YOU SIGN IN. AN UNCLAIMED-VESSEL RECORD HAS ATTACHED ITSELF TO YOUR DOCKET.",
      .texts = e9_tx, .choices = e9_ch, .n_choices = 2 },
    { .id = 10, .weight = 9, .npc_kind = NK_DOCKHAND, .trig = TRIG_DOCK,
      .title = "DOCKSIDE WAGER",
      .body = "'THAT YOUR BIRD?' A RACER NODS AT YOUR BAY. 'BEACON AND BACK, 150 SAYS MINE'S FASTER. DOCK CAMS AS WITNESS.'",
      .texts = e10_tx, .choices = e10_ch, .n_choices = 2 },

    /* the Adjuster arc */
    { .id = 11, .weight = 7, .npc_kind = NK_OFFICIAL, .trig = TRIG_BAR,
      .not_flag = 1 + 8, .fixed_npc = 1,
      .title = "A STRANGER IN GREY",
      .body = "A FIGURE IN PLAIN GREY TAKES THE STOOL BESIDE YOU AND ORDERS NOTHING. 'YOU FLY THE $S LANES. WE LOG GOOD CARE. KEEP CURRENT.'",
      .texts = e11_tx, .choices = e11_ch, .n_choices = 2 },
    { .id = 12, .weight = 9, .npc_kind = NK_OFFICIAL, .trig = TRIG_BAR,
      .need_flag = 1 + 8, .not_flag = 1 + 10, .fixed_npc = 1,
      .title = "THE RETAINER",
      .body = "THE SAME GREY COAT. THE SAME STOOL. 'A FREIGHTER WILL BREAK UP OFF $T. WHEN IT DOES - NOT IF - TELL US HOW MANY ESCAPE PODS YOU COUNT.'",
      .texts = e12_tx, .choices = e12_ch, .n_choices = 3 },
    { .id = 13, .weight = 14, .flags = EV_ONESHOT, .npc_kind = NK_OFFICIAL,
      .trig = TRIG_DOCK, .need_flag = 1 + 10, .fixed_npc = 1,
      .title = "CLAIM ADJUSTED",
      .body = "THE NEWS FEED: FREIGHTER LOST OFF $T, ALL HANDS. BERTHED TWO PADS DOWN SITS A PLAIN GREY SHIP THAT DOCKED YESTERDAY.",
      .texts = e13_tx, .choices = e13_ch, .n_choices = 3 },

    /* bar pool */
    { .id = 14, .weight = 11, .npc_kind = NK_DOCKHAND, .trig = TRIG_BAR,
      .title = "CARD GAME",
      .body = "A BACK-TABLE GAME HAS AN EMPTY CHAIR AND A POT WORTH LOOKING AT. THE DEALER RAPS THE TABLE. 'HUNDRED TO SIT.'",
      .texts = e14_tx, .choices = e14_ch, .n_choices = 2 },
    { .id = 15, .weight = 10, .npc_kind = NK_CIVILIAN, .trig = TRIG_BAR,
      .title = "THE NAVIGATOR",
      .body = "AN EX-SURVEY NAVIGATOR NURSES AN EMPTY GLASS. 'I KNOW A SCOOP LINE THROUGH $S THAT BEATS THE BOOK BY HALF A TANK.'",
      .texts = e15_tx, .choices = e15_ch, .n_choices = 2 },
    { .id = 16, .weight = 10, .npc_kind = NK_CIVILIAN, .trig = TRIG_BAR,
      .title = "OLD WAR STORY",
      .body = "A VETERAN OF THE $F LINES HOLDS COURT BY THE WINDOW, REFIGHTING A BATTLE EVERYONE ELSE HAS HEARD TWICE.",
      .texts = e16_tx, .choices = e16_ch, .n_choices = 3 },
    { .id = 17, .weight = 12, .npc_kind = NK_PIRATE, .trig = TRIG_BAR,
      .gate = GATE_ROUGH | GATE_WANTED,
      .title = "THE FIXER",
      .body = "A QUIET BOOTH, A QUIETER VOICE. 'I HEAR THE LAW HAS YOUR NAME SPELLED RIGHT FOR ONCE. FOUR HUNDRED MAKES IT A TYPO.'",
      .texts = e17_tx, .choices = e17_ch, .n_choices = 2 },

    /* in-space: derelict boarding */
    { .id = 18, .weight = 14, .npc_kind = NK_NONE, .trig = TRIG_SPACE,
      .title = "COLD HULL",
      .body = "THE AIRLOCK GIVES. INSIDE: VACUUM-FROZEN CORRIDORS, A HOLD HALF-SPILLED, AND A FLIGHT DECK NOBODY WALKED OUT OF.",
      .texts = e18_tx, .choices = e18_ch, .n_choices = 3 },
    { .id = 19, .weight = 7, .flags = EV_ONESHOT, .npc_kind = NK_NONE,
      .trig = TRIG_SPACE,
      .title = "THE LAST POD",
      .body = "EVERY SYSTEM ABOARD IS DEAD EXCEPT ONE: A SINGLE CRYOPOD, STILL CYCLING, ITS STATUS LIGHT PATIENT AS A HEARTBEAT.",
      .texts = e19_tx, .choices = e19_ch, .n_choices = 3 },
    { .id = 20, .weight = 11, .npc_kind = NK_NONE, .trig = TRIG_SPACE,
      .gate = GATE_THREAT,
      .title = "STILL WARM",
      .body = "SCORCH PATTERNS STILL GLOWING, ATMOSPHERE STILL VENTING. THIS WRECK ISN'T A RUIN - IT'S A CRIME SCENE, AND IT'S FRESH.",
      .texts = e20_tx, .choices = e20_ch, .n_choices = 2 },
    { .id = 21, .weight = 12, .flags = EV_ONESHOT, .npc_kind = NK_NONE,
      .trig = TRIG_SPACE, .need_flag = 1 + 11,
      .title = "GREY PAINT",
      .body = "NO DISTRESS CODE. NO REGISTRY. THE HULL IS PAINTED A PLAIN, PATIENT GREY YOU'VE SEEN BERTHED TWO PADS DOWN.",
      .texts = e21_tx, .choices = e21_ch, .n_choices = 2 },

    /* arrival hails */
    { .id = 22, .weight = 12, .npc_kind = NK_OFFICIAL, .trig = TRIG_ARRIVAL,
      .gate = GATE_LAWFUL | GATE_ILLEGAL,
      .title = "PATROL CHALLENGE",
      .body = "A $F PATROL WING SLIDES ONTO YOUR SCOPE BEFORE THE DROP-WAKE FADES. 'CUT THRUST. CARGO SCAN. THIS IS NOT A REQUEST.'",
      .texts = e22_tx, .choices = e22_ch, .n_choices = 2 },
    { .id = 23, .weight = 8, .npc_kind = NK_OFFICIAL, .trig = TRIG_ARRIVAL,
      .gate = GATE_LAWFUL | GATE_NO_ILLEGAL,
      .title = "ROUTINE SWEEP",
      .body = "'$F TRAFFIC CONTROL. ROUTINE SWEEP. TRANSMIT YOUR MANIFEST AND ENJOY THE LANES, PILOT.'",
      .texts = e23_tx, .choices = e23_ch, .n_choices = 2 },
    { .id = 24, .weight = 12, .npc_kind = NK_PIRATE, .trig = TRIG_ARRIVAL,
      .gate = GATE_THREAT,
      .title = "WAYLAID",
      .body = "THREE CONTACTS BRACKET YOUR DROP POINT. '$N'S LANE, FRIEND. ESCORT FEE IS 150 - AND OUT HERE, EVERYONE WANTS AN ESCORT.'",
      .texts = e24_tx, .choices = e24_ch, .n_choices = 2 },
    { .id = 25, .weight = 9, .npc_kind = NK_CIVILIAN, .trig = TRIG_ARRIVAL,
      .title = "DRIFTING TRADER",
      .body = "A FREIGHTER RUNNING DARK FLASHES ITS BAY LIGHTS. '$G, TWO CRATES, 120 FLAT. NO DOCKS, NO DUTIES, NO NAMES.'",
      .texts = e25_tx, .choices = e25_ch, .n_choices = 2 },

    /* continuity */
    { .id = 26, .weight = 12, .flags = EV_ONESHOT, .npc_kind = NK_CIVILIAN,
      .trig = TRIG_BAR, .need_flag = 1 + 1,
      .title = "A FAMILIAR FACE",
      .body = "SOMEONE PUSHES THROUGH THE CROWD TOWARD YOUR TABLE - AND YOU KNOW THE FACE, THOUGH IT LAST LOOKED OUT FROM BEHIND YOUR CARGO RACKS.",
      .texts = e26_tx, .choices = e26_ch, .n_choices = 2 },
};
const int k_n_events = (int)(sizeof k_events / sizeof k_events[0]);

/* --- lore fragments (read again in the station DATABASE) ----------------- */
const Lore k_lore[] = {
    /* 0 */ { "THE COVER CHARGE",
      "EVERY DOCKING FEE, EVERY FUEL TITHE, EVERY FINE - A FRACTION ROUTES TO AN ACCOUNT NO AUDIT CAN FOLLOW. THE LEDGERS JUST CALL IT 'COVER'." },
    /* 1 */ { "THE ADJUSTERS",
      "PILOTS SWEAR THEY'VE SEEN PLAIN GREY SHIPS HOLDING STATION AT WRECK SITES - LOGGED HOURS BEFORE THE WRECK OCCURRED. THE BARS CALL THEM ADJUSTERS." },
    /* 2 */ { "CLAIM DENIED",
      "A COLONY ONCE STOPPED PAYING. EVERY CHART NOW AGREES THERE WAS NEVER A COLONY THERE AT ALL." },
    /* 3 */ { "THE PROSPECTOR'S CLAIM",
      "DEEP-ROCK COORDINATES POINTING BEYOND THE RIM, COUNTERSIGNED: CLAIM RECORDED - THE INDEMNITY. NOTHING IS CHARTED OUT THERE." },
    /* 4 */ { "YOUR FILE",
      "AN UNCLAIMED-VESSEL RECORD: YOUR HULL, LOST WITH ALL HANDS FORTY YEARS AGO. REGISTERED OWNER: YOU. POLICY STATUS: CURRENT." },
    /* 5 */ { "DOCTRINE OF THE COVER",
      "THE DOCTRINE OF THE COVER: NOBODY SIGNED, NOBODY READS THE TERMS. PAYMENT IS TAKEN IN FUEL, IN HULL, IN YEARS. MISS ONE AND IT NOTICES." },
    /* 6 */ { "THE OTHER POLICYHOLDER",
      "A CRYOPOD MANIFEST: YOUR NAME, YOUR PRINTS, YOUR BLOOD TYPE - ABOARD A SHIP YOU NEVER CREWED. THE POD WAS WARM, AND RECENTLY VACATED." },
    /* 7 */ { "RECALLED",
      "GREY PLATES, NO REGISTRY, SYSTEMS WIPED CLEANER THAN ANY SALVAGER WORKS. THE ADJUSTERS DECOMMISSION THEIR OWN - AND LEAVE NOTHING TO CLAIM." },
};
const int k_n_lore = (int)(sizeof k_lore / sizeof k_lore[0]);
