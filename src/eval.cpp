#include "rodent.h"
#include "eval.h"

static const int passed_bonus_mg[2][8] = {
  { 0, 12, 12, 30, 50, 80, 130, 0 },
  { 0, 120, 80, 50, 30, 12, 12, 0 }
};

const int passed_bonus_eg[2][8] = {
  { 0, 16, 16, 39, 65, 104, 156, 0 },
  { 0, 156, 104, 65, 39, 16, 16, 0 }
};

static const int att_weight[16] = {
  0, 0, 128, 192, 224, 240, 248, 252, 254, 255, 256, 256, 256, 256, 256, 256,
};

#define REL_SQ(sq,cl)   ( sq ^ (cl * 56) )

static const U64 bbQSCastle[2] = { SqBb(A1) | SqBb(B1) | SqBb(C1) | SqBb(A2) | SqBb(B2) | SqBb(C2),
                                   SqBb(A8) | SqBb(B8) | SqBb(C8) | SqBb(A7) | SqBb(B7) | SqBb(C7)
                                 };
static const U64 bbKSCastle[2] = { SqBb(F1) | SqBb(G1) | SqBb(H1) | SqBb(F2) | SqBb(G2) | SqBb(H2),
                                   SqBb(F8) | SqBb(G8) | SqBb(H8) | SqBb(F7) | SqBb(G7) | SqBb(H7)
                                 };

static const U64 bbCentralFile = FILE_C_BB | FILE_D_BB | FILE_E_BB | FILE_F_BB;

void cParam::Init(void) {

  int pst_type = 1; // try 2 with higher pawn value
  int perc = 100;

  for (int sq = 0; sq < 64; sq++) {
    for (int sd = 0; sd < 2; sd++) {

      mg_pst[sd][P][REL_SQ(sq, sd)] = 100 + ((pstPawnMg[pst_type][sq] * perc) / 100);
      eg_pst[sd][P][REL_SQ(sq, sd)] = 100 + ((pstPawnEg[pst_type][sq] * perc) / 100);
      mg_pst[sd][N][REL_SQ(sq, sd)] = 325 + ((pstKnightMg[pst_type][sq] * perc) / 100);
      eg_pst[sd][N][REL_SQ(sq, sd)] = 325 + ((pstKnightEg[pst_type][sq] * perc) / 100);
      mg_pst[sd][B][REL_SQ(sq, sd)] = 325 + ((pstBishopMg[pst_type][sq] * perc) / 100);
      eg_pst[sd][B][REL_SQ(sq, sd)] = 325 + ((pstBishopEg[pst_type][sq] * perc) / 100);
      mg_pst[sd][R][REL_SQ(sq, sd)] = 500 + ((pstRookMg[pst_type][sq] * perc) / 100);
      eg_pst[sd][R][REL_SQ(sq, sd)] = 500 + ((pstRookEg[pst_type][sq] * perc) / 100);
      mg_pst[sd][Q][REL_SQ(sq, sd)] = 975 + ((pstQueenMg[pst_type][sq] * perc) / 100);
      eg_pst[sd][Q][REL_SQ(sq, sd)] = 975 + ((pstQueenEg[pst_type][sq] * perc) / 100);
      mg_pst[sd][K][REL_SQ(sq, sd)] = ((pstKingMg[pst_type][sq] * perc) / 100);
      eg_pst[sd][K][REL_SQ(sq, sd)] = ((pstKingEg[pst_type][sq] * perc) / 100);
    }
  }

  // Init tables for adjusting piece values 
  // according to the number of own pawns

  for (int i = 0; i < 9; i++) {
    np_table[i] = adj[i] * 6; // TODO: make 6 a variable
    rp_table[i] = adj[i] * 3; // TODO: make 3 a varialbe
  }

  // Init support mask (for detecting weak pawns)

  for (int sq = 0; sq < 64; sq++) {
    support_mask[WC][sq] = ShiftWest(SqBb(sq)) | ShiftEast(SqBb(sq));
    support_mask[WC][sq] |= FillSouth(support_mask[WC][sq]);

    support_mask[BC][sq] = ShiftWest(SqBb(sq)) | ShiftEast(SqBb(sq));
    support_mask[BC][sq] |= FillNorth(support_mask[BC][sq]);
  }

  // Init mask for passed pawn detection

  for (int sq = 0; sq < 64; sq++) {
    passed_mask[WC][sq] = FillNorthExcl(SqBb(sq));
    passed_mask[WC][sq] |= ShiftSideways(passed_mask[WC][sq]);
    passed_mask[BC][sq] = FillSouthExcl(SqBb(sq));
    passed_mask[BC][sq] |= ShiftSideways(passed_mask[BC][sq]);
  }
}

