// Arduino Uno R4 Wi-Fi Temperature Sensor with Text and Email Alarm. Attribution at bottom. 

#include "RTC.h"
#include <NTPClient.h>
#include <WiFiS3.h>
#include <ESP_Mail_Client.h>

int sensorPin = A0;   // setup data for Temperature Sensor - TMP36 
int sensorValue = 0;  // variable to store the value coming from the sensor
float volts;
float calibrationFactor = -0.036; // adjust volts down 0.036V, 3.6°C, and 6.5°F
float degreesC;
float degreesF;
float minDegreesF = 50.0;
int testHour = 12, timeUpdateHour = 9;
bool testSent = false, timeUpdated = false;
bool sendMessage = true; // send a notification on powerup. 
unsigned long unixTime;
unsigned long unixTimeLastSend = 0;
unsigned long minutesSinceLastSend;
unsigned long minMinutesBetweenSends = 15;

WiFiUDP Udp; // A UDP instance to let us send and receive packets over UDP
NTPClient timeClient(Udp);

#define WIFI_SSID "Your WiFi SSID"
//#define WIFI_PASS "Your WiFi Password" // not needed with MAC address filtering

#define SMTP_HOST "smtp.gmail.com"
#define SMTP_PORT esp_mail_smtp_port_587

#define AUTHOR_EMAIL "Your.Gmail.Account@gmail.com" // Gmail sign in
#define AUTHOR_PASSWORD "16 char app password"  // app password

#define RECIPIENT_EMAIL "WhereToSendAlerts@vtext.com" // @vtext.com turns an email into a Verizon text message. Other carriers have different addresses.

SMTPSession smtp;
void smtpCallback(SMTP_Status status); // Callback function to get the Email sending status
Session_Config config; // Declare the Session_Config for user defined session credentials
SMTP_Message message; // Declare the message class
RTCTime currentTime; // create a RTCTime object

void connectwifi(){
  Serial.begin(9600);
  WiFi.begin(WIFI_SSID);  // WiFi.begin(WIFI_SSID) or WiFi.begin(WIFI_SSID, WIFI_PASS); depending on if password is necessary
  Serial.print("Connecting to Wi-Fi.");
  while (WiFi.status() != WL_CONNECTED){
    Serial.print(".");
    delay(300);
  }
  Serial.print(" Connected with IP: ");
  Serial.println(WiFi.localIP());
}

void setRTCtime(){
  RTC.begin();
  Serial.println("Starting connection to time server...");
  timeClient.begin();
  timeClient.update();
  auto timeZoneOffsetHours = -7; // change the time zone offset to your local one.
  auto unixTime = timeClient.getEpochTime() + (timeZoneOffsetHours * 3600);
  RTCTime timeToSet = RTCTime(unixTime);
  RTC.setTime(timeToSet);
  RTC.getTime(currentTime);
  Serial.println("Real Time Clock (RTC) set to: " + String(currentTime));
}

void getTempF(){
  float accumulator = 0;
  for (int i = 1; i <= 500; i++) {
    accumulator += analogRead(sensorPin);
  }
  sensorValue = accumulator / 500.0;
  volts = (float)sensorValue*5.0/1023.0 + calibrationFactor;
  degreesC = volts * 100.0 - 50.0;
  degreesF = degreesC * 9.0 / 5.0 + 32.0; // global var. no need to return. 
  Serial.println("Temp = " + String(degreesF, 1) + " Deg F");
}

void setupEmail(){
  config.server.host_name = SMTP_HOST; // Set the session config
  config.server.port = SMTP_PORT;
  config.login.email = AUTHOR_EMAIL;
  config.login.password = AUTHOR_PASSWORD;
  //config.login.user_domain = "";
  config.login.user_domain = F("127.0.0.1"); // not having this might have been causing a crash?         **** 

  message.sender.name = "Arduino"; // Setup the email message
  message.sender.email = AUTHOR_EMAIL;  
  message.clearRecipients();
  message.addRecipient(F("Andy"), RECIPIENT_EMAIL);

  if (degreesF < minDegreesF) { // finish setting up email variables
    message.subject = "ALARM";
  } else {
    message.subject = "Notification";
  }
  message.text.content = "Your Location Temp = " + String(degreesF, 1) + " Deg F";
 
  RTC.getTime(currentTime);
  float gmtOffset = 19.0; // GMT offset in hour (7 + 12 per Send_HTML.ino)
  smtp.setSystemTime(currentTime.getUnixTime(), gmtOffset);
}

