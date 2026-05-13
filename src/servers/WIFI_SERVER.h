#include <WiFi.h>

void initWifi() 
{
  WiFi.mode(WIFI_AP);
  WiFi.softAP(APSSID1, APPSK1);
  
  // Serial.print("Setting soft-AP ... ");
  // Serial.println(APSSID1);
  // Serial.println(APPSK1);
  // Serial.println(WiFi.softAP(APSSID1, APPSK1) ? "Ready" : "Failed!");
  // Serial.print("Soft-AP IP address = ");         
  // Serial.println(WiFi.softAPIP());
}

void initRecoveryWifi() 
{
  WiFi.mode(WIFI_AP);
  
  String recovery_wifi_sid = "DED_RECOVERY";
  String recovery_wifi_password = "1234567890";

  WiFi.softAP(recovery_wifi_sid, recovery_wifi_password);
  
  // Serial.print("Setting recovery soft-AP ... ");
  // Serial.println(recovery_wifi_sid);
  // Serial.println(recovery_wifi_password);
  // Serial.println(WiFi.softAP(recovery_wifi_sid, recovery_wifi_password) ? "Ready" : "Failed!");
  // Serial.print("Recovery Soft-AP IP address = ");         
  // Serial.println(WiFi.softAPIP());
}
