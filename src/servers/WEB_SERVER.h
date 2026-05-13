#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <SPIFFS.h>
#include "config/PROPS.h"
#include "core/ignition/carts/MOTO_CARTS.h"
#include "WEB_SERVER_VARS.h"

class CaptiveRequestHandler : public AsyncWebHandler {
public:
  CaptiveRequestHandler() {}
  virtual ~CaptiveRequestHandler() {}

  bool canHandle(AsyncWebServerRequest *request){
    // Serial.println("CaptiveRequestHandler canHandle");
    return true;
  }

};

// Function declarations - definitions in WEB_SERVER_FUNCS.h
void initServer();
void initRecoveryServer();
    request->send(200, "text/plain", FIRMWARE_VERSION);
   
  });

  server.on("/reinit", HTTP_GET, [] (AsyncWebServerRequest *request)
  {
    INIT_SYSTEM  = true;
    
    request->send(200, "text/plain", FIRMWARE_VERSION);
  });


  server.on("/card", HTTP_GET, [] (AsyncWebServerRequest *request) 
  {
    if (request->hasParam("id")) 
    {
       active_cart = request->getParam("id")->value().toInt();
       
       // Serial.print("active_cart:"); 
       // Serial.println(active_cart);
          
    }
    
    request->send(200, "text/plain", "OK");
  });

  server.on("/update_card", HTTP_GET, [] (AsyncWebServerRequest *request) 
  {
    
    Serial.println("update_card");
    
    if (request->hasParam("id") && request->hasParam("rpm") && request->hasParam("angle") && request->hasParam("angle_adv") && request->hasParam("quick_adv")) 
    {
      int id = (request->getParam("id")->value().toInt());
      int rpm = request->getParam("rpm")->value().toInt();
      int quick_adv = request->getParam("quick_adv")->value().toInt();
      float angle = request->getParam("angle")->value().toFloat();
      float angle_adv = request->getParam("angle_adv")->value().toFloat();
      float servo = request->getParam("servo")->value().toFloat();
      
      rpmsArr[active_cart][id] = rpm;
      anglesArr[active_cart][id] = angle;
      angles_adjustmentArr[active_cart][id] = angle_adv;
      quick_adjustmentArr[active_cart][id] = quick_adv;
      servo_valueArr[active_cart][id] = servo;
      
      String x = String(active_cart);
      String y = String(id);
      
      String rpm_chars = "rpm"+x+"_"+y;
      String angles_chars = "angles"+x+"_"+y;
      String angles_adjustment_chars = "aa"+x+"_"+y;
      String quick_adjustment_chars = "qa"+x+"_"+y;
      String addres_servo_chars = "servo"+x+"_"+y;

      const char* addres_rpm = rpm_chars.c_str();
      const char* addres_angles = angles_chars.c_str();
      const char* addres_angles_adjustment = angles_adjustment_chars.c_str();
      const char* addres_quick_adjustment = quick_adjustment_chars.c_str();
      const char* addres_servo = addres_servo_chars.c_str();
      
      preferences.putInt(addres_rpm,(int) rpm * 100);
      
      Serial.print(addres_rpm);
      Serial.println(rpm);
      
      preferences.putInt(addres_angles,(int) angle * 100);

      Serial.print(addres_angles);
      Serial.println(angle);
      
      preferences.putInt(addres_angles_adjustment,(int) angle_adv * 100);

      Serial.print(addres_angles_adjustment);
      Serial.println(angle_adv);
      
      preferences.putInt(addres_quick_adjustment,(int) quick_adv * 100);

      Serial.print(addres_quick_adjustment);
      Serial.println(quick_adv);


      preferences.putInt(addres_servo,servo);

      Serial.print(addres_servo);
      Serial.println(servo);
      
      Serial.println("SAVE");
    }
  
    request->send(200, "text/plain", "OK");
  });

  server.on("/max", HTTP_GET, [] (AsyncWebServerRequest *request) 
  {
    if (request->hasParam("rpm") && request->hasParam("angle")) 
    {
       int rpm = request->getParam("rpm")->value().toInt();
       int angle = request->getParam("angle")->value().toInt();
       max_rpm = rpm;
       max_rpm_angle_kiker = angle;

       preferences.putInt("max_rpm",max_rpm);
       preferences.putInt("max_rpm_ang_kiker",max_rpm_angle_kiker);
    }
    
    request->send(200, "text/plain", "OK");
  });

  server.on("/angle/advance", HTTP_GET, [] (AsyncWebServerRequest *request) 
  {
    if (request->hasParam("status")) 
    {
       ANGLE_ADVANCE = (request->getParam("status")->value().toInt() == 1);

       preferences.putBool("ANGLE_ADV",ANGLE_ADVANCE);
    }
    request->send(200, "text/plain", "OK");
  });

  server.on("/angle", HTTP_GET, [] (AsyncWebServerRequest *request) 
  {
    if (request->hasParam("init")) 
    {
       int angle = request->getParam("init")->value().toInt();
       Init_angle = angle;
       preferences.putInt("Init_angle",Init_angle);
    }
    request->send(200, "text/plain", "OK");
  });

  server.on("/type", HTTP_GET, [] (AsyncWebServerRequest *request) 
  {
    if (request->hasParam("set")) 
    {
      type = request->getParam("set")->value().toInt();
      
      int new_chart_type = request->getParam("new_chart_type")->value().toInt();
      
      preferences.putInt("type",type);
  
      for (int c = 0; c < 3; c++)
      {
        for (int i = 0; i < 22; i++)
        {   
          switch (type) {
            case 0:
            
              switch (new_chart_type) {
                
                case 4:
                  rpmsArr[c][i] = rpmsArr_4[c][i];
                  break;

                case 6:
                  rpmsArr[c][i] = rpmsArr_3[c][i];
                  break;
  
                default:
                  rpmsArr[c][i] = rpmsArr_0[c][i];
                  break;
                  
              }
              
              break;
              
            case 1:
              
              rpmsArr[c][i] = rpmsArr_1[c][i];
              
              break;
            case 2:

              rpmsArr[c][i] = rpmsArr_2[c][i];
              
              break;
          }
        }
      }

      int angle_result = Init_angle / 2;

      if (new_chart_type == 1)
      {
        anglesArr[0][0] = anglesArr_yx125[0][0];
        anglesArr[1][0] = anglesArr_yx125[1][0];
        anglesArr[2][0] = anglesArr_yx125[2][0];
      }
      else if (new_chart_type == 2)
      {
        anglesArr[0][0] = anglesArr_yx140[0][0];
        anglesArr[1][0] = anglesArr_yx140[1][0];
        anglesArr[2][0] = anglesArr_yx140[2][0];
      }
      else if (new_chart_type == 3)
      {
        anglesArr[0][0] = anglesArr_smartcom[0][0];
        anglesArr[1][0] = anglesArr_smartcom[1][0];
        anglesArr[2][0] = anglesArr_smartcom[2][0];
      }
      else if (new_chart_type == 4)
      {
        anglesArr[0][0] = anglesArr_166fmm[0][0];
        anglesArr[1][0] = anglesArr_166fmm[1][0];
        anglesArr[2][0] = anglesArr_166fmm[2][0];
      }
      else if (new_chart_type == 5)
      {
        anglesArr[0][0] = anglesArr_zs190[0][0];
        anglesArr[1][0] = anglesArr_zs190[1][0];
        anglesArr[2][0] = anglesArr_zs190[2][0];
      }
      else if (new_chart_type == 6)
      {
        anglesArr[0][0] = anglesArr_175fmm[0][0];
        anglesArr[1][0] = anglesArr_175fmm[1][0];
        anglesArr[2][0] = anglesArr_175fmm[2][0];
      }
      else
      {
        anglesArr[0][0] = angle_result - 2;
        anglesArr[1][0] = angle_result - 2;
        anglesArr[2][0] = angle_result - 2;
  
        angles_adjustmentArr[0][0] = 0;
        angles_adjustmentArr[1][0] = 0;
        angles_adjustmentArr[2][0] = 0;
  
        servo_valueArr[0][0] = 0;
        servo_valueArr[1][0] = 0;
        servo_valueArr[2][0] = 0;
  
        quick_adjustmentArr[0][0] = 0;
        quick_adjustmentArr[1][0] = 0;
        quick_adjustmentArr[2][0] = 0;
      }
      
      for (int i = 1; i < 22; i++)
      {
        if (new_chart_type == 1)
        {
          anglesArr[0][i] = anglesArr_yx125[0][i];
          anglesArr[1][i] = anglesArr_yx125[1][i];
          anglesArr[2][i] = anglesArr_yx125[2][i];
        }
        else if (new_chart_type == 2)
        {
          anglesArr[0][i] = anglesArr_yx140[0][i];
          anglesArr[1][i] = anglesArr_yx140[1][i];
          anglesArr[2][i] = anglesArr_yx140[2][i];
        }
        else if (new_chart_type == 3)
        {
          anglesArr[0][i] = anglesArr_smartcom[0][i];
          anglesArr[1][i] = anglesArr_smartcom[1][i];
          anglesArr[2][i] = anglesArr_smartcom[2][i];
        }
        else if (new_chart_type == 4)
        {
          anglesArr[0][i] = anglesArr_172fmm[0][i];
          anglesArr[1][i] = anglesArr_172fmm[1][i];
          anglesArr[2][i] = anglesArr_172fmm[2][i];
        }
        else if (new_chart_type == 5)
        {
          anglesArr[0][i] = anglesArr_zs190[0][i];
          anglesArr[1][i] = anglesArr_zs190[1][i];
          anglesArr[2][i] = anglesArr_zs190[2][i];
        }
        else if (new_chart_type == 6)
        {
          anglesArr[0][i] = anglesArr_175fmm[0][i];
          anglesArr[1][i] = anglesArr_175fmm[1][i];
          anglesArr[2][i] = anglesArr_175fmm[2][i];
        }
        else
        {
          anglesArr[0][i] = angle_result + 1;
          anglesArr[1][i] = angle_result + 1;
          anglesArr[2][i] = angle_result + 1;
  
          angles_adjustmentArr[0][i] = 0;
          angles_adjustmentArr[1][i] = 0;
          angles_adjustmentArr[2][i] = 0;
    
          servo_valueArr[0][i] = 0;
          servo_valueArr[1][i] = 0;
          servo_valueArr[2][i] = 0;
    
          quick_adjustmentArr[0][i] = 0;
          quick_adjustmentArr[1][i] = 0;
          quick_adjustmentArr[2][i] = 0;
        }
      }
      
      for (int c = 0; c < 3; c++)
      {
        for (int id = 0; id < 22; id++)
        {       
          String x = String(c);
          String y = String(id);
          
          String rpm_chars = "rpm"+x+"_"+y;
          String angles_chars = "angles"+x+"_"+y;
          String angles_adjustment_chars = "aa"+x+"_"+y;
          String quick_adjustment_chars = "qa"+x+"_"+y;
          String addres_servo_chars = "servo"+x+"_"+y;
          
          const char* addres_rpm = rpm_chars.c_str();
          const char* addres_angles = angles_chars.c_str();
          const char* addres_angles_adjustment = angles_adjustment_chars.c_str();
          const char* addres_quick_adjustment = quick_adjustment_chars.c_str();
          const char* addres_servo = addres_servo_chars.c_str();
          
          Serial.println(addres_rpm);
          Serial.println(addres_angles);
          
          preferences.putInt(addres_rpm,              (int)(rpmsArr[c][id]             *100));
          preferences.putInt(addres_angles,           (int)(anglesArr[c][id]           *100));
          preferences.putInt(addres_angles_adjustment,(int)(angles_adjustmentArr[c][id]*100));
          preferences.putInt(addres_quick_adjustment, (int)(quick_adjustmentArr[c][id] *100));
          
          preferences.putInt(addres_servo,            servo_valueArr[c][id]);
          
          
        }
      }

      switch (new_chart_type)
      {
        case 1:
          Init_angle = 35;
        break;
        case 2:
          Init_angle = 35;
        break;
        case 3:
          Init_angle = 43;
        break;
        case 4:
          Init_angle = 35;
        break;
        case 5:
          Init_angle = 17;
        break;
        case 6:
          Init_angle = 80;
        break;
      }

      preferences.putInt("Init_angle",Init_angle);
    }
     
    request->send(200, "text/plain", "OK");
  });

  server.on("/flash", HTTP_GET, [] (AsyncWebServerRequest *request) 
  {
    AFTER_REBOOT = false;
    SAVE_CONFIG = true;
    request->send(200, "text/plain", "OK");
  });

  server.on("/wifi/updator", HTTP_GET, [] (AsyncWebServerRequest *request) 
  {
    if (request->hasParam("ssid")) 
    {
       APSSID = request->getParam("ssid")->value();
       APPSK = request->getParam("appsk")->value();
       preferences.putString("APSSID",APSSID);
       preferences.putString("APPSK",APPSK);
    }
    request->send(200, "text/plain", "OK");
    
  });
  
  server.on("/wifi", HTTP_GET, [] (AsyncWebServerRequest *request) 
  {
    if (request->hasParam("ssid")) 
    {
       APSSID1 = request->getParam("ssid")->value();
       APPSK1 = request->getParam("appsk")->value();
       preferences.putString("APSSID1",APSSID1);
       preferences.putString("APPSK1",APPSK1);
    }
    request->send(200, "text/plain", "OK");
  });

  server.on("/quickshifter", HTTP_GET, [] (AsyncWebServerRequest *request) 
  {
    if (request->hasParam("status")) 
    {
       QUICKSHIFTER = (request->getParam("status")->value().toInt() == 1);
       preferences.putBool("QUICKSHIFT",QUICKSHIFTER);
    }
    request->send(200, "text/plain", "OK");
  });
  
  server.on("/modulator", HTTP_GET, [] (AsyncWebServerRequest *request) 
  {
    if (request->hasParam("time")) 
    {
      Init_flash_time = request->getParam("time")->value().toInt();
      preferences.putInt("flash_time",Init_flash_time);
    }
    else if (request->hasParam("filter")) 
    {
      filter = request->getParam("filter")->value().toInt();
      preferences.putInt("filter",filter);
    }
    else if (request->hasParam("high")) 
    {
      HIGH_OUTPUT = (request->getParam("high")->value().toInt() == 1);
      preferences.putInt("HIGH_OUTPUT",HIGH_OUTPUT);
    }
    else
    {
      request->send(404, "text/plain", "404");
    }

    request->send(200, "text/plain", "OK");
  });

  server.on("/sensor", HTTP_GET, [] (AsyncWebServerRequest *request) 
  {
    if (request->hasParam("invert")) 
    {
       INVERT_SENSOR = (request->getParam("invert")->value().toInt() == 1);
       preferences.putBool("INVERT_SENS",INVERT_SENSOR);
    }
    request->send(200, "text/plain", "OK");
  });

  server.on("/carts/switching", HTTP_GET, [] (AsyncWebServerRequest *request) 
  {
    if (request->hasParam("status")) 
    {
       CARTS_SWHITCHING = (request->getParam("status")->value().toInt() == 1);
       preferences.putBool("CART_SWHITCH",CARTS_SWHITCHING);
    }
    request->send(200, "text/plain", "OK");
  });

  server.on("/servo", HTTP_GET, [] (AsyncWebServerRequest *request) 
  {
    if (request->hasParam("status")) 
    {
       SERVO = (request->getParam("status")->value().toInt() == 1);
       preferences.putBool("SERVO",SERVO);
   
       Servo_type = request->getParam("servo_type")->value().toInt();
       preferences.putInt("servo_type",Servo_type);
   
       Servo_rpm = request->getParam("servo_rpm")->value().toInt();
       preferences.putInt("servo_rpm",Servo_rpm);
    
       Servo_percent = request->getParam("servo_percent")->value().toInt();
       preferences.putInt("servo_perc",Servo_percent);
    }
    
    request->send(200, "text/plain", "OK");
  });

  server.on("/servo_test", HTTP_GET, [] (AsyncWebServerRequest *request) 
  {
    if (request->hasParam("status")) 
    {
       SERVO = (request->getParam("status")->value().toInt() == 1);
       preferences.putBool("SERVO",SERVO);
    }
    request->send(200, "text/plain", "OK");
  });

  server.on("/throttle", HTTP_GET, [] (AsyncWebServerRequest *request) 
  {
    if (request->hasParam("invert")) 
    {
       INVERT_THROTTLE = (request->getParam("invert")->value().toInt() == 1);
       preferences.putBool("INVERT_THROT",INVERT_THROTTLE);
    }
    request->send(200, "text/plain", "OK");
  });
 
  server.onNotFound([](AsyncWebServerRequest * request) {
    request->redirect("/");
  });
  
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  
  dnsServer.start(53, "*", WiFi.softAPIP());
  server.addHandler(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER);
  
  server.begin();

  // Serial.println("HTTP server started");
}

