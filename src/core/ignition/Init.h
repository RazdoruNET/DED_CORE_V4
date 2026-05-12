#ifndef INIT_H
#define INIT_H

void initMoto()
{
  pinMode(sensor_pin, INPUT);   // ДАТЧИК
  pinMode(throttle_pin, INPUT);   // ДАТЧИК
  pinMode(quickshifter_pin, INPUT);   // ДАТЧИК
  pinMode(out_pin, OUTPUT);  // ВЫХОД НА КОМУТАТОР
  
  gpio_set_pull_mode(GPIO_NUM_17, GPIO_PULLUP_ONLY);
  
  if (SOUND == false)
  {
    pinMode(select_cart_up_pin, INPUT);
    pinMode(select_cart_down_pin, INPUT);

    gpio_set_pull_mode(GPIO_NUM_32, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(GPIO_NUM_33, GPIO_PULLUP_ONLY);
  
  }
  else
  {
    pinMode(select_cart_up_pin, OUTPUT);
    pinMode(select_cart_down_pin, INPUT);

    gpio_set_pull_mode(GPIO_NUM_33, GPIO_PULLUP_ONLY);
  }
}

#endif // INIT_H