void cEngine::ScorePieces(POS *p, eData *e, int sd) {

  U64 bb_pieces, bb_attacks, bb_control;
  int op, sq, ksq, cnt;
  int att = 0;
  int wood = 0;

  // Init score with data from board class

  e->mg_sc[sd] = p->mg_sc[sd];
  e->eg_sc[sd] = p->eg_sc[sd];

  // Material adjustment

  int tmp = Par.np_table[p->cnt[sd][P]] * p->cnt[sd][N]   // knights lose value as pawns disappear
	      - Par.rp_table[p->cnt[sd][P]] * p->cnt[sd][R];  // rooks gain value as pawns disappear

  if (p->cnt[sd][B] == 2) tmp += 50;                      // bishop pair
  if (p->cnt[sd][N] == 2) tmp -= 10;                      // knight pair
  if (p->cnt[sd][R] == 2) tmp -= 5;                       // rook pair

  Add(e, sd, tmp, tmp);

  // Init variables

  op = Opp(sd);
  ksq = KingSq(p, op);

  // Init enemy king zone for attack evaluation. We mark squares where the king
  // can move plus two or three more squares facing enemy position.

  U64 bb_zone = k_attacks[ksq];
  (sd == WC) ? bb_zone |= ShiftSouth(bb_zone) : bb_zone |= ShiftNorth(bb_zone);

  // KNIGHT EVAL

  bb_pieces = PcBb(p, sd, N);
  while (bb_pieces) {
    sq = PopFirstBit(&bb_pieces);

    // knight king attack score

    bb_control = n_attacks[sq] & ~p->cl_bb[sd];

    if (bb_control & bb_zone) {
      wood++;
      att += 1;
    }

    // knight mobility score

    cnt = PopCnt(bb_control &~e->pawn_takes[op]);
    Add(e, sd, 4 * (cnt-4), 4 * (cnt-4));
  }

  // BISHOP EVAL

  bb_pieces = PcBb(p, sd, B);
  while (bb_pieces) {
    sq = PopFirstBit(&bb_pieces);

    // bishop king attack score

    bb_attacks = BAttacks(OccBb(p) ^ PcBb(p, sd, Q), sq);
    if (bb_attacks & bb_zone) {
      wood++;
      att += 1;
    }

    // bishop mobility score

    bb_control = BAttacks(OccBb(p), sq);
    cnt = PopCnt(bb_control);
    Add(e, sd, 5 * (cnt - 7),  5 * (cnt - 7));
  }

  // ROOK EVAL

  bb_pieces = PcBb(p, sd, R);
  while (bb_pieces) {
    sq = PopFirstBit(&bb_pieces);

   // rook king attack score

    bb_attacks = RAttacks(OccBb(p) ^ PcBb(p, sd, Q) ^ PcBb(p, sd, R), sq);
    if (bb_attacks & bb_zone) {
      wood++;
      att += 2;
    }

    // rook mobility score

    bb_control = RAttacks(OccBb(p), sq);
    cnt = PopCnt(bb_control);
    Add(e, sd, 2 * (cnt - 7), 4 * (cnt - 7));

    // rook on (half) open file

    U64 r_file = FillNorth(SqBb(sq)) | FillSouth(SqBb(sq));
    if (!(r_file & PcBb(p, sd, P))) {
      if (!(r_file & PcBb(p, op, P))) Add(e, sd, 12, 12);
      else                            Add(e, sd,  6,  6);
    }

    // rook on 7th rank attacking pawns or cutting off enemy king

    if (SqBb(sq) & bbRelRank[sd][RANK_7]) {
      if (PcBb(p, op, P) & bbRelRank[sd][RANK_7]
      ||  PcBb(p, op, K) & bbRelRank[sd][RANK_8]) {
          Add(e, sd, 16, 32);
      }
    }
  }

  // QUEEN EVAL
  
  bb_pieces = PcBb(p, sd, Q);
  while (bb_pieces) {
    sq = PopFirstBit(&bb_pieces);

    // queen king attack score

    bb_attacks  = BAttacks(OccBb(p) ^ PcBb(p, sd, B) ^ PcBb(p, sd, Q), sq);
    bb_attacks |= RAttacks(OccBb(p) ^ PcBb(p, sd, R) ^ PcBb(p, sd, Q), sq);
    if (bb_attacks & bb_zone) {
      wood++;
      att += 4;
    }

    // queen mobility score

	bb_control = QAttacks(OccBb(p), sq);
    cnt = PopCnt(bb_control);
    Add(e, sd, 1 * (cnt - 14), 2 * (cnt - 14));
  }

  // king attack - 

  if (PcBb(p, sd, Q)) {
    int att_score = (att * 20 * att_weight[wood]) / 256;
    Add(e, sd, att_score, att_score);
  }

}

