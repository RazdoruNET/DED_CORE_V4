void beep(int count, int delay_time, int high_time, int tone_value)
{
  for (int i = 0; i < count; i++)
  {
    tone(select_cart_up_pin, tone_value, high_time);
    tone(select_cart_up_pin, 0, delay_time);
  }
}

void beepCart(int active_cart)
{
  if (active_cart == 0)
  {
    beep(1, 100, 250, 440);
  }
  else if (active_cart == 1)
  {
    beep(2, 100, 150, 392);
  }
  else if (active_cart == 2)
  {
    beep(3, 100, 150, 293.7);
  }
}
