#include "sota.h"
#include "sota_regions_data.h"
#include <stdio.h>
#include <string.h>

// Estos siete SI son fijos: son literalmente los que Aprs2Sota_Info.php dice
// que acepta (otros modos se convierten a uno de estos, o dan error).
const char *const kSotaModes[] = {"AM", "CW", "DATA", "DV", "FM", "SSB", "OTHER"};
const int kSotaModeCount = sizeof(kSotaModes) / sizeof(kSotaModes[0]);

// Comentarios que el operador puede anadir al final del spot (2026-09-26).
const char *const kSotaComments[] = {"QRV", "QSY", "QRT", "TEST"};
const int kSotaCommentCount = sizeof(kSotaComments) / sizeof(kSotaComments[0]);

// ---------------------------------------------------------------------------
//  Consulta de la tabla asociacion/region.
// ---------------------------------------------------------------------------
int sotaAssocCount() { return kSotaAssocCount; }

const char *sotaAssocCode(int assocIdx) {
  if (assocIdx < 0 || assocIdx >= kSotaAssocCount) return "?";
  return kSotaAssocs[assocIdx].code;
}

int sotaRegionCount(int assocIdx) {
  if (assocIdx < 0 || assocIdx >= kSotaAssocCount) return 0;
  return (int)kSotaAssocs[assocIdx].regionCount;
}

const char *sotaRegionCode(int assocIdx, int regionIdx) {
  if (assocIdx < 0 || assocIdx >= kSotaAssocCount) return "?";
  const SotaAssocEntry &a = kSotaAssocs[assocIdx];
  if (regionIdx < 0 || regionIdx >= (int)a.regionCount) return "?";
  return kSotaRegionCodes[a.regionStart + regionIdx];
}