void sendEmail(){
  smtp.setTCPTimeout(10); // not having this might have been causing a crash?                ****
  
  if (!smtp.connect(&config)){
    ESP_MAIL_PRINTF("Connection error, Status Code: %d, Error Code: %d, Reason: %s", smtp.statusCode(), smtp.errorCode(), smtp.errorReason().c_str());
    return;
  } else {
    Serial.println("smtp.connect successful");
  }
  
  if (!smtp.isLoggedIn()){
    Serial.println("\nNot yet logged in.");
    return;
  }
  else{
    if (smtp.isAuthenticated())
      Serial.println("\nSuccessfully logged in.");
    else
      Serial.println("\nConnected with no Auth.");
  }

  if (MailClient.sendMail(&smtp, &message)) {
    sendMessage = false;
    unixTimeLastSend = unixTime;
  } else {
    ESP_MAIL_PRINTF("Error, Status Code: %d, Error Code: %d, Reason: %s", smtp.statusCode(), smtp.errorCode(), smtp.errorReason().c_str());
  }
}

void setup(){
  connectwifi();
  setRTCtime();
  WiFi.disconnect();

  MailClient.networkReconnect(true);
  smtp.debug(1);
  smtp.callback(smtpCallback); // Set the callback function to get the sending results
}

void loop(){
  getTempF();
  RTC.getTime(currentTime);
  int hour = currentTime.getHour();
  unixTime = currentTime.getUnixTime();
  minutesSinceLastSend = (unixTime - unixTimeLastSend)/60;

  if (hour >= timeUpdateHour && timeUpdated == false){ // RTC on R4 drifts about 15 minutes per day. Update each morning.
      connectwifi();
      setRTCtime();
      WiFi.disconnect();
      timeUpdated = true;
  }
  if (hour < (timeUpdateHour-1) && timeUpdated == true) timeUpdated = false; // -1hr to prevent 2 updates if reset earlier a couple minutes. 

  if (hour >= testHour && testSent == false){ // set flag to send message if time and it hasn't been sent yet
    sendMessage = true;
    testSent = true;
  }
  if (hour < testHour && testSent == true) testSent = false; // reset trigger

  if (degreesF < minDegreesF) sendMessage = true; // set flag to send message if temperature is too low

  if (sendMessage == true && minutesSinceLastSend >= minMinutesBetweenSends) {
    setupEmail();
    connectwifi();
    sendEmail();
    //smtp.closeSession(); // could this be causing crash? not sure it is necessary. 
    WiFi.disconnect();
  }

  delay(60000); // about 1 minute
}

void smtpCallback(SMTP_Status status){
  Serial.println(status.info());
  if (status.success()){
    ESP_MAIL_PRINTF("Message sent success: %d\n", status.completedCount());
    ESP_MAIL_PRINTF("Message sent failed: %d\n", status.failedCount());
    for (size_t i = 0; i < smtp.sendingResult.size(); i++) {
      SMTP_Result result = smtp.sendingResult.getItem(i);
      ESP_MAIL_PRINTF("Message No: %d\n", i + 1);
      ESP_MAIL_PRINTF("Status: %s\n", result.completed ? "success" : "failed");
      ESP_MAIL_PRINTF("Date/Time: %s\n", MailClient.Time.getDateTimeString(result.timestamp, "%B %d, %Y %H:%M:%S").c_str());
      ESP_MAIL_PRINTF("Recipient: %s\n", result.recipients.c_str());
      ESP_MAIL_PRINTF("Subject: %s\n", result.subject.c_str());
    }
    Serial.println("----------------");
    smtp.sendingResult.clear(); // You need to clear sending result as the memory usage will grow up.
  }
}

/*
built based on examples provided by the following and many others:
Mobizt https://github.com/mobizt/ESP-Mail-Client/blob/master/examples/SMTP/Send_HTML/Send_HTML.ino
Rui Santos https://RandomNerdTutorials.com/esp32-send-email-smtp-server-arduino-ide/

MIT License

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/