void cEngine::ScorePawns(POS *p, eData *e, int sd) {

  U64 bb_pieces, bb_span;
  int op = Opp(sd);
  int sq, fl_unopposed;

  bb_pieces = PcBb(p, sd, P);
  while (bb_pieces) {
    sq = PopFirstBit(&bb_pieces);
    bb_span = GetFrontSpan(SqBb(sq), sd);
    fl_unopposed = ((bb_span & PcBb(p, op, P)) == 0);

    // DOUBLED PAWNS

    if (bb_span & PcBb(p, sd, P))
      Add(e, sd, -10, -20);

    // PASSED PAWNS

    if (!(passed_mask[sd][sq] & PcBb(p, Opp(sd), P))) {
      e->mg_sc[sd] += passed_bonus_mg[sd][Rank(sq)];
      e->eg_sc[sd] += passed_bonus_eg[sd][Rank(sq)];
    }
    
    // ISOLATED PAWNS

    if (!(adjacent_mask[File(sq)] & PcBb(p, sd, P))) {
      e->mg_sc[sd] -= (10 + 10 * fl_unopposed);
      e->eg_sc[sd] -= 20;
    }

    // WEAK PAWNS

    else if ((support_mask[sd][sq] & PcBb(p, sd, P)) == 0) {
      e->mg_sc[sd] -= (8 + 8 * fl_unopposed);
      e->eg_sc[sd] -= 10;
    }
  }
}

void cEngine::ScoreKing(POS *p, eData *e, int sd) {

  const int startSq[2] = { E1, E8 };
  const int qCastle[2] = { B1, B8 };
  const int kCastle[2] = { G1, G8 };

  int sq = KingSq(p, sd);

  // Normalize king square for pawn shield evaluation,
  // to discourage shuffling the king between g1 and h1.

  if (SqBb(sq) & bbKSCastle[sd]) sq = kCastle[sd];
  if (SqBb(sq) & bbQSCastle[sd]) sq = qCastle[sd];

  // Evaluate shielding and storming pawns on each file.

  U64 bbKingFile = FillNorth(SqBb(sq)) | FillSouth(SqBb(sq));
  int result = EvalKingFile(p, sd, bbKingFile);

  U64 bbNextFile = ShiftEast(bbKingFile);
  if (bbNextFile) result += EvalKingFile(p, sd, bbNextFile);

  bbNextFile = ShiftWest(bbKingFile);
  if (bbNextFile) result += EvalKingFile(p, sd, bbNextFile);

  e->mg_sc[sd] += result;
}