// ---------------------------------------------------------------------------
//  Estado del asistente.
// ---------------------------------------------------------------------------
namespace {
SotaStep gStep = SOTA_STEP_ASSOC;
int gCursor = 0;
int gAssocIdx = 0;
int gRegionIdx = 0;
int gDigit[3] = {0, 0, 0};
// 0 = "(vacio)" (activa sin prefijo, el caso normal en el pais propio);
// 1..kSotaAssocCount = sotaAssocCode(gCallPrefixOpt - 1).
int gCallPrefixOpt = 0;
int gFreqDigit[6] = {0, 0, 0, 0, 0, 0};
int gModeIdx = 0;
// 0 = sin comentario; 1..kSotaCommentCount = kSotaComments[gCommentOpt - 1].
int gCommentOpt = 0;
// Recientes (indices en la tabla), en cabeza de la lista del paso ASSOC, justo
// despues de la fila del re-spot si la hay (ver headRows/assocOfRow).
int gRecent[kSotaRecentMax];
int gRecentN = 0;
// Fila de la lista ASSOC que se eligio (no el indice de la tabla): al volver
// atras desde REGION (o desde FREQ1 en un re-spot) el cursor vuelve a ESA fila.
int gAssocRow = 0;
// RE-SPOT: la ultima cumbre spoteada (ver sotaWizardOpen). Si gLastOk, la
// fila 0 del paso ASSOC lo ofrece; gRespot = este spot va por ese camino
// (se salto REGION..CALLPREFIX), para que "atras" desde FREQ1 vuelva a ASSOC.
bool gLastOk = false;
int gLastAssoc = 0, gLastRegion = 0, gLastDigit[3] = {0, 0, 0}, gLastPfxOpt = 0;
bool gRespot = false;
// Recorrido SPOT (sotaWizardOpenSpot): solo COMMENT y CONFIRM.
bool gSpot = false;
// Indicativo base: el del nodo sin SSID y con "/P" (ver sota.h).
char gBaseCall[20] = "NOCALL/P";

int assocIndexOf(const char *code, size_t len) {
  for (int i = 0; i < kSotaAssocCount; i++) {
    const char *c = kSotaAssocs[i].code;
    if (strlen(c) == len && strncmp(c, code, len) == 0) return i;
  }
  return -1;
}

int regionIndexOf(int assocIdx, const char *code, size_t len) {
  for (int r = 0; r < sotaRegionCount(assocIdx); r++) {
    const char *c = sotaRegionCode(assocIdx, r);
    if (strlen(c) == len && strncmp(c, code, len) == 0) return r;
  }
  return -1;
}

// Filas de cabeza del paso ASSOC: [re-spot] + recientes, antes de la lista completa.
int respotRows() { return gLastOk ? 1 : 0; }
int headRows() { return respotRows() + gRecentN; }

// Indice en la tabla de la fila `row` (solo para filas que NO son el re-spot).
int assocOfRow(int row) {
  row -= respotRows();
  return (row < gRecentN) ? gRecent[row] : row - gRecentN;
}

// "F/PE-103" + opcionalmente ",EA2" / "," -> indices de la tabla. El campo
// del prefijo termina en la siguiente "," o al final; `*after` queda
// apuntando a ese caracter. Cualquier cosa que no cuadre con la tabla da
// false: mejor no ofrecer un dato que ofrecer uno falso.
bool parseRefPfx(const char *s, int &a, int &r, int d[3], int &pfx, const char **after) {
  if (!s || !*s) return false;
  const char *slash = strchr(s, '/');
  const char *dash = slash ? strchr(slash, '-') : nullptr;
  if (!slash || !dash) return false;
  a = assocIndexOf(s, (size_t)(slash - s));
  if (a < 0) return false;
  r = regionIndexOf(a, slash + 1, (size_t)(dash - slash - 1));
  if (r < 0) return false;
  for (int i = 0; i < 3; i++) {
    if (dash[1 + i] < '0' || dash[1 + i] > '9') return false;
    d[i] = dash[1 + i] - '0';
  }
  const char *rest = dash + 4;
  pfx = 0;
  if (*rest == ',') {
    rest++;
    const char *end = strchr(rest, ',');
    const size_t len = end ? (size_t)(end - rest) : strlen(rest);
    if (len > 0) {
      const int pa = assocIndexOf(rest, len);
      if (pa < 0) return false;
      pfx = pa + 1;
    }
    rest += len;
  } else if (*rest) {
    return false;
  }
  *after = rest;
  return true;
}

// "F/PE-103,EA2" (o "F/PE-103,") -> gLast*.
void parseLastSpot(const char *s) {
  int a, r, d[3], pfx;
  const char *after;
  gLastOk = parseRefPfx(s, a, r, d, pfx, &after) && *after == '\0';
  if (!gLastOk) return;
  gLastAssoc = a;
  gLastRegion = r;
  for (int i = 0; i < 3; i++) gLastDigit[i] = d[i];
  gLastPfxOpt = pfx;
}

// Plan completo (ver sotaWizardPlan): cumbre, prefijo, 6 digitos de
// frecuencia y modo. false si algo no cuadra.
bool parsePlan(const char *s, int &a, int &r, int d[3], int &pfx, int f[6], int &mode) {
  const char *p;
  if (!parseRefPfx(s, a, r, d, pfx, &p) || *p != ',') return false;
  p++;
  for (int i = 0; i < 6; i++) {
    if (p[i] < '0' || p[i] > '9') return false;
    f[i] = p[i] - '0';
  }
  p += 6;
  if (*p != ',') return false;
  p++;
  for (int m = 0; m < kSotaModeCount; m++) {
    if (strcmp(p, kSotaModes[m]) == 0) { mode = m; return true; }
  }
  return false;
}

// "EA2OY-11" -> "EA2OY/P"; con una "/" ya puesta se deja tal cual.
void baseCallOf(const char *nodeCall, char *out, size_t outLen) {
  const char *c = (nodeCall && *nodeCall) ? nodeCall : "NOCALL";
  const char *dash = strchr(c, '-');
  const int len = dash ? (int)(dash - c) : (int)strlen(c);
  if (strchr(c, '/')) snprintf(out, outLen, "%.*s", len, c);
  else snprintf(out, outLen, "%.*s/P", len, c);
}
void setBaseCall(const char *nodeCall) { baseCallOf(nodeCall, gBaseCall, sizeof(gBaseCall)); }

// Indicativo del spot: "<prefijo>/<base>" o solo "<base>" sin prefijo.
void fullCallOf(int pfxOpt, const char *base, char *out, size_t outLen) {
  if (pfxOpt <= 0) snprintf(out, outLen, "%s", base);
  else snprintf(out, outLen, "%s/%s", sotaAssocCode(pfxOpt - 1), base);
}
}  // namespace

