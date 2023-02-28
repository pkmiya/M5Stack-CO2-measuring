/* ------------------------------------
  Public workshop "Manufacture of carbon dioxide measuring device for visualization of ventilation" by NIT, KC
  Sample program

    FUNCTIONS
    + Measure concentration of co2 in the air
    + Send data to Ambient and visualize ventilation status
    + Send data to IFTTT and notify and record them
    - Send and manage data with Google spread-sheet

    PROPERTIES
    - Purpose:    Measure, record and publish co2 cont. in stores & shops
    - Devide:     M5Stack-Core-ESP32
    - Author:     Prof. Katsuhiro Morishita
    (Translator and revisor: Yusaku MIYATA)
    - Created at: 2021-09-03, v1.6
    - Lisence:    MIT

    NOTES
    - Maintain program until logic breaks down in 2106.
    - Root certificates are not set for IFTTT comms in this program.
      Do so if you need.

   ------------------------------------ */

// v1.6
/*
  DONE Ambient data control: hours, days
  YET  Google spread-sheet
  DONE IFTTT sending message per 2 hours
*/

/* ------------------------------------
        LIBRARY INCLUSIONS
   ------------------------------------ */

#include <M5Stack.h>
#include <time.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <Ambient.h>
#include <IFTTTMaker.h>
// Library version - ArduinoJson: v5.x.x (latest: v6.18.3)
// Board   version - esp32:       v1.0.4 (latest: v2.0.0)

/* ------------------------------------
        VARIABLE DECLARATIONS
   ------------------------------------ */

// CAN CHANGE AS YOU WANT
const uint32_t AMBIENT_INTERVAL = 30ul;     // interval of uploading to ambient [s] - ul: stands for "unsigned long"
uint16_t buzzer_limit = 1100;               // threshold value for sounding buzzer
const int16_t BANDS = 3;                             // number of time slot for uploading data to Ambient
int16_t upload_time[BANDS][2] = {{0, 4}, {7, 12}, {13, 24}}; // *remark: declare separately if time-zone goes accross midnight (e.g. [23-4] > [23-0] + [0-4])
bool WORK_DAY[7] = {false, true, true, true, true, true, true}; // true: open, false: close - 0: Sun = 6: Sat
int amb_caution = 900;                    // Ambient: co2 cont for caution
int amb_warning = 1000;                   //                   for warning

// time things
time_t unix_time          = 0;            // UNIX time - times passed since 1970-01-01                    [s]
uint32_t millis_at_sync   = 0ul;          // internal time at the time when unix_time has been set        [ms]
const time_t NTP_INTERVAL = 1200ul;       // interval of inquiring to NTP server if NOT time-synced [ms]
time_t next_ntp_time      = NTP_INTERVAL; // scheduled time of ...
uint32_t ntp_sync_count   = 0ul;          // counting how many times syncs are done

// Wi-Fi and IFTTT things
char ssid[]     = "your_ssid_here";   // 0024A5E7ACCC   CR_Ras_LOG  kpj1a          D80F99E12004-2G  // DEPENDS ON ENVIRONMENTS
char password[] = "password";         // aucrakbj84ffa  __pkmiya    0522-M5stack   2215002767137    // DEPENDS ON ENVIRONMENTS
WiFiClient client;                    // Ambient
const uint32_t WIFI_INTERVAL = 30ul;  // interval of trying to connect to Wi-Fi [s]
time_t next_wifi_time = WIFI_INTERVAL;// scheduled time for attempting to connect to Wi-Fi if NOT connected

// Ambient things
Ambient ambient;
uint16_t channelId = 12345; // Channel ID of Ambient // DEPENDS ON ENVIRONMENTS // 36250            41001
char writeKey[] = "EnterWriteKeyHere"; // Light key   // DEPENDS ON ENVIRONMENTS // 5ddb9445d980d9a8 cd85ffb482cc4e2c
time_t next_amb_time = AMBIENT_INTERVAL;
const double DIV_COE = 1.f; // coefficient to devide the measured values (Deviding by 100 should make it easier to look for smartphone users)

// IFTTT things - Maker webhooks
String eventName = "Event-name";         // e.g. "CO2-sensor_read"
String eventKey = "EventKeyHere";
const time_t IFTTT_INTERVAL = 3600ul;         // interval       of sending data to IFTTT [s]  <= Rec: 3600 for non-Pro users, 900 for Pro users
time_t next_ifttt_time = IFTTT_INTERVAL;      // scheduled time of ...


/* ------------------------------------
        MAIN PROCESS
   ------------------------------------ */


