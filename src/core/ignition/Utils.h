#ifndef UTILS_H
#define UTILS_H

void handleActiveCartSave()
{
  if (active_cart != preferences.getInt("active_cart", -1))
  {
    preferences.putInt("active_cart",active_cart);
  }
}

void handleMoto()
{
  handlePin();
  handleRpm();
  // Перебор метки ротора идёт по оборотам: ему нужны свежие RPM и время оборота
  handleMarkScan();
  handleAngle();
  // Во время перебора метки опережение фиксируем после карты, иначе она его перепишет
  markScanHoldAdvance();
  handleIgnition();
  handleFlash();
  //handleActiveCartSave();
}

#endif // UTILS_H
