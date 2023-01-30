#include <LiquidCrystal.h>
#include <Ethernet.h>
#include <PubSubClient.h>
#include <TimeLib.h>
#include <Timezone.h>
#include <OneWire.h>
#include <DallasTemperature.h>

LiquidCrystal lcd(8, 7, 6, 5, 4, 3);

#define MODE A0
boolean TEST_MODE = false;

OneWire oneWire(2);                         //temperature sensor data wire connected to d2
DallasTemperature sensor(&oneWire);

char TEMPERATURE[6] = {};                   //char array for measured temperature

#define SERIAL_ON                           //define SERIAL_ON for debugging

void measure();
void refreshDisplay();
void testMode();

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//===-----------------------------------------Ethernet and MQTT-------------------------------------------------------===//
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
EthernetClient ethClient;
static uint8_t mac[6] = {0x44, 0x76, 0x58, 0x10, 0x00, 0x42};             //arduinos mac address


char* deviceId = "2020_DI_B_22";                                          //mqtt client username
char* clientId = "2020_CI_B_22";                                          //mqtt client id
char* devicePass = "tamk1";                                               //mqtt password

unsigned int Port = 10884;                                                //mqtt port
byte server[] = {193, 167, 167,59};                                       // TAMK IP

void callback(char* topic, byte* payload, unsigned int length);           // subscription callback for received MQTT messages   

PubSubClient client(server, Port, callback, ethClient);                   // mqtt client 

 #define inTopic    "ICT1B_in_2020"                                        // * MQTT channel where data are received 
 #define outTopic   "ICT1B_out_2020"                                       // * MQTT channel where data is send 

 void initEthernet();
 void connectMQTT();
 void sendJSON();
 
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

 
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//===------------------------------------------------TIME----------------------------------------------------------===//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const int NTP_PACKET_SIZE= 48;                                // NTP time stamp is in the first 48 bytes of the message

byte packetBuffer[ NTP_PACKET_SIZE];                          //create byte array as buffer for NTP messages

EthernetUDP Udp;

TimeChangeRule euDST = {"DST", Last, Sun, Mar, 3, 180};      //Daylight saving time
TimeChangeRule euST = {"ST", Last, Sun, Oct, 4, 120};        //Standard time

Timezone euEastern(euDST, euST);                             //Timezone