void setup(){
  // Settings - serial comms
  Wire.begin(115200);
  Serial2.begin(9600);
  delay(500);
  Serial.println("-- Start of setup() --");

  // Settings - M5stack
  M5.begin();
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextColor(TFT_WHITE);
  M5.Lcd.setTextFont(4);

  // Wi-Fi - attempt for connection
  M5.Lcd.println("Connecting to Wi-Fi ...");
  bool req_wifi = wifiConnect(ssid, password, 30000ul); // wait for 30 seconds to be connected
  M5.Lcd.print("Wi-Fi connection result: ");
  M5.Lcd.println(req_wifi);

  // Time - prep for connecting to NTP servers
  configTime(3600 * 9, 0, "ntp.nict.jp", "time.google.com", "ntp.jst.mfeed.ad.jp");  // arguments: time_lag_to_UTC, daylight_summer_time_or_not, name_of_ntp_servers_1, name_of_ntp_servers_2, ...

  // config through the Internet
  if (WiFi.status() == WL_CONNECTED){
    // Ambient - connect to the ambient serivce
    Serial.println("Connecting to Ambient ...");
    bool req_amb = ambient.begin(channelId, writeKey, &client);  // REMARK: returns TRUE (1) even if key does not exist (can check if the Internet is available though)
    M5.Lcd.print("Ambient connection result: ");
    M5.Lcd.println(req_amb);                                     // 1: Successful, 0: failed
    
    // get current time
    unix_time = get_time(&millis_at_sync, &ntp_sync_count);
  }

  // End of setup
  delay(5000);
  M5.Lcd.clear();     // Clear screen of LCD
  Serial.println("-- End of setup() --");

  // Notify M5Stack has been successfully booted
  my_beep(2000, 500, 2);
}

void loop(){
  Serial.print("-- start of loop() --, ");
  uint32_t t = millis();// get current time [ms] in mi-con
  Serial.print(t);
  M5.update();          // update m5stack

  // actions when buttons are pushed
  button_action();
  
  // measurements and display
  uint16_t co2 = read_CO2();  // read CO2 concentration [ppm]
  Serial.print(", co2: ");    // send to serial port
  Serial.print(co2);
  drawPPMVal(co2);            // display on the M5Stack
  Serial.print(", Wi-Fi: ");
  Serial.print(WiFi.status());

  // sound beep
  if(co2 > buzzer_limit){
    my_beep(1000, 200, 2);
  }

  // update time
  update_time(&unix_time, &millis_at_sync);
  Serial.print(", UNIX time: ");
  Serial.print(unix_time);
  Serial.print(time2_str(&unix_time));

  // for DEBUG
  Serial.printf(", next_wifi_time: %d, next_amb_time: %d, next_ifttt_time: %d, next_ntp_time: %d", next_wifi_time, next_amb_time, next_ifttt_time, next_ntp_time);

  // attempt connecting to Wi-Fi periodically if NOT connected
  if(WiFi.status() != WL_CONNECTED &&  // if: over scheduled time
      next_wifi_time < unix_time){     // if: NOT connected to the Internet
    Serial.print(",");
    wifiConnect(ssid, password, 30000ul);
    // next_wifi_time = (unix_time / WIFI_INTERVAL + 1) * WIFI_INTERVAL; // calc next execution time
    next_wifi_time = unix_time + WIFI_INTERVAL;
  } 

  // send data to Ambient
  if (WiFi.status() == WL_CONNECTED && // if: connected to the Internet
        next_amb_time < unix_time &&   // if: over scheduled time
        isService(&unix_time)){        // if: during businness hours

    Serial.print(", uploading to Ambient ... ");
    double co2f = (double)co2 / DIV_COE;  // for smartphone users
    ambient.set(1, co2f);
    ambient.set(2, amb_caution);          // threshold value: caution
    ambient.set(3, amb_warning);          // threshold value: warning
    ambient.send();
    client.stop();
    // next_amb_time = (unix_time / AMBIENT_INTERVAL + 1) * AMBIENT_INTERVAL;
    next_amb_time = unix_time + AMBIENT_INTERVAL;
    Serial.print(co2f);
    Serial.print(", sent successfully. ");
  }

  // send data to IFTTT     *REMARK: may not work if calling interval is too short
  if (WiFi.status() == WL_CONNECTED &&
        next_ifttt_time < unix_time){
    WiFiClientSecure client_s;            // class for encrypted comms
    client_s.setTimeout(20);              // set time out [s]
    IFTTTMaker ifttt(eventKey, client_s); // object to utilize IFTTT
    Serial.print(", uploading to IFTTT ... ");
    bool r = ifttt.triggerEvent(eventName, String(co2), time2_str(&unix_time)); // arguments: event_name, val1, val2, val3
    client_s.stop();                      // DOES NOT WORK without this disconnecting action

    if(r){ // check if comms are successful
      next_ifttt_time = unix_time + IFTTT_INTERVAL;
      Serial.print(", sent successfully. ");
    } else{
      Serial.print(", sending failed:( ");
      next_ifttt_time = unix_time + (time_t)random(60, 200);  // retrying in a short time
    }
    Serial.print(", next IFTTT comms at: ");
    Serial.print(time2_str(&next_ifttt_time));

    // next_ifttt_time = (unix_time / IFTTT_INTERVAL + 1) * IFTTT_INTERVAL;
  }

  // get current time if NOT obtained
  if (WiFi.status() == WL_CONNECTED &&  // if: connected to the Internet
        next_ntp_time < unix_time &&    // if: over scheduled time
        ntp_sync_count == 0ul){         // if: NOT synced yet
    time_t t_ = get_time(&millis_at_sync, &ntp_sync_count);   // attempt to obtain time
    if (t_ > 0) unix_time = t_;         // store results in global variable
    // next_ntp_time = (unix_time / NTP_INTERVAL + 1) * NTP_INTERVAL;
    next_ntp_time = unix_time + NTP_INTERVAL;
    Serial.print(", synced with NTP server. ");

  }

  // Reset M5Stack every day
  if(t > 86400000ul){
    M5.Power.reset();
  }
  
  delay(1000);
  Serial.println("");
}


