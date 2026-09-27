#pragma once
// SOTA (Summits On The Air): asistente por menus desplegables, SIN deteccion GPS -- decision tomada con el
// operador: el spot es siempre un gesto deliberado, no una sugerencia
// automatica al pasar cerca de un pico. El mensaje final se envia como
// mensaje APRS normal a "APRS2SOTA" (ver aprsSendMessage(), sin tocar) y la
// respuesta de la pasarela (Spotted/Dupe/Error) llega como cualquier otro
// mensaje: se ve sola en la escena "Mensajes" del carrusel, no hace falta
// nada nuevo para eso.
//
// ★ 2026-09-26 (indicativo y frecuencia editables, a peticion del
//   operador): el PREFIJO del indicativo ya no es una lista aparte -- es la
//   MISMA tabla de asociaciones que el paso ASOCIACION (mas una opcion
//   "(vacio)" para activar sin prefijo, el caso normal en el pais propio).
//   Solo el indicativo BASE (con su sufijo /P si aplica) sigue fijo en el
//   codigo -- eso no cambia entre activaciones. La frecuencia ya no es una
//   lista preparada de antemano: son 6 digitos sueltos, "000.000", con el
//   mismo mecanismo que los 3 digitos del numero de sombre.
//
// Sin String en ningun sitio de este fichero: dibujaTextoEnvuelto() y
// msgLogPush() (mas arriba, en epaper_techo.cpp) tenian el mismo patron --
// texto reconstruido en cada repintado -- y ahi es donde aparecio el bug
// del mensaje cortado tras la primera linea (sospecha de fallo silencioso
// de String por RAM justa). Las funciones de aqui abajo se van a llamar
// una vez por fila visible en CADA repintado del asistente: mismo riesgo,
// mismo remedio.
#include <stddef.h>
#include <stdint.h>

// ---- consulta de la tabla asociacion/region (ver sota_regions_data.h,
//      generada por gen_sota_regions_data.py a partir del CSV oficial de
//      sotadata.org.uk -- no editar sota_regions_data.h a mano) ----
int sotaAssocCount();
const char *sotaAssocCode(int assocIdx);                    // "F", "EA3", "?" si fuera de rango
int sotaRegionCount(int assocIdx);
const char *sotaRegionCode(int assocIdx, int regionIdx);    // "PE", "AB", "?" si fuera de rango

// ---- el indicativo base: ya NO va fijo en el codigo (2026-09-26). Es el
//      indicativo del nodo (DigiConfig::callsign) sin el SSID y con "/P"
//      ("EA2OY-11" -> "EA2OY/P"); si ya lleva una "/" se deja tal cual. Se lo
//      pasa quien abre el asistente (sotaWizardOpen). El prefijo de pais se
//      elige en el asistente (paso CALLPREFIX). ----

// Los siete modos que acepta APRS2SOTA (ver Aprs2Sota_Info.php): no son
// una lista editable, es lo que la pasarela entiende de verdad.
extern const char *const kSotaModes[];
extern const int kSotaModeCount;

// Comentario opcional al final del mensaje (paso COMMENT). El indice 0 del
// paso es "(vacio)" = sin comentario; 1..kSotaCommentCount = kSotaComments[i-1].
extern const char *const kSotaComments[];
extern const int kSotaCommentCount;

// ---- el asistente: un paso a la vez ----
//
// ★ 2026-09-27: DOS RECORRIDOS (menu "SOTA", peticion del operador):
//   - PLAN DE ACTIVACION (sotaWizardOpen): cumbre -> frecuencia -> modo ->
//     prefijo de indicativo -> CONFIRM ("guardar plan?"). NO pide comentario
//     ni envia nada: el que llama guarda sotaWizardPlan() en la configuracion.
//   - SPOT (sotaWizardOpenSpot): parte del plan guardado y SOLO pide el
//     comentario -> CONFIRM ("enviar spot?") -> DONE: el que llama envia
//     sotaWizardMessage() a APRS2SOTA.
//   El enum va en el orden del recorrido del plan (COMMENT solo en el spot).
enum SotaStep : uint8_t {
  SOTA_STEP_ASSOC = 0,
  SOTA_STEP_REGION,
  SOTA_STEP_DIGIT1,
  SOTA_STEP_DIGIT2,
  SOTA_STEP_DIGIT3,
  SOTA_STEP_RECAP,       // sin lista: solo confirmar o volver ("F/PE-103")
  SOTA_STEP_FREQ1,       // frecuencia MHz, "000.000": centenas
  SOTA_STEP_FREQ2,       //   decenas
  SOTA_STEP_FREQ3,       //   unidades
  SOTA_STEP_FREQ4,       //   decimas       (tras el punto)
  SOTA_STEP_FREQ5,       //   centesimas
  SOTA_STEP_FREQ6,       //   milesimas
  SOTA_STEP_MODE,
  SOTA_STEP_CALLPREFIX,  // misma tabla que ASSOC + "(vacio)" en el indice 0
  SOTA_STEP_COMMENT,     // solo SPOT: "(vacio)" + kSotaComments (QRV, QSY...)
  SOTA_STEP_CONFIRM,     // sin lista: "guardar plan?" / "enviar spot?"
  SOTA_STEP_DONE,        // plan: el que llama guarda sotaWizardPlan();
                         // spot: el que llama envia sotaWizardMessage()
};

