#ifndef CARTSELECTION_H
#define CARTSELECTION_H

void handleSelectCartUpControllRevertPin()
{
  int count_click = 0;
  
  for(int i=0; i<15; i++)
  {
    int select_cart_down  = digitalRead(select_cart_down_pin);

    if (select_cart_down == LOW)
    {
      count_click++;
    }
    else
    {
      count_click = 0;
    }

    vTaskDelay(1);
  }
  
  if (count_click > 5)
  {
    if (select_cart_in_down_proces != 0)
    {
      if (active_cart < 2)
      {
        active_cart = active_cart + 1;    
      }
      else if (CARTS_SWHITCHING == true && active_cart >= 2)
      {
        active_cart = 0;
      }

      beepCart(active_cart);
    }
    
    select_cart_in_down_proces = 0;
  } 
  else if (count_click == 0)
  {
    select_cart_in_down_proces = 1;
  }
}

void handleSelectCartUpControllPin() 
{
  int count_click = 0;
  
  for(int i=0; i<15; i++)
  {
    int select_cart_up  = digitalRead(select_cart_up_pin);

    if (select_cart_up == LOW)
    {
      count_click++;
    }
    else
    {
      count_click = 0;
    }

    vTaskDelay(1);
  }
  
  if (count_click > 5)
  {
    if (select_cart_in_up_proces != 0)
    {
      if (active_cart < 2)
      {
        active_cart = active_cart + 1;    
      }
      else if (CARTS_SWHITCHING == true && active_cart >= 2)
      {
        active_cart = 0;
      }
    }
    
    select_cart_in_up_proces = 0;
  } 
  else if (count_click == 0)
  {
    select_cart_in_up_proces = 1;
  }
} 

void handleSelectCartDownControllPin() 
{
  int count_click = 0;
  
  for(int i=0; i<15; i++)
  {
    int select_cart_down  = digitalRead(select_cart_down_pin);

    if (select_cart_down == LOW)
    {
      count_click++;
    }
    else
    {
      count_click = 0;
    }

    vTaskDelay(1);
  }

  if (count_click > 5)
  {
    if (select_cart_in_down_proces != 0)
    {
      if (active_cart > 0)
      {
        active_cart = active_cart - 1;
      }
      else if (CARTS_SWHITCHING == true && active_cart >= 0)
      {
        active_cart = 2;
      }
    }

    select_cart_in_down_proces = 0;
  } 
  else if (count_click == 0)
  {
    select_cart_in_down_proces = 1;
  }
} 

#endif // CARTSELECTION_H