/* ------------------------------------
        USER-DEFINED FUNCTIONS
   ------------------------------------ */

// make beep sounds
void my_beep(uint16_t freq, uint16_t time_width, uint16_t sound_volume){
  M5.Speaker.begin();                // calling - here, beep sounds
  M5.Speaker.setVolume(sound_volume);// 0(muted), 1 - 8 (BE CAREFUL: quite loud)
  M5.Speaker.tone(freq, time_width); // tone with freq [Hz], for time_width[ms] *probably not working?
  delay(time_width);                 // time_width [ms]
  M5.Speaker.mute();                 // Stopping beep
}

// do actions for each button
void button_action(void){
  // A: shutdown, B: reboot, C: calibration
  if (M5.BtnA.wasPressed()){
    M5.powerOFF();
  }
  if (M5.BtnB.isPressed()){
    M5.Power.reset();
  }
  if (M5.BtnC.wasPressed()){
    reset_CO2();
    drawBlueScreen("Resetting...");
    delay(1000);
    M5.Power.reset();
  }
  return;
}

// Draw Blue Screen
void drawBlueScreen(String s){
  M5.Lcd.fillScreen(TFT_BLUE);
  M5.Lcd.setTextColor(TFT_WHITE);
  M5.Lcd.setTextSize(1);
  M5.Lcd.drawString(s, 0, 100);
}

// Display measured value on LCD even if there are no updates
void drawPPMVal(uint16_t ppm){
  M5.Lcd.setTextColor(TFT_WHITE);
  M5.Lcd.setTextSize(2);
  M5.Lcd.drawString("CO2 Meter", 0, 0);
  M5.Lcd.drawString("ppm", 215, 80);

  // overwrite with black characters if not displaying "warning" - seems like screen turned off
  if (ppm < 1500){
    M5.Lcd.setTextColor(TFT_BLACK);
    M5.Lcd.drawString("Warning!", 10, 160);
  }

  // configure display of "warning", color of text
  // *deleted "< 1500" to prevent bugs
  if (ppm >= 1500){
    M5.Lcd.setTextColor(TFT_RED);
    M5.Lcd.drawString("Warning!", 10, 160);
  }
  else if (ppm >= 1000){  
    M5.Lcd.setTextColor(TFT_ORANGE);
  }
  else if (ppm >= 500){
    M5.Lcd.setTextColor(TFT_GREEN);
  }
  else{
    M5.Lcd.setTextColor(TFT_BLUE);
  }

  // display CO2 concentration
  M5.Lcd.fillRect(0, 60, 215, 100, TFT_BLACK);  // fill partly, due to that clear() flicks the screen entirelyclrear()
  M5.Lcd.drawNumber(ppm, 0, 60, 6);

  // display communication status
  M5.Lcd.setTextColor(TFT_WHITE);
  M5.Lcd.setTextSize(1);
  M5.Lcd.fillRect(200, 200, 50, 30, TFT_BLACK);
  M5.Lcd.drawString("Wi-Fi condition: ", 0 , 200);
  M5.Lcd.drawNumber(WiFi.status(), 200, 200);
}



