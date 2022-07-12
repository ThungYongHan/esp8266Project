#include <ESP8266WiFi.h>
#include <FirebaseArduino.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// MQ-135
#include "MQ135.h"
#define ANALOGPIN A0
#define RZERO 50.47

// MQ-2
#include <MQ2.h>

//DHT-11
#include <Adafruit_Sensor.h>
#include <DHT.h>
#define DHTTYPE DHT11
#define DHTPIN D4

//DSM501A
byte buff[2];
unsigned long durationPM25;
unsigned long starttime;
unsigned long endtime;
unsigned long sampletime_ms = 30000;
unsigned long lowpulseoccupancyPM25 = 0;

int i=0;

MQ135 gasSensor = MQ135(ANALOGPIN);

int LPG, CO, Smoke;
int mq2_Pin = A0;
MQ2 mq2(mq2_Pin);

DHT dht(DHTPIN,DHTTYPE);  
float flttemperature;
float flthumidity;
String temperature;
String humidity;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

#define FIREBASE_HOST "group-project-esp8266-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "NKH6PeDAccfhdILOG7kcZgVvXOyjWzmm7YVDfvpQ"
#define WIFI_SSID "POCO X3 Pro"
#define WIFI_PASSWORD "yonghanpswd"

//Multiplexer control pins
int s0 = D0;
int s1 = D1;
int s2 = D2;
int s3 = D3;

//Multiplexer in "SIG" pin
int SIG_pin = A0;

void setup(){
  // DSM501A
  pinMode(D5,INPUT);
  starttime = millis(); 

  // Multiplexer
  pinMode(s0, OUTPUT); 
  pinMode(s1, OUTPUT); 
  pinMode(s2, OUTPUT); 
  pinMode(s3, OUTPUT); 
  digitalWrite(s0, LOW);
  digitalWrite(s1, LOW);
  digitalWrite(s2, LOW);
  digitalWrite(s3, LOW);
  
  Serial.begin(9600);

  // Connect to wifi.
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("connecting");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();
  Serial.print("connected: ");
  Serial.println(WiFi.localIP());

  // necessary to avoid NaN readings
  dht.begin();          
  
  timeClient.begin();
  timeClient.setTimeOffset(28800);

  Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH);
  delay(20000);
}

void loop(){
  timeClient.update();
  time_t epochTime = timeClient.getEpochTime();
  struct tm *ptm = gmtime ((time_t *)&epochTime);
  int monthDay = ptm->tm_mday;
  int currentMonth = ptm->tm_mon+1;
  int currentYear = ptm->tm_year+1900;
  String date = String(currentYear) + "-" + String(currentMonth) + "-" + String(monthDay);
  String time = timeClient.getFormattedTime();

  for(int i = 0; i < 2; i ++){
    //MQ-135
    if (i == 0){
      delay(1000);
      Serial.print("Value at channel 0 is: ");
      Serial.println(readMux(0));
      
      float ppm = gasSensor.getPPM();
      Serial.print("CO2 ppm value: ");
      Serial.println(ppm);
      
      String CO2 = String(ppm);
      String firebaseCO2 = "/MQ135/CO2/" + date + "/" + time + "/";
      
      Firebase.pushString(firebaseCO2, CO2);
    }
    
    //MQ-2
    if (i == 1){
      delay(1000);
      Serial.print("Value at channel 1 is: ");
      Serial.println(readMux(1));
      
      float* values= mq2.read(true);
      LPG = mq2.readLPG();
      CO = mq2.readCO();
      Smoke = mq2.readSmoke();
      
      String LPG = String(mq2.readLPG());
      String CO = String(mq2.readCO());
      String Smoke = String(mq2.readSmoke());
      
      String firebaseLPG = "/MQ2/LPG/" + date + "/" + time + "/";
      String firebaseCO = "/MQ2/CO/" + date + "/" + time + "/";
      String firebaseSmoke = "/MQ2/Smoke/" + date + "/" + time + "/";
      
      Serial.println("LPG Value: " + LPG);
      Serial.println("CO Value: " + CO);
      Serial.println("Smoke Value: " + Smoke);
      
      Firebase.pushString(firebaseLPG, LPG);
      Firebase.pushString(firebaseCO, CO);
      Firebase.pushString(firebaseSmoke, Smoke);
    }
  }

   // DHT11
   delay(1000);
   flttemperature = dht.readTemperature();
   flthumidity = dht.readHumidity();
   
   temperature = String(flttemperature);
   humidity = String(flthumidity);
   
   Serial.println("Temperature: " + temperature);
   Serial.println("Humidity: " + humidity); 
                  
   if(!isnan(flttemperature)){
       String firebaseTemperature = "/DHT11/Temperature/" + date + "/" + time + "/";
       Firebase.pushString(firebaseTemperature,temperature);        
   }             
   if(!isnan(flthumidity)){
      String firebaseHumidity = "/DHT11/Humidity/" + date + "/" + time + "/";
      Firebase.pushString(firebaseHumidity, humidity);  
   }

  // DSM501A
  durationPM25 = pulseIn(D5, LOW);
  lowpulseoccupancyPM25 += durationPM25;
  endtime = millis();
  if ((endtime-starttime) > sampletime_ms) //Only after 30s has passed we calcualte the ratio
  {
    float conPM25 = calculateConcentration(lowpulseoccupancyPM25,30);
    Serial.print("PM25: ");
    Serial.println(conPM25);
    
    String firePM25 = String(conPM25);
    lowpulseoccupancyPM25 = 0;
    starttime = millis();
    String pm25 = "/DSM501A/PM25/" +  date + "/" + time + "/";
    
    Firebase.pushString(pm25,firePM25);
  }                    
  delay(60000);
}

int readMux(int channel){
  int controlPin[] = {s0, s1, s2, s3};

  int muxChannel[2][4]={
    {0,0,0,0}, //channel 0
    {1,0,0,0}, //channel 1
  };

  //loop through the 4 sig
  for(int i = 0; i < 4; i ++){
    digitalWrite(controlPin[i], muxChannel[channel][i]);
  }

  //read the value at the SIG pin
  int val = analogRead(SIG_pin);

  //return the value
  return val;
}

float calculateConcentration(long lowpulseInMicroSeconds, long durationinSeconds){
  float ratio = (lowpulseInMicroSeconds/1000000.0)/30.0*100.0; //Calculate the ratio
  float concentration = 0.001915 * pow(ratio,2) + 0.09522 * ratio - 0.04884;//Calculate the mg/m3
  Serial.print("lowpulseoccupancy:");
  Serial.print(lowpulseInMicroSeconds);
  String firelowpulse = String(lowpulseInMicroSeconds);
  Serial.print("    ratio:");
  Serial.print(ratio);
  String fireratio = String (ratio);
  Serial.print("    Concentration:");
  Serial.println(concentration);
  String firecon = String (concentration);
  return concentration;
}
