#ifndef MARK_SCAN_H
#define MARK_SCAN_H

// Автоопределение метки ротора.
//
// Метка (Init_angle) — угол от импульса датчика до ВМТ. По нему прошивка считает, через
// сколько после импульса дать искру: `Init_angle - Advance_angle` градусов. У одноствола
// датчик один и даёт один импульс за оборот, поэтому по самому сигналу видно только целый
// оборот, а где в нём ВМТ — нет. Прошивка не может «увидеть» метку, зато может найти её
// опытом — тем же, что делает владелец руками: крутит мотор и слушает.
//
// Правило простое: искра должна попасть в конец такта сжатия. Если метка угадана, вспышка
// раскручивает мотор выше оборотов прокрутки — это и есть признак верного значения. Значит,
// владелец крутит мотор (кикстартером или стартером), а блок перебирает кандидатов и
// следит за оборотами.
//
// Порядок перебора — от больших значений к меньшим, и это про безопасность. Искра приходит
// через `Init_angle - Advance_angle` градусов после импульса, то есть чем МЕНЬШЕ кандидат,
// тем РАНЬШЕ искра: при метке 100° кандидат 0 дал бы искру в 100° до ВМТ — удар в кик.
// Перебор сверху вниз начинает с искры «поздно» (в выхлоп — мотор просто не схватывает) и
// подходит к верной метке со стороны запаздывания, а как только мотор схватил, перебор
// останавливается.
//
// Во время перебора опережение берётся нулевым принудительно: искра идёт ровно в точку
// кандидата. Мотор поэтому может работать неровно, зато раньше ВМТ искра не уходит.
//
// Уточнение после вспышки: владелец отпускает газ и даёт мотору поработать на холостых,
// а блок пробует соседние значения (±6° шагом 2°) и берёт то, на котором обороты ровнее
// всего (разброс периодов оборота минимален). Если мотор на каком-то значении заглох,
// остаётся прежнее, лучшее.

#include <Arduino.h>
#include <Preferences.h>
#include "Variables.h"

extern Preferences preferences;
// Объявлена в core/net/DEVICE_COMMANDS.h, а кадры нужны и здесь.
void sendDeviceFrame(const String &frame);

// Сколько оборотов прокрутки держим одного кандидата: на 600 об/мин это около секунды,
// кикстартеру хватает на два-три удара.
#define MARK_SCAN_REVS_PER_CANDIDATE 6
// Обороты, с которых считаем, что мотор завёлся (прокрутка стартером даёт 300…900).
#define MARK_SCAN_RUN_RPM 1100
// Столько оборотов подряд выше порога, чтобы поверить: одиночная вспышка не в счёт.
#define MARK_SCAN_RUN_REVS 3
// Ниже этих оборотов мотор не крутят: ждём владельца.
#define MARK_SCAN_CRANK_RPM 120
// Уточнение: сколько оборотов слушаем каждое значение вокруг найденной метки.
#define MARK_SCAN_REFINE_REVS 4
// Владелец не крутит мотор — столько ждём и останавливаемся, чтобы не держать режим.
#define MARK_SCAN_IDLE_TIMEOUT_MS 15000
// Предохранитель: дольше этого перебор не идёт.
#define MARK_SCAN_TOTAL_TIMEOUT_MS 180000

// Кандидаты: сначала редкая сетка 120…40 (заведомо «поздняя» искра), затем наблюдаемые
// у одностволов метки 36/34 и 17/15, в конце мелкая сетка. Ниже 10° не спускаемся: у моторов
// из целевого класса метка не бывает меньше 15°, а слишком маленький кандидат — это искра
// далеко до ВМТ.
const int MARK_SCAN_CANDIDATES[] = { 120, 110, 100, 90, 80, 70, 60, 50, 40, 36, 34, 30, 25, 20, 17, 15, 12, 10 };
#define MARK_SCAN_CANDIDATES_COUNT (sizeof(MARK_SCAN_CANDIDATES) / sizeof(MARK_SCAN_CANDIDATES[0]))

// Соседние значения вокруг найденной метки: сначала сама найденная (чтобы было с чем
// сравнивать), потом по сторонам.
const int MARK_SCAN_REFINE_OFFSETS[] = { 0, -6, -4, -2, 2, 4, 6 };
#define MARK_SCAN_REFINE_COUNT (sizeof(MARK_SCAN_REFINE_OFFSETS) / sizeof(MARK_SCAN_REFINE_OFFSETS[0]))

enum MarkScanPhase {
  MARK_SCAN_OFF = 0,
  MARK_SCAN_RUNNING = 1,
  MARK_SCAN_REFINING = 2,
  MARK_SCAN_DONE = 3,
  MARK_SCAN_CANCELLED = 4
};