// Reset CO2 sensors
void reset_CO2()
{
  const uint8_t kResetCommand[] = {0xFF, 0x01, 0x87, 0x00, 0x00,
                                         0x00, 0x00, 0x00, 0x78};
  Serial2.write(kResetCommand, sizeof(kResetCommand));
}


// Read CO2 concentration
uint16_t read_CO2()
{
  uint16_t meas_PPM = 0;
  const uint8_t kReadCommand[] = {0xFF, 0x01, 0x86, 0x00, 0x00,
                                        0x00, 0x00, 0x00, 0x79};
  
  // starting reading sensor values
  uint8_t response[9] = {};     // declare before using
  Serial2.write(kReadCommand, sizeof(kReadCommand));
  Serial2.readBytes(response, sizeof(response));

  // read sensor values
  if (response[0] == 0xFF && response[1] == 0x86){
    meas_PPM = ((uint16_t)response[2] << 8) + (uint16_t)response[3];  // shifting bits
  }

  // enable the above for tesing functions
  // meas_PPM = (uint16_t)random(400, 2900);   // should return random value when testing

  return meas_PPM;
}



// Try connecting to Wi-Fi
bool wifiConnect(char *ap_ssid, char *pass, uint32_t timeout)
{
  uint32_t ts = millis();    // remember time when processing started
  Serial.print("Connecting to " + String(ssid) + ". ");

  // start connecting to Wi-Fi
  WiFi.begin(ap_ssid, pass);

  // wait until connected - remark: timeout status exists
  while (WiFi.status() != WL_CONNECTED){
    uint32_t t = millis();
    if (t < ts || t - ts > timeout) break;
    delay(500);
    Serial.print(".");
  }

  // exit this function depending on the connection status
  if (WiFi.status() == WL_CONNECTED){
    // conncted successfully - displaying IP address
    Serial.print("Connected! IP address: ");
    Serial.println(WiFi.localIP());
    return true;
  }
  else{
    Serial.println("ERROR:( Failed connecting to Wi-Fi");
    return false;
  }
}

time_t get_time(uint32_t *time_millis, uint32_t *count){
  struct tm timeinfo;
  if(getLocalTime(&timeinfo)){
    Serial.print("NTP time: ");
    Serial.println(&timeinfo, "%A %B %d %Y %H:%M:%S");
    time_t t = mktime(&timeinfo);                       // convert into UNIX time [sec]
    *time_millis = millis();
    *count = *count + 1;

    return t;
  }
  return 0;
}

void update_time(time_t *unix_t, uint32_t *last_time_millis){
  uint32_t t = millis();
  *unix_t += (t - *last_time_millis) / 1000ul;  
  *last_time_millis = t - (t - *last_time_millis) % 1000;
  // note: ignored that continuting to work over a month would occur bugs - since M5Stack resets every day
}

String time2_str(time_t *unix_t){
  tm *t_tm;
  char buff[100];
  t_tm = localtime(unix_t);                                     // generate struct storing date and days
  strftime(buff, sizeof(buff), "%Y-%m-%d(%A) %H:%M:%S", t_tm);
  return String(buff);
}

bool isService(time_t *unix_t){
  if(*unix_t == 0 ) return false; // Simply verify to be UNIX time or not

  tm *t_tm;
  t_tm = localtime(unix_t);
  if(WORK_DAY[t_tm->tm_wday]){// lookup weekday - whether the input date is a businness day or not
    for(int i = 0; i < BANDS; i++){
      int t1 = upload_time[i][0];
      int t2 = upload_time[i][1];
      if(t1 <= t_tm->tm_hour && t_tm->tm_hour < t2) return true; // lookup businness hour
    }
  }
  return false;
}

/* WI-FI STATUS
    255 WL_NO_SHIELD:       assigned when no Wi-Fi shield is present;
    0   WL_IDLE_STATUS:     assigned temporarily when WiFi.begin() is called and remains active until the number of attempts expires (resulting in WL_CONNECT_FAILED) or a connection is established (resulting in WL_CONNECTED);
    1   WL_NO_SSID_AVAIL:   assigned when no SSID is available;
    2   WL_SCAN_COMPLETED:  assigned when the scan networks is completed;
    3   WL_CONNECTED:       assigned when connected to a Wi-Fi network;
    4   WL_CONNECT_FAILED:  assigned when the connection fails for all the attempts;
    5   WL_CONNECTION_LOST: assigned when the connection is lost;
    6   WL_DISCONNECTED:    assigned when disconnected from a network;
*/

// EOF
