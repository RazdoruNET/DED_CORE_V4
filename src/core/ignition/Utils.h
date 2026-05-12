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
  handleAngle();
  handleIgnition();
  handleFlash();
  //handleActiveCartSave();
}

#endif // UTILS_H