bool markScanActive = false;
int markScanPhase = MARK_SCAN_OFF;
String markScanMethod = "kick";
String markScanNote = "";
int markScanIndex = 0;
int markScanRevs = 0;
int markScanRunRevs = 0;
int markScanCaught = -1;
int markScanBest = -1;
int markScanCrankingRpm = 0;
unsigned long markScanLastRevMicro = 0;
unsigned long markScanCrankSeenAt = 0;
unsigned long markScanStartedAt = 0;
unsigned long markScanLastFrameAt = 0;
// Уточнение
int markScanRefineIndex = 0;
int markScanRefineValue = 0;
int markScanRefineBest = -1;
int markScanRefineRevs = 0;
int markScanRefineMinPeriod = 0;
int markScanRefineMaxPeriod = 0;
long markScanRefineScore = 0;
long markScanBestScore = 0;

int markScanCandidate() {
  if (markScanIndex < 0 || markScanIndex >= (int) MARK_SCAN_CANDIDATES_COUNT) {
    return -1;
  }

  return MARK_SCAN_CANDIDATES[markScanIndex];
}

const char *markScanStateName() {
  switch (markScanPhase) {
    case MARK_SCAN_RUNNING:
      return "scanning";
    case MARK_SCAN_REFINING:
      return "refining";
    case MARK_SCAN_DONE:
      return "done";
    case MARK_SCAN_CANCELLED:
      return "cancelled";
    default:
      return "off";
  }
}

/** Кадр состояния: приложению нужны и прогресс, и живые обороты, и что уже найдено. */
String markScanJson() {
  String json = "{\"event\":\"mark_scan\",\"data\":{";
  json += "\"state\":\"" + String(markScanStateName()) + "\",";
  json += "\"method\":\"" + markScanMethod + "\",";
  json += "\"candidate\":" + String(markScanCandidate()) + ",";
  json += "\"attempt\":" + String(markScanIndex + 1) + ",";
  json += "\"total\":" + String(MARK_SCAN_CANDIDATES_COUNT) + ",";
  json += "\"rpm\":" + String(RPM) + ",";
  json += "\"cranking_rpm\":" + String(markScanCrankingRpm) + ",";
  json += "\"caught\":" + String(markScanCaught) + ",";
  json += "\"best\":" + String(markScanBest) + ",";
  json += "\"init_angle\":" + String(Init_angle) + ",";
  json += "\"note\":\"" + markScanNote + "\"";
  json += "}}";

  return json;
}

void markScanReport(bool force) {
  // Кадры уходят всем каналам: чаще раза в секунду не шлём, иначе канал занят.
  if (!force && millis() - markScanLastFrameAt < 1000) {
    return;
  }

  markScanLastFrameAt = millis();
  sendDeviceFrame(markScanJson());
}

/** Текущий кандидат — в память, но не в NVS: кандидатов много, флеш не для них. */
void markScanSetAngle(int value) {
  Init_angle = value;
}

/** Найденная метка — в память и в NVS: иначе она потеряется при перезагрузке. */
void markScanApply(int value) {
  markScanSetAngle(value);
  preferences.putInt("Init_angle", Init_angle);

  Serial.print("mark scan: Init_angle = ");
  Serial.println(Init_angle);
}

void markScanStartCandidates() {
  markScanIndex = 0;
  markScanRevs = 0;
  markScanRunRevs = 0;
  markScanCrankingRpm = 0;
  markScanCaught = -1;
  markScanPhase = MARK_SCAN_RUNNING;
  markScanSetAngle(markScanCandidate());
}

void markScanStart(const String &method) {
  markScanActive = true;
  markScanMethod = method.length() > 0 ? method : String("kick");
  markScanNote = "";
  markScanBest = -1;
  markScanRefineBest = -1;
  markScanRefineIndex = 0;
  markScanStartedAt = millis();
  markScanCrankSeenAt = millis();
  markScanLastRevMicro = oldRpmMicro;

  markScanStartCandidates();

  Serial.print("mark scan started, method ");
  Serial.println(markScanMethod);

  markScanReport(true);
}

void markScanStop(const char *note) {
  markScanActive = false;
  markScanPhase = MARK_SCAN_CANCELLED;
  markScanNote = note;
  markScanSetAngle(preferences.getInt("Init_angle", Init_angle));

  Serial.print("mark scan stopped: ");
  Serial.println(note);

  markScanReport(true);
}

/** Завершение: лучший угол записан в NVS, остаётся рассказать об этом приложению. */
void markScanFinish(const char *note) {
  markScanActive = false;
  markScanPhase = MARK_SCAN_DONE;
  markScanNote = note;

  if (markScanBest >= 0) {
    markScanApply(markScanBest);
  }

  Serial.print("mark scan finished: ");
  Serial.print(markScanBest);
  Serial.print(" (");
  Serial.print(note);
  Serial.println(")");

  markScanReport(true);
}

void markScanRefineBegin() {
  markScanRefineValue = markScanCaught + MARK_SCAN_REFINE_OFFSETS[markScanRefineIndex];
  markScanRefineRevs = 0;
  markScanRefineMinPeriod = 0;
  markScanRefineMaxPeriod = 0;
  markScanRefineScore = 0;

  markScanSetAngle(markScanRefineValue);
}