void sendNTP();
time_t getNTP();

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void setup() 
{
  
  Serial.begin(9600);
  lcd.begin(16,2);
  sensor.begin();                                                            

  if(analogRead(MODE) == LOW){TEST_MODE = true;}                              //start test mode if button is pushed
  
  else
  {
    initEthernet();
    Udp.begin(123);
    setSyncProvider(getNTP);                                                 //set sync provider for time 
    setSyncInterval(3600);                                                   //set sync interval (1 hour)

    while(timeStatus() == timeNotSet) {setSyncProvider(getNTP);}

    connectMQTT();
  }
  Serial.println(TEST_MODE); 
  lcd.clear();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

time_t prevDisplay = 0;                                           // when lcd was refreshed
time_t prevSend = 0;                                              // when data was previously sent to server

void loop()
{
  if(TEST_MODE)
  {
    testMode();
    delay(500);
  }

  else                                                           //mqtt loop
  {
    if (timeStatus() != timeNotSet) 
    {
      if (now() != prevDisplay)                                  //update the display only if time has changed
      { 
        prevDisplay = now();
        
        measure();
        refreshDisplay();    
        client.loop();                                          //maintain mqtt and ethernet
        Ethernet.maintain();  
      }   
      
      if (now() - prevSend > 120)                               //send data every 2 minutes to server
      {
        prevSend = now();
        sendJSON();
      }
    }
  }
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//===-------------------------------------------ETHERNET AND MQTT FUNCTIONS----------------------------------===//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void initEthernet()
{
  #ifdef SERIAL_ON
    Serial.println("Setting up ethernet.");
  #endif

  lcd.clear();
  lcd.print("Connecting...");
  delay(1000);

  if(Ethernet.begin(mac) == 0)                         //connect ethernet. ethernet.begin() will return 0 if connection
  {                                                    //fails. wait time is 60 seconds 
    #ifdef SERIAL_ON
      Serial.println("Failed to connect.");
    #endif

    lcd.setCursor(0,1);
    lcd.print("Connection fail");
    delay(2000);
  } 
  else                                                //if connection was successful print ip 
  {
    #ifdef SERIAL_ON
      Serial.print("Connected! IP: ");
      Serial.println(Ethernet.localIP());
    #endif
  
    lcd.clear();
    lcd.print("Connected!");
    lcd.setCursor(0,1);
    lcd.print("IP: ");
    lcd.print(Ethernet.localIP());
    delay(1000);
  }           
}

void connectMQTT()
{
  #ifdef SERIAL_ON
    Serial.println("Connecting to mqtt.");
  #endif
  
  unsigned long startmillis = millis();

  while(!client.connected() && millis() - startmillis < 10000)      //try connection for 10 secs
  {
    client.connect(clientId, deviceId, devicePass);
    delay(1000);      
  }

  if(client.connected())
  {
    client.subscribe(inTopic);

    #ifdef SERIAL_ON
      Serial.println("Client connected.");
    #endif
  }

  else
  {
    #ifdef SERIAL_ON
      Serial.println("Connection failed.");
    #endif
  }
}


void sendJSON()
{
  char msg[100] = {};
  sprintf(msg, "IOTJS={\"S_name\":\"temp_22\",\"S_value\":%5s,\"S_unit\":\"°C\"}", TEMPERATURE);  //create JSON

  #ifdef SERIAL_ON
    Serial.print("JSON: ");
    Serial.println(msg);
    Serial.println("Sending JSON to raspberry.");
  #endif

  if(client.connected())                                              //if client connected
  {
    client.publish(outTopic, msg);                                    //send JSON to raspberry
    
    #ifdef SERIAL_ON
      Serial.println("Message sent.");
    #endif
  }

  else                                                                //if client is not connected
  {
    connectMQTT();                                                    //try reconnecting

    if(client.connected()){client.publish(outTopic, msg);}            //if succesfully connected -> send message, otherwise try again next time 
  }
}

void callback(char* topic, byte* payload, unsigned int length)
{
  char* received = {};
  received = (char*) malloc(length + 1);
  memcpy(received, payload, length);                                //copy received msg to received variable

  free(received);                                                   //for now dont do anything with it
}
//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx//


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//===--------------------------------------------TIME FUNCTIONS-------------------------------------------------===//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void sendNTP(char* address)                                           //sends NTP time request to time server
{
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
                                                                      // Initialize values for NTP request
  packetBuffer[0] = 0b11100011;  
  packetBuffer[1] = 0;     
  packetBuffer[2] = 6;
  packetBuffer[3] = 0xEC;  
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  if(Udp.beginPacket(address, 123) == 1) 
  {
    Udp.write(packetBuffer, NTP_PACKET_SIZE);
    
    if(Udp.endPacket() != 1)
    {
      Serial.println(F("Send error"));
    }
  }
  
  else 
  {
    Serial.println(F("Socket error"));
  }
}


time_t getNTP()
{
  char address[]PROGMEM = "0.europe.pool.ntp.org";                                    //address for NTP time pool
  unsigned long _time ={};                                                            //variable for parsed time 
  const unsigned long seventyYears = 2208988800UL;                                    //seventy years in seconds
  
  #ifdef SERIAL_ON
    Serial.println("Fetching time...");
  #endif
  
  sendNTP(address);                                                                   //send time request to server

  #ifdef SERIAL_ON
    Serial.println("Sent NTP packet.");
  #endif

  unsigned long startmillis = millis();

  while(millis() - startmillis < 5000)                                                //wait up to 5s for response
  {
    if(Udp.parsePacket())
    {
      Udp.read(packetBuffer, NTP_PACKET_SIZE);                                        //read received packet to buffer

      unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
      unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
      unsigned long secsSince1900 = highWord << 16 | lowWord;      

      _time = secsSince1900 - seventyYears;                                           //init _time variable with utc time so we can handle DST with timezone lib

      #ifdef SERIAL_ON
        Serial.print("Sync successful: ");
        Serial.println(_time);
      #endif

      return _time;                                                                  //return time if sync is successful
    }
  }

  #ifdef SERIAL_ON
    Serial.println("Time sync failed.");
  #endif
  return 0;                                                                          //return 0 if sync failed
}

//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx//


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//===----------------------------MEASURE, DISPLAY AND TEST MODE FUNCTIONS--------------------------------------===//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
                                                                                                                                                                                                                                 
void measure()                                                                                                     
{                                                                                                                
  sensor.requestTemperatures();
  float temp = sensor.getTempCByIndex(0);
  dtostrf(temp, 5, 1, TEMPERATURE);                                   //write temperature to char array
}

void refreshDisplay()
{
  {
    time_t utc = now();                                               //save utc time to time_t variable 
    time_t local = euEastern.toLocal(utc);                            //and convert it to local time using timezone rules
    char upper_row[16] = {};
    
    sprintf(upper_row, "%02d:%02d:%02d", hour(local), minute(local), second(local));
        
    lcd.setCursor(0,0);
    lcd.print(upper_row);
  }
  {
    char lower_row[16] = {};

    sprintf(lower_row, "Temp:%9s%1c%1c", TEMPERATURE, 223, 'C');
    
    lcd.setCursor(0,1);
    lcd.print(lower_row);
  }
}

void testMode()
{
  char upper_row[16] = {};                                        //definition of needed variables in test mode 
  char lower_row[16] = {};
  
  measure();                                                      //refresh global values
    
  sprintf(upper_row, "Temp:%9s%1c%1c", TEMPERATURE, 223, 'C');
  lcd.print(upper_row);  
  
  sprintf(lower_row, "Team 22%9s", "TM");
  lcd.setCursor(0,1);
  lcd.print(lower_row);
}