// Reinicia el asistente en el primer paso.
//  - `nodeCall`: el indicativo del nodo, con o sin SSID (ver arriba).
//  - `recentsCsv`: las ultimas asociaciones usadas, la mas reciente primero
//    ("F,EA2,EA1", ver DigiConfig::sotaRecent): se ponen EN CABEZA de la
//    lista del paso ASSOC, antes de la lista completa, para no tener que
//    desplazar 40 filas cada vez. Los codigos que ya no existan se ignoran.
//  - `lastSpot`: la ultima cumbre spoteada y su prefijo, "F/PE-103,EA2" (o
//    "F/PE-103," sin prefijo; ver DigiConfig::sotaLast). Si es valido, la
//    PRIMERA fila del paso ASSOC es "Re F/PE-103": conserva cumbre y
//    prefijo y salta directo a la frecuencia; frecuencia y modo se eligen de
//    nuevo y el prefijo se salta.
// Abre el recorrido PLAN DE ACTIVACION.
constexpr int kSotaRecentMax = 4;
void sotaWizardOpen(const char *nodeCall, const char *recentsCsv = nullptr,
                    const char *lastSpot = nullptr);
// Abre el recorrido SPOT a partir del plan guardado (formato de
// sotaWizardPlan()). false = no hay plan valido: el asistente no se abre.
bool sotaWizardOpenSpot(const char *nodeCall, const char *planCsv);
bool sotaWizardIsSpot();             // true = recorrido SPOT, false = PLAN
SotaStep sotaWizardStep();
int sotaWizardOptionCount();         // 0 en los pasos sin lista (RECAP/CONFIRM)
int sotaWizardCursor();
void sotaWizardNavigate(int delta);  // +1/-1 con vuelta; no hace nada si count()==0
bool sotaWizardConfirm();            // true = avanza de paso; false = ya esta en DONE
bool sotaWizardBack();               // true = retrocede; false = ya esta en el primero

// Etiqueta de la opcion `i` del paso ACTUAL ("F", "7", "(vacio)", "SSB"...).
// Devuelve `out` para poder encadenar directo en un drawText(...).
const char *sotaWizardOptionLabel(int i, char *out, size_t outLen);
// "F/PE-103" -- construido con lo elegido HASTA AHORA (valido desde RECAP).
const char *sotaWizardSummary(char *out, size_t outLen);
// El plan de activacion elegido, para guardarlo en la configuracion
// (DigiConfig::sotaPlan): "<Ref>,<Prefijo>,<6 digitos de frecuencia>,<Modo>",
// p. ej. "F/PE-103,EA2,145500,SSB" o "F/PE-103,,145500,SSB" sin prefijo.
// Valido en CONFIRM/DONE del recorrido PLAN.
const char *sotaWizardPlan(char *out, size_t outLen);
// El mensaje APRS2SOTA completo: "<Ref> <FreqMHz> <Modo> <Indicativo> [Comentario]"
// (orden preferido segun Aprs2Sota_Info.php). Valido en CONFIRM/DONE. La
// frecuencia va pegada a "MHz" SIN espacio (la pasarela lo exige asi
// cuando se da una unidad).
const char *sotaWizardMessage(char *out, size_t outLen);

// La lista de recientes actualizada con la asociacion elegida (delante, sin
// repetir, maximo kSotaRecentMax), en el mismo formato que recibe
// sotaWizardOpen(). La llama quien guarda el plan, para guardarla.
const char *sotaWizardRecents(char *out, size_t outLen);

// La cumbre y el prefijo del plan actual, en el formato que recibe
// sotaWizardOpen() como `lastSpot`. La llama quien guarda el plan, para
// ofrecer el "Re ..." la proxima vez.
const char *sotaWizardLastSpot(char *out, size_t outLen);

// ---- el plan de activacion, descifrado (escena SOTA del carrusel) ----
// (2026-09-27: sin nombre ni altitud de la cumbre, decision del operador: sobre
//  el terreno ya se sabe en que cumbre se esta, no hace falta la lista.)
struct SotaPlan {
  char ref[16];    // "F/PE-103"
  char freq[12];   // "145.500"
  char mode[8];    // "SSB"
  char call[32];   // "EA2/F4LEK/P"
};
// true y `out` relleno si `planCsv` (DigiConfig::sotaPlan) es un plan valido.
// `nodeCall` es el indicativo del nodo, para montar el indicativo /P.
bool sotaPlanParse(const char *planCsv, const char *nodeCall, SotaPlan *out);
