#include <ArduinoJson.h>
#include <WebSocketsServer.h>
#include "config/PROPS.h"

// WebSocket variable declarations - definitions in WEB_SOCKET_VARS.h
extern int socet_flash;
extern int pass;
extern int userIndex;
extern int modul;
extern WebSocketsServer webSocket;

void onEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) 
{
  // Serial.println("ON_EVENT_WEBSOCKET");
  switch (type) 
  {
    case WStype_DISCONNECTED:
      Serial.println("WebSocket disconnected");
      break;
    case WStype_CONNECTED:
      Serial.println("WebSocket connected");
      break;
    case WStype_BIN:
      Serial.println("WebSocket binar requeest");
      break;
    case WStype_TEXT:
      //Serial.println("WebSocket text request");

      String dataStr = String((char*)payload);

      dataStr.replace("\\","");
      dataStr.replace("}\"","}");
      dataStr.replace("\"{","{");
      
      StaticJsonBuffer<5000> jsonBufferWS;
        
      JsonObject& jsonWS = jsonBufferWS.parseObject(dataStr);
         
      if (jsonWS.success()) 
      {  
        // Serial.println(dataStr);
        
        if (jsonWS["event"] == "cart")
        {
          Serial.println("[event] == cart");
            
          int resp_active_cart = (int) jsonWS["data"]["cart"];
        
          if (resp_active_cart > 0 && resp_active_cart <= 3)
          {
            active_cart = (resp_active_cart - 1);
            preferences.putInt("active_cart", active_cart);

            Serial.print("active_cart");
            Serial.println(active_cart);
          }
        }

        if (jsonWS["event"] == "ping")
        {
          Serial.println("[event] == ping");
          
          String body = "";
          
          JsonObject& json = jsonBufferWS.createObject();
          
          json["event"] = "pong";
          json.printTo(body);
          
          webSocket.sendTXT(num,body);
        }

        if (jsonWS["event"] == "servo_error")
        {
          Serial.println("servo_error");
          Serial.println(num);

          Servo_error = 1;
        }
      }
      else
      {
          Serial.println(dataStr);
      }
      
      break;
  };
}

void initWebSocket() 
{
  webSocket.begin();
  webSocket.onEvent(onEvent);
}

String getDataJson() 
{
 
  String body = "";

  StaticJsonBuffer<5048> jsonBuffer;
  
  JsonObject& json = jsonBuffer.createObject();
  JsonObject& data = jsonBuffer.createObject();
  
  json["event"] = "message";
  data["micros"] = micros();
  data["select_cart"] = select_cart;
  data["select_qs"] = select_qs;
  data["aa"] = ANGLE_ADVANCE;
  data["qs"] = QUICKSHIFTER;
  data["cc"] = CARTS_SWHITCHING;
  data["modulator"] = modul;
  data["max_rpm"] = max_rpm;
  data["max_rpm_angle_kiker"] = max_rpm_angle_kiker;
  data["advance_angle"] = Advance_angle;
  data["rpm"] = RPM;
  data["termo"] = Termo;
  data["throttle"] = Throttle;
  data["init_angle"] = Init_angle;
  data["filter_val"] = filter;
  data["high"] = (HIGH_OUTPUT) ? 1 : 0;
  data["init_flash_time"] = Init_flash_time;
  data["cpu_termo"] = (temprature_sens_read() - 32) / 1.8;
  data["card"] = active_cart;
  data["wsid"] = APSSID1;
  data["wpass"] = APPSK1;
  data["INVERT_SENSOR"] = INVERT_SENSOR;
  data["INVERT_THROTTLE"] = INVERT_THROTTLE;
  data["servo"] = SERVO;
  data["servo_position"] = (SERVO) ? Servo_position : 0;
  data["servo_type"] = Servo_type;
  data["servo_rpm"] = Servo_rpm;
  data["servo_percent"] = Servo_percent;
  data["type"] = type;
  data["wifi_auto_update_enabled"] = WIFI_AUTO_UPDATE_ENABLED ? 1 : 0;
  data["wifi_update_ssid"] = WIFI_UPDATE_SSID;
  data["wifi_update_status"] = WIFI_UPDATE_STATUS;
  data["wifi_last_check_time"] = WIFI_LAST_CHECK_TIME;
  data["wifi_last_attempt_time"] = WIFI_LAST_ATTEMPT_TIME;
  data["wifi_connected"] = (WiFi.status() == WL_CONNECTED && WiFi.SSID() == WIFI_UPDATE_SSID) ? 1 : 0;
  data["ota_update_available"] = otaManager.getLastFirmwareInfo().updateAvailable ? 1 : 0;
  data["ota_last_version"] = otaManager.getLastFirmwareInfo().version;
  data["motor_running"] = (RPM > 0) ? 1 : 0;
  
  JsonArray& rpm_array = jsonBuffer.createArray();
  JsonArray& servo_array = jsonBuffer.createArray();

  int count = (type == 0) ? 7 : (type == 1) ? 12 : 22;
  
  for (byte i = 0; i < count; i++) 
  {
    JsonArray& rpm_item = jsonBuffer.createArray();
    
    rpm_item.add(rpmsArr[active_cart][i]);
    rpm_item.add(anglesArr[active_cart][i]);
    rpm_item.add(angles_adjustmentArr[active_cart][i]);
    rpm_item.add(quick_adjustmentArr[active_cart][i]);
    rpm_item.add(servo_valueArr[active_cart][i]);
    
    rpm_array.add(rpm_item);
  }

  data["angles"] = rpm_array;

  json["data"] = data;
  
  json.printTo(body);
  
  return body;
}

void send_data()
{
  String json = getDataJson();
    
  for(int i = 0; i <= 100; i++)
  {
    webSocket.sendTXT(i,json);
  }
 
  modul = 0;
}

void handleWebSocket() 
{
    webSocket.loop();
}

void handelWebSocketData()
{
  if (Modulator > 0)
  {
    modul = Modulator;
    socet_flash = Modulator;
  }

  if (RPM > 0)
  {
    modul = 1;
  }
  
  if (pass > 150)
  {
    send_data();
    socet_flash = 0;
    pass = 0;
  }
  else
  {
    pass++;
  }
}