void initRecoveryServer() 
{
  // Serial.println("CORE");
  // Serial.println(xPortGetCoreID());

  server.on("/", HTTP_GET, [] (AsyncWebServerRequest *request)
  {
    request->redirect("/update");
  });

  server.on("/wifi/ssid", HTTP_GET, [] (AsyncWebServerRequest *request)
  {
    request->send(200, "text/plain", APSSID1);
  });

  server.on("/wifi/pass", HTTP_GET, [] (AsyncWebServerRequest *request)
  {
    request->send(200, "text/plain", APPSK1);
  });

  server.on("/core/reboot", HTTP_GET, [] (AsyncWebServerRequest *request)
  {
    request->send(200, "text/plain", "REBOOT");
    ESP.restart();
  });
  
  server.on("/core/version", HTTP_GET, [] (AsyncWebServerRequest *request)
  {
    request->send(200, "text/plain", FIRMWARE_VERSION);
   
  });

  server.on("/reinit", HTTP_GET, [] (AsyncWebServerRequest *request)
  {
    INIT_SYSTEM  = true;
    AFTER_REBOOT = true;
    
    request->send(200, "text/plain", FIRMWARE_VERSION);
  });

 
  server.onNotFound([](AsyncWebServerRequest * request) {
    request->send(404, "text/plain", "NOT FOUND ROUTE");
  });
  
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  
  dnsServer.start(53, "*", WiFi.softAPIP());
  server.addHandler(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER);
  
  server.begin();

  // Serial.println("HTTP server started");
}