void sotaWizardOpen(const char *nodeCall, const char *recentsCsv, const char *lastSpot) {
  gStep = SOTA_STEP_ASSOC;
  gCursor = 0;
  gAssocIdx = 0;
  gAssocRow = 0;
  gRespot = false;
  gSpot = false;
  setBaseCall(nodeCall);
  parseLastSpot(lastSpot);
  gRecentN = 0;
  for (const char *p = recentsCsv; p && *p && gRecentN < kSotaRecentMax;) {
    const char *end = strchr(p, ',');
    size_t len = end ? (size_t)(end - p) : strlen(p);
    int idx = assocIndexOf(p, len);
    bool dup = false;
    for (int k = 0; k < gRecentN; k++) dup = dup || (gRecent[k] == idx);
    if (idx >= 0 && !dup) gRecent[gRecentN++] = idx;
    if (!end) break;
    p = end + 1;
  }
  gRegionIdx = 0;
  gDigit[0] = gDigit[1] = gDigit[2] = 0;
  gCallPrefixOpt = 0;
  for (int i = 0; i < 6; i++) gFreqDigit[i] = 0;
  gModeIdx = 0;
  gCommentOpt = 0;
}

bool sotaWizardOpenSpot(const char *nodeCall, const char *planCsv) {
  int a, r, d[3], pfx, f[6], mode;
  if (!parsePlan(planCsv, a, r, d, pfx, f, mode)) return false;
  setBaseCall(nodeCall);
  gAssocIdx = a;
  gRegionIdx = r;
  for (int i = 0; i < 3; i++) gDigit[i] = d[i];
  gCallPrefixOpt = pfx;
  for (int i = 0; i < 6; i++) gFreqDigit[i] = f[i];
  gModeIdx = mode;
  gCommentOpt = 0;
  gRespot = false;
  gSpot = true;
  gStep = SOTA_STEP_COMMENT;
  gCursor = 0;
  return true;
}

bool sotaWizardIsSpot() { return gSpot; }

SotaStep sotaWizardStep() { return gStep; }
int sotaWizardCursor() { return gCursor; }

int sotaWizardOptionCount() {
  switch (gStep) {
    case SOTA_STEP_ASSOC:      return headRows() + sotaAssocCount();
    case SOTA_STEP_REGION:     return sotaRegionCount(gAssocIdx);
    case SOTA_STEP_DIGIT1:
    case SOTA_STEP_DIGIT2:
    case SOTA_STEP_DIGIT3:     return 10;
    case SOTA_STEP_RECAP:      return 0;
    case SOTA_STEP_CALLPREFIX: return sotaAssocCount() + 1;  // +1 = "(vacio)"
    case SOTA_STEP_FREQ1:
    case SOTA_STEP_FREQ2:
    case SOTA_STEP_FREQ3:
    case SOTA_STEP_FREQ4:
    case SOTA_STEP_FREQ5:
    case SOTA_STEP_FREQ6:      return 10;
    case SOTA_STEP_MODE:       return kSotaModeCount;
    case SOTA_STEP_COMMENT:    return kSotaCommentCount + 1;  // +1 = "(vacio)"
    case SOTA_STEP_CONFIRM:    return 0;
    case SOTA_STEP_DONE:       return 0;
  }
  return 0;
}

void sotaWizardNavigate(int delta) {
  int n = sotaWizardOptionCount();
  if (n <= 0) return;
  int c = (gCursor + delta) % n;
  if (c < 0) c += n;
  gCursor = c;
}

