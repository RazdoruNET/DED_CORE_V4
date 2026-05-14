#define ARRAY_SIZE(x) sizeof(x)/sizeof(x[0])

bool saveConfig() {
  return true;
}

boolean loadConfig() {

  String rpm_chars = "rpm";
  String angles_chars = "angles";
  String angles_adjustment_chars = "aa";
  String quick_adjustment_chars = "qa";

  initSPIFFS = preferences.getBool("initSPIFFS", initSPIFFS);

  active_cart = preferences.getInt("active_cart", active_cart);

  type = preferences.getInt("type",type);
  max_rpm_angle_kiker = preferences.getInt("max_rpm_ang_k",max_rpm_angle_kiker);
  max_rpm = preferences.getInt("max_rpm",max_rpm);
  filter = preferences.getInt("filter",filter);
    
  Init_angle = preferences.getInt("Init_angle",Init_angle);
  Init_flash_time =  preferences.getInt("flash_time",Init_flash_time);
  
  HIGH_OUTPUT = preferences.getBool("HIGH_OUTPUT",HIGH_OUTPUT);
  ANGLE_ADVANCE = preferences.getBool("ANGLE_ADV",ANGLE_ADVANCE);
  QUICKSHIFTER = preferences.getBool("QUICKSHIFT",QUICKSHIFTER);
  INVERT_SENSOR = preferences.getBool("INVERT_SENS",INVERT_SENSOR);
  INVERT_THROTTLE = preferences.getBool("INVERT_THROT",INVERT_THROTTLE);
  CARTS_SWHITCHING = preferences.getBool("CART_SWHITCH",CARTS_SWHITCHING);
  SERVO = preferences.getBool("SERVO",SERVO);

  Servo_type = preferences.getInt("servo_type",Servo_type);
  Servo_rpm = preferences.getInt("servo_rpm",Servo_rpm);
  Servo_percent = preferences.getInt("servo_perc",Servo_percent);
  
  APSSID = preferences.getString("APSSID",APSSID);
  APPSK = preferences.getString("APPSK",APPSK);
  
  UPDATE_MODE = preferences.getBool("UPDATE_MODE",UPDATE_MODE);
  UPDATE_CORE = preferences.getBool("UPDATE_CORE",UPDATE_CORE);
  INIT_SYSTEM = preferences.getBool("INIT_SYSTEM",INIT_SYSTEM);
  
  // OTA Configuration
  OTA_MODE = preferences.getInt("OTA_MODE", OTA_MODE);
  OTA_PASSWORD = preferences.getString("OTA_PASSWORD", OTA_PASSWORD);
  OTA_JWT_TOKEN = preferences.getString("OTA_JWT_TOKEN", OTA_JWT_TOKEN);

  Serial.print("Init_angle");
  Serial.println(Init_angle);
  
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

      Serial.println(addres_angles);
      
      rpmsArr[c][id] = ((float) preferences.getInt(addres_rpm,(int) (rpmsArr[c][id] * 100))) / 100;
      anglesArr[c][id] = ((float) preferences.getInt(addres_angles,(int) (anglesArr[c][id] * 100))) / 100;
      angles_adjustmentArr[c][id] = ((float) preferences.getInt(addres_angles_adjustment,(int) (angles_adjustmentArr[c][id] * 100))) / 100;
      quick_adjustmentArr[c][id] = ((float) preferences.getInt(addres_quick_adjustment,(int) (quick_adjustmentArr[c][id] * 100))) / 100;
      servo_valueArr[c][id] = preferences.getInt(addres_servo,servo_valueArr[c][id]);

      Serial.print(addres_rpm);
      Serial.println(rpmsArr[c][id]);

      Serial.print(addres_angles);
      Serial.println(anglesArr[c][id]);
      
      Serial.print(addres_quick_adjustment);
      Serial.println(angles_adjustmentArr[c][id]);
    }
  }
  
  return true;
}
