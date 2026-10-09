#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <SPIFFS.h>
#include "config/PROPS.h"
#include "core/ignition/carts/MOTO_CARTS.h"
#include "WEB_SERVER_VARS.h"
#include "core/ota/HYBRID_OTA.h"

extern HybridOTAManager otaManager;

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

  server.on("/wifi/auto", HTTP_GET, [] (AsyncWebServerRequest *request) 
  {
    if (request->hasParam("ssid")) {
      WIFI_UPDATE_SSID = request->getParam("ssid")->value();
      preferences.putString("WIFI_UPDATE_SSID", WIFI_UPDATE_SSID);
    }

    if (request->hasParam("pass")) {
      WIFI_UPDATE_PASSWORD = request->getParam("pass")->value();
      preferences.putString("WIFI_UPDATE_PASSWORD", WIFI_UPDATE_PASSWORD);
    }

    if (request->hasParam("enabled")) {
      WIFI_AUTO_UPDATE_ENABLED = (request->getParam("enabled")->value().toInt() == 1);
      preferences.putBool("WIFI_AUTO_UPDATE_ENABLED", WIFI_AUTO_UPDATE_ENABLED);
    }

    request->send(200, "application/json", "{\"enabled\":" + String(WIFI_AUTO_UPDATE_ENABLED ? "true" : "false") + ",\"ssid\":\"" + WIFI_UPDATE_SSID + "\",\"status\":\"" + WIFI_UPDATE_STATUS + "\",\"pass_set\":" + String(WIFI_UPDATE_PASSWORD.length() > 0 ? "true" : "false") + ",\"connected\":" + String(WiFi.status() == WL_CONNECTED && WiFi.SSID() == WIFI_UPDATE_SSID ? "true" : "false") + ",\"last_check_time\":" + String(WIFI_LAST_CHECK_TIME) + ",\"last_attempt_time\":" + String(WIFI_LAST_ATTEMPT_TIME) + ",\"retry_interval_ms\":" + String(WIFI_RETRY_INTERVAL_MS) + ",\"next_allowed_check_ms\":" + String((WIFI_LAST_ATTEMPT_TIME==0)?0:((WIFI_LAST_ATTEMPT_TIME+WIFI_RETRY_INTERVAL_MS)-millis())) + "}");
  });

  // Device status endpoint
  server.on("/status", HTTP_GET, [] (AsyncWebServerRequest *request) {
    bool sta_connected = (WiFi.status() == WL_CONNECTED && WiFi.SSID() == WIFI_UPDATE_SSID);
    FirmwareInfo info = otaManager.getLastFirmwareInfo();

    String response = "{";
    response += "\"wifi\":{";
    response += "\"mode\":\"" + String((WiFi.getMode() == WIFI_MODE_AP) ? "ap" : (WiFi.getMode() == WIFI_MODE_STA ? "sta" : "ap_sta")) + "\",";
    response += "\"ap_ssid\":\"" + APSSID1 + "\",";
    response += "\"sta_ssid\":\"" + WIFI_UPDATE_SSID + "\",";
    response += "\"sta_connected\":" + String(sta_connected ? "true" : "false") + ",";
    response += "\"status\":\"" + WIFI_UPDATE_STATUS + "\",";
    response += "\"last_check_time\":" + String(WIFI_LAST_CHECK_TIME) + ",";
    response += "\"last_attempt_time\":" + String(WIFI_LAST_ATTEMPT_TIME) + ",";
    unsigned long next_check = 0;
    if (WIFI_LAST_ATTEMPT_TIME != 0) next_check = (WIFI_LAST_ATTEMPT_TIME + WIFI_RETRY_INTERVAL_MS > millis()) ? (WIFI_LAST_ATTEMPT_TIME + WIFI_RETRY_INTERVAL_MS - millis()) : 0;
    response += "\"next_check_ms\":" + String(next_check);
    response += "},";

    response += "\"ota\":{";
    response += "\"currentVersion\":\"" + otaManager.getCurrentVersion() + "\",";
    response += "\"mode\":\"" + otaManager.getModeString() + "\",";
    response += "\"initialized\":" + String(otaManager.isInitialized() ? "true" : "false") + ",";
    response += "\"updateMode\":" + String(UPDATE_MODE ? "true" : "false") + ",";
    bool updateAvailable = otaManager.getLastFirmwareInfo().updateAvailable;
    response += "\"updateAvailable\":" + String(updateAvailable ? "true" : "false") + ",";
    response += "\"lastFirmwareInfo\":{";
    response += "\"version\":\"" + info.version + "\",";
    response += "\"size\":\"" + String(info.size) + "\",";
    response += "\"md5\":\"" + info.md5 + "\",";
    response += "\"changelog\":\"" + info.changelog + "\"";
    response += "}";
    response += "},";

    response += "\"motor\":{";
    response += "\"rpm\":" + String(RPM) + ",";
    response += "\"throttle\":" + String(Throttle) + ",";
    response += "\"motor_running\":" + String(RPM > 0 ? "true" : "false");
    response += "},";

    response += "\"config\":{";
    response += "\"autoUpdateEnabled\":" + String(WIFI_AUTO_UPDATE_ENABLED ? "true" : "false") + ",";
    response += "\"wifiUpdateSSID\":\"" + WIFI_UPDATE_SSID + "\",";
    response += "\"wifiPasswordSet\":" + String(WIFI_UPDATE_PASSWORD.length() > 0 ? "true" : "false") + ",";
    response += "\"wifiConnected\":" + String(sta_connected ? "true" : "false") + ",";
    response += "\"retryIntervalMs\":" + String(WIFI_RETRY_INTERVAL_MS) + ",";
    response += "\"nextCheckMs\":" + String(next_check);
    response += "}";

    response += "}";

    request->send(200, "application/json", response);
  });

  // Cart type metadata endpoint
  server.on("/cart/types", HTTP_GET, [] (AsyncWebServerRequest *request) {
    String resp = "{";
    resp += "\"types\":[";
    resp += "{\"id\":0,\"label\":\"7-point default\",\"points\":7},";
    resp += "{\"id\":1,\"label\":\"12-point medium\",\"points\":12},";
    resp += "{\"id\":2,\"label\":\"22-point large\",\"points\":22}";
    resp += "]}";
    request->send(200, "application/json", resp);
  });

  // Device configuration endpoint
  server.on("/device/config", HTTP_GET, [] (AsyncWebServerRequest *request) {
    String resp = "{";
    resp += "\"activeCart\":" + String(active_cart) + ",";
    resp += "\"type\":" + String(type) + ",";
    resp += "\"autoUpdateEnabled\":" + String(WIFI_AUTO_UPDATE_ENABLED ? "true" : "false") + ",";
    resp += "\"wifiUpdateSSID\":\"" + WIFI_UPDATE_SSID + "\",";
    resp += "\"wifiPasswordSet\":" + String(WIFI_UPDATE_PASSWORD.length() > 0 ? "true" : "false") + ",";
    resp += "\"quickShifterEnabled\":" + String(QUICKSHIFTER ? "true" : "false") + ",";
    resp += "\"angleAdvance\":" + String(ANGLE_ADVANCE ? "true" : "false") + ",";
    resp += "\"servoEnabled\":" + String(SERVO ? "true" : "false") + ",";
    resp += "\"servoType\":" + String(Servo_type) + ",";
    resp += "\"servoRpm\":" + String(Servo_rpm) + ",";
    resp += "\"servoPercent\":" + String(Servo_percent) + ",";
    resp += "\"invertSensor\":" + String(INVERT_SENSOR ? "true" : "false") + ",";
    resp += "\"invertThrottle\":" + String(INVERT_THROTTLE ? "true" : "false") + ",";
    resp += "\"maxRpm\":" + String(max_rpm) + ",";
    resp += "\"maxRpmAngleKicker\":" + String(max_rpm_angle_kiker);
    resp += "}";
    request->send(200, "application/json", resp);
  });

  // WiFi scan endpoint
  server.on("/wifi/scan", HTTP_GET, [] (AsyncWebServerRequest *request) {
    int n = WiFi.scanNetworks();
    String resp = "{";
    resp += "\"networks\": [";
    for (int i = 0; i < n; ++i) {
      resp += "{";
      resp += "\"ssid\":\"" + WiFi.SSID(i) + "\",";
      resp += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
      resp += "\"secure\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false") + ",";
      resp += "\"channel\":" + String(WiFi.channel(i));
      resp += "}";
      if (i < n-1) resp += ",";
    }
    resp += "]}";
    request->send(200, "application/json", resp);
  });

  // Manual connect endpoint
  server.on("/wifi/connect", HTTP_GET, [] (AsyncWebServerRequest *request) {
    if (request->hasParam("ssid")) {
      WIFI_UPDATE_SSID = request->getParam("ssid")->value();
      preferences.putString("WIFI_UPDATE_SSID", WIFI_UPDATE_SSID);
    }
    if (request->hasParam("pass")) {
      WIFI_UPDATE_PASSWORD = request->getParam("pass")->value();
      preferences.putString("WIFI_UPDATE_PASSWORD", WIFI_UPDATE_PASSWORD);
    }

    bool started = false;
    bool connected = false;
    if (WIFI_UPDATE_SSID.length() > 0) {
      started = true;
      WiFi.begin(WIFI_UPDATE_SSID.c_str(), WIFI_UPDATE_PASSWORD.c_str());
      unsigned long start = millis();
      while (millis() - start < 15000) {
        if (WiFi.status() == WL_CONNECTED) { connected = true; break; }
        delay(100);
      }
    }

    String out = "{";
    out += "\"started\":" + String(started ? "true" : "false") + ",";
    out += "\"target_ssid\":\"" + WIFI_UPDATE_SSID + "\",";
    out += "\"status\":\"" + String(connected ? "connected" : "connect_failed") + "\",";
    out += "\"passwordSet\":" + String(WIFI_UPDATE_PASSWORD.length() > 0 ? "true" : "false") + ",";
    out += "\"connected\":" + String((WiFi.status() == WL_CONNECTED && WiFi.SSID() == WIFI_UPDATE_SSID) ? "true" : "false") + "";
    out += "}";
    request->send(200, "application/json", out);
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

  // OTA Endpoints
  server.on("/ota/check", HTTP_GET, [] (AsyncWebServerRequest *request) {
    bool updateAvailable = otaManager.checkForUpdate();
    FirmwareInfo info = otaManager.getLastFirmwareInfo();
    
    String response = "{";
    response += "\"updateAvailable\":" + String(updateAvailable ? "true" : "false") + ",";
    response += "\"currentVersion\":\"" + otaManager.getCurrentVersion() + "\",";
    response += "\"mode\":\"" + otaManager.getModeString() + "\"";
    
    if (updateAvailable) {
      response += ",\"newVersion\":\"" + info.version + "\",";
      response += "\"size\":" + String(info.size) + ",";
      response += "\"md5\":\"" + info.md5 + "\",";
      response += "\"changelog\":\"" + info.changelog + "\"";
    }
    
    response += "}";
    request->send(200, "application/json", response);
  });

  server.on("/ota/status", HTTP_GET, [] (AsyncWebServerRequest *request) {
    FirmwareInfo info = otaManager.getLastFirmwareInfo();
    bool updateAvailable = info.updateAvailable;
    bool connected = (WiFi.status() == WL_CONNECTED && WiFi.SSID() == WIFI_UPDATE_SSID);

    String response = "{";
    response += "\"currentVersion\":\"" + otaManager.getCurrentVersion() + "\",";
    response += "\"mode\":\"" + otaManager.getModeString() + "\",";
    response += "\"initialized\":" + String(otaManager.isInitialized() ? "true" : "false") + ",";
    response += "\"updateMode\":" + String(UPDATE_MODE ? "true" : "false") + ",";
    response += "\"updateAvailable\":" + String(updateAvailable ? "true" : "false") + ",";
    response += "\"lastFirmwareInfo\":{";
    response += "\"version\":\"" + info.version + "\",";
    response += "\"size\":" + String(info.size) + ",";
    response += "\"md5\":\"" + info.md5 + "\",";
    response += "\"changelog\":\"" + info.changelog + "\"";
    response += "},";
    response += "\"wifiAutoUpdateEnabled\":" + String(WIFI_AUTO_UPDATE_ENABLED ? "true" : "false") + ",";
    response += "\"wifiUpdateSSID\":\"" + WIFI_UPDATE_SSID + "\",";
    response += "\"wifiConnected\":" + String(connected ? "true" : "false") + ",";
    response += "\"lastCheckTime\":" + String(WIFI_LAST_CHECK_TIME) + ",";
    response += "\"lastAttemptTime\":" + String(WIFI_LAST_ATTEMPT_TIME) + ",";
    response += "\"retryIntervalMs\":" + String(WIFI_RETRY_INTERVAL_MS);
    response += "}";
    request->send(200, "application/json", response);
  });

  server.on("/ota/update", HTTP_POST, [] (AsyncWebServerRequest *request) {
    bool success = otaManager.performUpdate();
    
    if (success) {
      request->send(200, "text/plain", "Update initiated, rebooting...");
    } else {
      request->send(500, "text/plain", "Update failed");
    }
  });

  server.on("/ota/mode", HTTP_POST, [] (AsyncWebServerRequest *request) {
    if (request->hasParam("mode")) {
      int mode = request->getParam("mode")->value().toInt();
      OTA_MODE = mode;
      preferences.putInt("OTA_MODE", OTA_MODE);
      
      OtaMode otaMode = static_cast<OtaMode>(mode);
      otaManager.setMode(otaMode);
      
      request->send(200, "application/json", "{\"mode\":\"" + otaManager.getModeString() + "\"}");
    } else {
      request->send(400, "text/plain", "Missing mode parameter");
    }
  });

  server.on("/ota/password", HTTP_POST, [] (AsyncWebServerRequest *request) {
    if (request->hasParam("password")) {
      String password = request->getParam("password")->value();
      OTA_PASSWORD = password;
      preferences.putString("OTA_PASSWORD", OTA_PASSWORD);
      otaManager.setArduinoOTAPassword(password);
      
      request->send(200, "text/plain", "OK");
    } else {
      request->send(400, "text/plain", "Missing password parameter");
    }
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