// Cada paso, al confirmar: guarda gCursor en el campo que le toca, resetea
// el cursor a 0 para el paso siguiente (salvo los "sin lista", que no lo
// usan) y avanza gStep. CONFIRM->DONE es la unica transicion que devuelve
// false: ahi es donde el que llama debe leer sotaWizardMessage() y enviarlo.
bool sotaWizardConfirm() {
  switch (gStep) {
    case SOTA_STEP_ASSOC:
      gAssocRow = gCursor;
      if (gCursor < respotRows()) {
        // RE-SPOT: misma cumbre y mismo prefijo; se va directo a la frecuencia.
        gRespot = true;
        gAssocIdx = gLastAssoc; gRegionIdx = gLastRegion;
        for (int i = 0; i < 3; i++) gDigit[i] = gLastDigit[i];
        gCallPrefixOpt = gLastPfxOpt;
        gCursor = 0; gStep = SOTA_STEP_FREQ1; return true;
      }
      gRespot = false;
      gAssocIdx = assocOfRow(gCursor);
      gCursor = 0; gStep = SOTA_STEP_REGION; return true;
    case SOTA_STEP_REGION:
      gRegionIdx = gCursor; gCursor = 0; gStep = SOTA_STEP_DIGIT1; return true;
    case SOTA_STEP_DIGIT1:
      gDigit[0] = gCursor; gCursor = 0; gStep = SOTA_STEP_DIGIT2; return true;
    case SOTA_STEP_DIGIT2:
      gDigit[1] = gCursor; gCursor = 0; gStep = SOTA_STEP_DIGIT3; return true;
    case SOTA_STEP_DIGIT3:
      gDigit[2] = gCursor; gCursor = 0; gStep = SOTA_STEP_RECAP; return true;
    case SOTA_STEP_RECAP:
      gCursor = 0; gStep = SOTA_STEP_FREQ1; return true;
    case SOTA_STEP_FREQ1:
      gFreqDigit[0] = gCursor; gCursor = 0; gStep = SOTA_STEP_FREQ2; return true;
    case SOTA_STEP_FREQ2:
      gFreqDigit[1] = gCursor; gCursor = 0; gStep = SOTA_STEP_FREQ3; return true;
    case SOTA_STEP_FREQ3:
      gFreqDigit[2] = gCursor; gCursor = 0; gStep = SOTA_STEP_FREQ4; return true;
    case SOTA_STEP_FREQ4:
      gFreqDigit[3] = gCursor; gCursor = 0; gStep = SOTA_STEP_FREQ5; return true;
    case SOTA_STEP_FREQ5:
      gFreqDigit[4] = gCursor; gCursor = 0; gStep = SOTA_STEP_FREQ6; return true;
    case SOTA_STEP_FREQ6:
      gFreqDigit[5] = gCursor; gCursor = 0; gStep = SOTA_STEP_MODE; return true;
    case SOTA_STEP_MODE:
      gModeIdx = gCursor;
      if (gRespot) { gCursor = 0; gStep = SOTA_STEP_CONFIRM; return true; }  // prefijo conservado
      gCursor = gCallPrefixOpt; gStep = SOTA_STEP_CALLPREFIX; return true;
    case SOTA_STEP_CALLPREFIX:
      gCallPrefixOpt = gCursor; gCursor = 0; gStep = SOTA_STEP_CONFIRM; return true;
    case SOTA_STEP_COMMENT:
      gCommentOpt = gCursor; gCursor = 0; gStep = SOTA_STEP_CONFIRM; return true;
    case SOTA_STEP_CONFIRM:
      gStep = SOTA_STEP_DONE; return false;
    case SOTA_STEP_DONE:
      return false;
  }
  return false;
}