/** Уточнение: слушаем найденное значение и его соседей, держим самый ровный холостой ход. */
void markScanRefineStart() {
  markScanPhase = MARK_SCAN_REFINING;
  markScanRefineIndex = 0;
  markScanRefineBest = markScanCaught;
  markScanBestScore = -1;

  markScanRefineBegin();
  markScanReport(true);
}

void markScanNextRefineValue() {
  markScanRefineIndex++;

  if (markScanRefineIndex >= (int) MARK_SCAN_REFINE_COUNT) {
    markScanBest = markScanRefineBest;
    markScanFinish("метка уточнена по ровному холостому ходу");

    return;
  }

  markScanRefineBegin();
  markScanReport(true);
}

void markScanNextCandidate() {
  markScanIndex++;
  markScanRevs = 0;
  markScanRunRevs = 0;
  markScanCrankingRpm = 0;

  if (markScanIndex >= (int) MARK_SCAN_CANDIDATES_COUNT) {
    // Ни один кандидат не дал вспышки: либо метка вне сетки, либо мотор не крутят как надо.
    markScanBest = -1;
    markScanFinish("вспышки не было ни на одном значении — проверьте датчик и способ запуска");

    return;
  }

  markScanSetAngle(markScanCandidate());
  markScanReport(true);
}

/**
 * Один оборот мотора: здесь и измерение, и весь перебор.
 *
 * Вызывается из handleMoto() после handleRpm(): к этому моменту RPM и время оборота уже
 * посчитаны по свежему импульсу.
 */
void handleMarkScan() {
  if (!markScanActive) {
    return;
  }

  // Новый оборот виден по времени последнего импульса: остальные вызовы пропускаем.
  if (oldRpmMicro == markScanLastRevMicro) {
    return;
  }

  markScanLastRevMicro = oldRpmMicro;

  if (millis() - markScanStartedAt > MARK_SCAN_TOTAL_TIMEOUT_MS) {
    markScanFinish("перебор шёл слишком долго — остановил, чтобы не мучить мотор");

    return;
  }

  // Мотор не крутят: ждём владельца и не считаем это отказом.
  if (RPM < MARK_SCAN_CRANK_RPM) {
    if (millis() - markScanCrankSeenAt > MARK_SCAN_IDLE_TIMEOUT_MS) {
      markScanStop("мотор не крутят — определение остановлено");
    }

    return;
  }

  markScanCrankSeenAt = millis();

  if (RPM > markScanCrankingRpm) {
    markScanCrankingRpm = RPM;
  }

  if (RPM >= MARK_SCAN_RUN_RPM) {
    markScanRunRevs++;
  } else {
    markScanRunRevs = 0;
  }

  if (markScanPhase == MARK_SCAN_RUNNING) {
    // Вспышка: искра попала в такт сжатия — этот кандидат и есть метка.
    if (markScanRunRevs >= MARK_SCAN_RUN_REVS && markScanCaught < 0) {
      markScanCaught = markScanCandidate();
      markScanBest = markScanCaught;

      Serial.print("mark scan: caught at ");
      Serial.println(markScanCaught);

      markScanRefineStart();

      return;
    }

    markScanRevs++;

    if (markScanRevs >= MARK_SCAN_REVS_PER_CANDIDATE) {
      markScanNextCandidate();
    } else {
      markScanReport(false);
    }

    return;
  }

  if (markScanPhase == MARK_SCAN_REFINING) {
    // Мотор работает: копим разброс оборотов и оставляем самое ровное значение.
    if (RPM < MARK_SCAN_RUN_RPM) {
      markScanApply(markScanRefineBest);
      markScanBest = markScanRefineBest;
      markScanFinish("мотор заглох на уточнении — оставил прежнюю метку");

      return;
    }

    if (markScanRefineMinPeriod == 0 || timePerDegree < markScanRefineMinPeriod) {
      markScanRefineMinPeriod = timePerDegree;
    }

    if (timePerDegree > markScanRefineMaxPeriod) {
      markScanRefineMaxPeriod = timePerDegree;
    }

    markScanRefineScore = (long) markScanRefineMaxPeriod - (long) markScanRefineMinPeriod;
    markScanRefineRevs++;

    if (markScanRefineRevs >= MARK_SCAN_REFINE_REVS) {
      if (markScanBestScore < 0 || markScanRefineScore < markScanBestScore) {
        markScanBestScore = markScanRefineScore;
        markScanRefineBest = markScanRefineValue;
      }

      markScanNextRefineValue();

      return;
    }

    markScanReport(false);
  }
}

/**
 * Во время перебора опережение нулевое: искра идёт ровно в точку кандидата.
 *
 * Вызывается после handleAngle(): она пересчитывает опережение по карте каждый цикл.
 */
void markScanHoldAdvance() {
  if (!markScanActive) {
    return;
  }

  Advance_angle = 0;
}

#endif // MARK_SCAN_H