int cEngine::EvalKingFile(POS * p, int sd, U64 bbFile) {

  int shelter = EvalFileShelter(bbFile & PcBb(p, sd, P), sd);
  int storm   = EvalFileStorm  (bbFile & PcBb(p, Opp(sd), P), sd);
  if (bbFile & bbCentralFile) return (shelter / 2) + storm;
  else return shelter + storm;
}

int cEngine::EvalFileShelter(U64 bbOwnPawns, int sd) {

  if (!bbOwnPawns) return -36;
  if (bbOwnPawns & bbRelRank[sd][RANK_2]) return    2;
  if (bbOwnPawns & bbRelRank[sd][RANK_3]) return  -11;
  if (bbOwnPawns & bbRelRank[sd][RANK_4]) return  -20;
  if (bbOwnPawns & bbRelRank[sd][RANK_5]) return  -27;
  if (bbOwnPawns & bbRelRank[sd][RANK_6]) return  -32;
  if (bbOwnPawns & bbRelRank[sd][RANK_7]) return  -35;
  return 0;
}

int cEngine::EvalFileStorm(U64 bbOppPawns, int sd) {

  if (!bbOppPawns) return -16;
  if (bbOppPawns & bbRelRank[sd][RANK_3]) return -32;
  if (bbOppPawns & bbRelRank[sd][RANK_4]) return -16;
  if (bbOppPawns & bbRelRank[sd][RANK_5]) return -8;
  return 0;
}

void cEngine::Add(eData * e, int sd, int mg, int eg) {

  e->mg_sc[sd] += mg;
  e->eg_sc[sd] += eg;
}

int cEngine::Interpolate(POS * p, eData *e) {

  int mg_tot = e->mg_sc[WC] - e->mg_sc[BC];
  int eg_tot = e->eg_sc[WC] - e->eg_sc[BC];
  int mg_phase = Min(p->phase, 24);
  int eg_phase = 24 - mg_phase;

  return (mg_tot * mg_phase + eg_tot * eg_phase) / 24;
}

int cEngine::GetDrawFactor(POS * p, int sd) {

  int op = Opp(sd);

  if (p->phase < 2 && p->cnt[sd][P] == 0) return 0;

  return 64;
}

int cEngine::Evaluate(POS *p, eData *e) {

  // Try retrieving score from per-thread eval hashtable

  int addr = p->key % EVAL_HASH_SIZE;

  if (EvalTT[addr].key == p->key) {
    int sc = EvalTT[addr].score;
    return p->side == WC ? sc : -sc;
  }

  // Init helper bitboards

  e->pawn_takes[WC] = GetWPControl(PcBb(p, WC, P));
  e->pawn_takes[BC] = GetWPControl(PcBb(p, BC, P));

  // Run eval subroutines

  ScorePieces(p, e, WC);
  ScorePieces(p, e, BC);
  ScorePawns(p, e, WC);
  ScorePawns(p, e, BC);
  ScoreKing(p, e, WC);
  ScoreKing(p, e, BC);

  // Interpolate between midgame and endgame score

  int score = Interpolate(p, e);

  // Material imbalance evaluation

  int minorBalance = p->cnt[WC][N] - p->cnt[BC][N] + p->cnt[WC][B] - p->cnt[BC][B];
  int majorBalance = p->cnt[WC][R] - p->cnt[BC][R] + 2 * p->cnt[WC][Q] - 2 * p->cnt[BC][Q];

  int x = Max(majorBalance + 4, 0);
  if (x > 8) x = 8;

  int y = Max(minorBalance + 4, 0);
  if (y > 8) y = 8;

  score += imbalance[x][y];

  // Take care of drawish positions

  int scale = 64;
  if (score > 0) scale = GetDrawFactor(p, WC);
  if (score < 0) scale = GetDrawFactor(p, BC);
  score = (score * scale) / 64;

  // Make sure eval does not exceed mate score

  if (score < -MAX_EVAL)
    score = -MAX_EVAL;
  else if (score > MAX_EVAL)
    score = MAX_EVAL;

  // Save eval score in the evaluation hash table

  EvalTT[addr].key = p->key;
  EvalTT[addr].score = score;

  // Return score relative to the side to move

  return p->side == WC ? score : -score;
}