// Al volver atras, restaura el cursor al valor YA elegido en ese paso (no a
// 0): el operador corrige un solo paso sin tener que re-navegar desde cero.
bool sotaWizardBack() {
  switch (gStep) {
    case SOTA_STEP_ASSOC:
      return false;
    case SOTA_STEP_REGION:
      gStep = SOTA_STEP_ASSOC; gCursor = gAssocRow; return true;
    case SOTA_STEP_DIGIT1:
      gStep = SOTA_STEP_REGION; gCursor = gRegionIdx; return true;
    case SOTA_STEP_DIGIT2:
      gStep = SOTA_STEP_DIGIT1; gCursor = gDigit[0]; return true;
    case SOTA_STEP_DIGIT3:
      gStep = SOTA_STEP_DIGIT2; gCursor = gDigit[1]; return true;
    case SOTA_STEP_RECAP:
      gStep = SOTA_STEP_DIGIT3; gCursor = gDigit[2]; return true;
    case SOTA_STEP_FREQ1:
      if (gRespot) { gStep = SOTA_STEP_ASSOC; gCursor = gAssocRow; return true; }
      gStep = SOTA_STEP_RECAP; gCursor = 0; return true;
    case SOTA_STEP_FREQ2:
      gStep = SOTA_STEP_FREQ1; gCursor = gFreqDigit[0]; return true;
    case SOTA_STEP_FREQ3:
      gStep = SOTA_STEP_FREQ2; gCursor = gFreqDigit[1]; return true;
    case SOTA_STEP_FREQ4:
      gStep = SOTA_STEP_FREQ3; gCursor = gFreqDigit[2]; return true;
    case SOTA_STEP_FREQ5:
      gStep = SOTA_STEP_FREQ4; gCursor = gFreqDigit[3]; return true;
    case SOTA_STEP_FREQ6:
      gStep = SOTA_STEP_FREQ5; gCursor = gFreqDigit[4]; return true;
    case SOTA_STEP_MODE:
      gStep = SOTA_STEP_FREQ6; gCursor = gFreqDigit[5]; return true;
    case SOTA_STEP_CALLPREFIX:
      gStep = SOTA_STEP_MODE; gCursor = gModeIdx; return true;
    case SOTA_STEP_COMMENT:
      return false;   // primer paso del SPOT: largo = salir
    case SOTA_STEP_CONFIRM:
      if (gSpot) { gStep = SOTA_STEP_COMMENT; gCursor = gCommentOpt; return true; }
      if (gRespot) { gStep = SOTA_STEP_MODE; gCursor = gModeIdx; return true; }
      gStep = SOTA_STEP_CALLPREFIX; gCursor = gCallPrefixOpt; return true;
    case SOTA_STEP_DONE:
      return false;
  }
  return false;
}

const char *sotaWizardOptionLabel(int i, char *out, size_t outLen) {
  if (!out || outLen == 0) return out;
  out[0] = '\0';
  switch (gStep) {
    case SOTA_STEP_ASSOC:
      // Los recientes llevan "*" delante para distinguirlos de la lista completa.
      // "Re" = re-spot de la ultima cumbre (cabe: "Re EA2/NV-103" son 13 caracteres).
      if (i < respotRows()) {
        snprintf(out, outLen, "Re %s/%s-%d%d%d", sotaAssocCode(gLastAssoc),
                 sotaRegionCode(gLastAssoc, gLastRegion),
                 gLastDigit[0], gLastDigit[1], gLastDigit[2]);
      } else if (i < headRows()) {
        snprintf(out, outLen, "* %s", sotaAssocCode(assocOfRow(i)));
      } else {
        snprintf(out, outLen, "%s", sotaAssocCode(assocOfRow(i)));
      }
      break;
    case SOTA_STEP_REGION:
      snprintf(out, outLen, "%s", sotaRegionCode(gAssocIdx, i));
      break;
    case SOTA_STEP_DIGIT1:
    case SOTA_STEP_DIGIT2:
    case SOTA_STEP_DIGIT3:
      snprintf(out, outLen, "%d", i);
      break;
    case SOTA_STEP_CALLPREFIX:
      if (i == 0) snprintf(out, outLen, "(none)");
      else snprintf(out, outLen, "%s", sotaAssocCode(i - 1));
      break;
    case SOTA_STEP_FREQ1:
    case SOTA_STEP_FREQ2:
    case SOTA_STEP_FREQ3:
    case SOTA_STEP_FREQ4:
    case SOTA_STEP_FREQ5:
    case SOTA_STEP_FREQ6:
      snprintf(out, outLen, "%d", i);
      break;
    case SOTA_STEP_MODE:
      if (i >= 0 && i < kSotaModeCount) snprintf(out, outLen, "%s", kSotaModes[i]);
      break;
    case SOTA_STEP_COMMENT:
      if (i == 0) snprintf(out, outLen, "(none)");
      else if (i <= kSotaCommentCount) snprintf(out, outLen, "%s", kSotaComments[i - 1]);
      break;
    default:
      break;
  }
  return out;
}

