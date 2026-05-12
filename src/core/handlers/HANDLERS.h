
#ifndef HANDLERS_H
#define HANDLERS_H

#include "QuickShifter.h"
#include "CartSelection.h"
#include "Alarm.h"

void handleControllPin() 
{
  handleQuickShifterControllPin();

  if (SOUND == false)
  {
    handleSelectCartUpControllPin();
    handleSelectCartDownControllPin();
  }
  else
  {
    handleSelectCartUpControllRevertPin();
  }
}

#endif // HANDLERS_H