const char *sotaWizardSummary(char *out, size_t outLen) {
  if (!out || outLen == 0) return out;
  snprintf(out, outLen, "%s/%s-%d%d%d",
           sotaAssocCode(gAssocIdx), sotaRegionCode(gAssocIdx, gRegionIdx),
           gDigit[0], gDigit[1], gDigit[2]);
  return out;
}

const char *sotaWizardMessage(char *out, size_t outLen) {
  if (!out || outLen == 0) return out;
  char ref[24];
  sotaWizardSummary(ref, sizeof(ref));

  char call[40];
  fullCallOf(gCallPrefixOpt, gBaseCall, call, sizeof(call));

  const char *mode = (gModeIdx >= 0 && gModeIdx < kSotaModeCount) ? kSotaModes[gModeIdx] : "?";

  // Frecuencia pegada a "MHz" SIN espacio: Aprs2Sota_Info.php es explicito
  // ("units ... MUST be included without any spaces or other characters").
  // orden preferido: <Ass/Ref> <Freq> <Mode> [callsign] [comment]. El
  // comentario solo puede ir DETRAS del indicativo, que aqui va siempre.
  const bool conComent = gCommentOpt >= 1 && gCommentOpt <= kSotaCommentCount;
  snprintf(out, outLen, "%s %d%d%d.%d%d%dMHz %s %s%s%s",
           ref,
           gFreqDigit[0], gFreqDigit[1], gFreqDigit[2],
           gFreqDigit[3], gFreqDigit[4], gFreqDigit[5],
           mode, call,
           conComent ? " " : "", conComent ? kSotaComments[gCommentOpt - 1] : "");
  return out;
}

const char *sotaWizardRecents(char *out, size_t outLen) {
  if (!out || outLen == 0) return out;
  snprintf(out, outLen, "%s", sotaAssocCode(gAssocIdx));
  int n = 1;
  for (int k = 0; k < gRecentN && n < kSotaRecentMax; k++) {
    if (gRecent[k] == gAssocIdx) continue;
    size_t used = strlen(out);
    snprintf(out + used, outLen - used, ",%s", sotaAssocCode(gRecent[k]));
    n++;
  }
  return out;
}

const char *sotaWizardLastSpot(char *out, size_t outLen) {
  if (!out || outLen == 0) return out;
  char ref[24];
  sotaWizardSummary(ref, sizeof(ref));
  snprintf(out, outLen, "%s,%s", ref,
           gCallPrefixOpt > 0 ? sotaAssocCode(gCallPrefixOpt - 1) : "");
  return out;
}

const char *sotaWizardPlan(char *out, size_t outLen) {
  if (!out || outLen == 0) return out;
  char ref[24];
  sotaWizardSummary(ref, sizeof(ref));
  const char *mode = (gModeIdx >= 0 && gModeIdx < kSotaModeCount) ? kSotaModes[gModeIdx] : "SSB";
  snprintf(out, outLen, "%s,%s,%d%d%d%d%d%d,%s", ref,
           gCallPrefixOpt > 0 ? sotaAssocCode(gCallPrefixOpt - 1) : "",
           gFreqDigit[0], gFreqDigit[1], gFreqDigit[2],
           gFreqDigit[3], gFreqDigit[4], gFreqDigit[5], mode);
  return out;
}

bool sotaPlanParse(const char *planCsv, const char *nodeCall, SotaPlan *out) {
  int a, r, d[3], pfx, f[6], mode;
  if (!out || !parsePlan(planCsv, a, r, d, pfx, f, mode)) return false;
  snprintf(out->ref, sizeof(out->ref), "%s/%s-%d%d%d",
           sotaAssocCode(a), sotaRegionCode(a, r), d[0], d[1], d[2]);
  snprintf(out->freq, sizeof(out->freq), "%d%d%d.%d%d%d", f[0], f[1], f[2], f[3], f[4], f[5]);
  snprintf(out->mode, sizeof(out->mode), "%s", kSotaModes[mode]);
  char base[20];
  baseCallOf(nodeCall, base, sizeof(base));
  fullCallOf(pfx, base, out->call, sizeof(out->call));
  return true;
}
