#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <EEPROM.h>
#include "MFRC522_.h"
#include <Wire.h>
#include "Adafruit_MCP23017.h"
#include <SPI.h>
#include <Ticker.h>

#define NEEDED_EEPROM_SIZE    512
#define CONFIG_EEPROM_ADDR    0
#define MAX_SENSORS   10
#define NO_SENSOR_TYPES 5   // add 1 extra to sensor types
#define NO_RELAY_OUTS   3
#define MAX_SOUND_FILENAME_SIZE 64

Adafruit_MCP23017 mcp;
Ticker sec_ticker;

enum game_status{NOT_SOLVED, DELAYED_SOLVED, WAIT_FOR_CHECK, SOLVED};

enum relay_states{RELAY_OFF, RELAY_DELAY_ON_STARTED, RELAY_DELAY_ON_EXPIRED, RELAY_DELAY_OFF_STARTED, RELAY_DELAY_OFF_EXPIRED};

enum game_status game_is_solved = NOT_SOLVED;
uint8_t game_solved_counter = 0;

String sensor_types[NO_SENSOR_TYPES] = {"None", "RFID", "IR", "Touch", "Hall"};
enum sensor_types_enum{SENSOR_NONE, SENSOR_RFID, SENSOR_IR, SENSOR_TOUCH, SENSOR_HALL};

//SSID and Password to your ESP Access Point
const char* ssid = "ESPWebServer";
const char* password = "12345678";
 
uint8_t io_to_expio[MAX_SENSORS] = {15, 14, 13, 12, 11, 10, 9, 8, 7, 6};
uint8_t relay_expios[NO_RELAY_OUTS] = {1, 2, 3};

MFRC522 *rfids[MAX_SENSORS];

ESP8266WebServer server(80); //Server on port 80

enum relay_switch_states {relay_NC, relay_NO};

struct SensorConfig
{
  uint8_t type, on_off;
};

struct RelayConfig
{
  enum relay_switch_states nc_no;
  uint8_t on_delay, on_delay_counter;
  uint8_t off_delay, off_delay_counter;
  enum relay_states state;
};

struct CONFIG
{
    struct SensorConfig all_sensor_config[MAX_SENSORS];
    struct RelayConfig relays_config[NO_RELAY_OUTS];
    char sound_file[MAX_SOUND_FILENAME_SIZE];
    uint8_t game_solved_delay;
} main_config;

String SendHTML()
{
  uint8_t i, j;
  
  String ptr = "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\"></head>";
  ptr+= "<center><b>ModuLocks controller configuration</center></b><br>";
  ptr += "<form action=\"/config\" method=\"POST\">";

  for(i=0; i<MAX_SENSORS; i++)
  {
    String sensor_number = String(i), sensor_number_pp = String(i+1);
    ptr += "Sensor input " + sensor_number_pp + ": ";
    ptr += "<select name=\"sensor_list" + sensor_number += "\">";
    for(j=0; j<NO_SENSOR_TYPES; j++)
    {
      ptr += "<option id=\"" + sensor_types[j] + "\"";;
      if(main_config.all_sensor_config[i].type == j)
        ptr += " selected"; 
      ptr += ">" + sensor_types[j] + "</option>";
    }
    ptr += "</select>";

    ptr += "<input type=\"radio\" name=\"on_off" + sensor_number + "\" value=\"on\"";
    if(main_config.all_sensor_config[i].on_off)
      ptr+= " checked";
    ptr += ">ON";
    
    ptr += "<input type=\"radio\" name=\"on_off" + sensor_number + "\" value=\"off\"";
    if(main_config.all_sensor_config[i].on_off == 0)
      ptr+= " checked";
    ptr += ">OFF<br>";
  }
  ptr += "<br>Delay before game is solved: ";
  ptr += "<input type=\"number\" name=\"game_solved_delay\" min=\"0\" max=\"10\" value=\"" + String(main_config.game_solved_delay) + "\"><br>";
  
  ptr += "<br>Set what should happen when game is solved:<br>";
  for(i=0; i<NO_RELAY_OUTS; i++)
  {    
    String relay_number = String(i), relay_number_pp = String(i+1);
    ptr += "Relay output " + relay_number_pp + ": ";
    ptr += "<input type=\"radio\" name=\"nc_no" + relay_number + "\" value=\"NC\"";
    if(main_config.relays_config[i].nc_no == relay_NC)
      ptr += " checked";
    ptr += ">NC";
    
    ptr += "<input type=\"radio\" name=\"nc_no" + relay_number + "\" value=\"NO\"";
    if(main_config.relays_config[i].nc_no == relay_NO)
      ptr += " checked";
    ptr += ">NO<br>";

    ptr += "    Delay before activate: ";
    ptr += "<input type=\"number\" name=\"on_delay" + relay_number + "\" min=\"0\" max=\"10\" value=\"" + String(main_config.relays_config[i].on_delay) + "\"><br>";
    
    ptr += "    Delay before deactivate: ";
    ptr += "<input type=\"number\" name=\"off_delay" + relay_number + "\" min=\"0\" max=\"10\" value=\"" + String(main_config.relays_config[i].off_delay) + "\"><br>";
  }
  ptr += "Sound file (max. " + String(MAX_SOUND_FILENAME_SIZE) + " characters: ";
  ptr += "<input type=\"text\" name=\"sound\" value=\"" + String(main_config.sound_file) + "\"><br>";
  ptr += "<input type=\"submit\" value=\"Save\">";
  ptr += "</form><br>";
  return ptr;
}

void reconfig()
{
  uint8_t i;
  for(i=0; i<MAX_SENSORS; i++)
  {
    delete rfids[i];
    switch(main_config.all_sensor_config[i].type)
    {
      case SENSOR_NONE:
        mcp.pinMode(io_to_expio[i], INPUT);
        break;
      case SENSOR_RFID:
        rfids[i] = new MFRC522(&mcp, io_to_expio[i], 0xFF);
        rfids[i]->PCD_Init();
        delay(10);
        rfids[i]->PCD_DumpVersionToSerial();
        break;
      case SENSOR_IR:
        mcp.pinMode(io_to_expio[i], INPUT);
        break;
      case SENSOR_TOUCH:
        mcp.pinMode(io_to_expio[i], INPUT);
        break;
      case SENSOR_HALL:
        mcp.pinMode(io_to_expio[i], INPUT);
        break;
    }
  }
}

//==============================================================
//     This rutine is exicuted when you open its IP in browser
//==============================================================
void handleRoot() {
  server.send(200, "text/html", SendHTML());
}

void handleConfig()
{
  uint8_t i,j;
  String sensor_list_number, on_off_number, nc_no_number, on_delay_number, off_delay_number;
  
  for(i=0; i<MAX_SENSORS; i++)
  {
    sensor_list_number = "sensor_list" + String(i);
    on_off_number = "on_off" + String(i);
    
    for(j=0; j<NO_SENSOR_TYPES; j++)
      if(server.arg(sensor_list_number) == sensor_types[j])
        main_config.all_sensor_config[i].type = j;

    if(server.arg(on_off_number) == "on")
      main_config.all_sensor_config[i].on_off = 1;
    else
      main_config.all_sensor_config[i].on_off = 0;
  }
  for(i=0; i<NO_RELAY_OUTS; i++)
  {
    main_config.relays_config[i].state = RELAY_OFF;
    main_config.relays_config[i].on_delay_counter = 0;
    main_config.relays_config[i].off_delay_counter = 0;
    
    nc_no_number = "nc_no" + String(i);
    if(server.arg(nc_no_number) == "NC")
      main_config.relays_config[i].nc_no = relay_NC;
    else
      main_config.relays_config[i].nc_no = relay_NO;    

    on_delay_number = "on_delay" + String(i);
    main_config.relays_config[i].on_delay = (server.arg(on_delay_number)).toInt();

    off_delay_number = "off_delay" + String(i);
    main_config.relays_config[i].off_delay = (server.arg(off_delay_number)).toInt();
  }
  main_config.game_solved_delay = (server.arg("game_solved_delay")).toInt();
  
  server.arg("sound").toCharArray(main_config.sound_file, MAX_SOUND_FILENAME_SIZE);
  //Serial.println(server.arg("sound"));
  EEPROM.put(CONFIG_EEPROM_ADDR, main_config);
  EEPROM.commit();

  reconfig();
  
/*  Serial.println("config handler");
  for(int i=0; i<server.args(); i++)
  {
    Serial.println(server.argName(i));
    Serial.println(server.arg(i));
  }
  */
  server.sendHeader("Location","/");
  server.send(303);  
}

void reset_config()
{
  uint8_t i;
  main_config.game_solved_delay = 0;
  
  for(i=0; i<MAX_SENSORS; i++)
  {
    main_config.all_sensor_config[i].type = 0;
    main_config.all_sensor_config[i].on_off = 0;
  }
  for(i=0; i<NO_RELAY_OUTS; i++)
  {
    main_config.relays_config[i].state = RELAY_OFF;
    main_config.relays_config[i].nc_no = relay_NO;
    main_config.relays_config[i].on_delay = 0;
    main_config.relays_config[i].off_delay = 0;
    main_config.relays_config[i].on_delay_counter = 0;
    main_config.relays_config[i].off_delay_counter = 0;
  }

  for(i=0; i<MAX_SOUND_FILENAME_SIZE; i++)
    main_config.sound_file[i] = 0;

  EEPROM.put(CONFIG_EEPROM_ADDR, main_config);
  EEPROM.commit();
  reconfig();
}

void print_config()
{
  uint8_t i;
  for(i=0; i<MAX_SENSORS; i++)
  {
    Serial.print("Sensor input ");
    Serial.print(i);
    Serial.print(": ");
    Serial.println(sensor_types[main_config.all_sensor_config[i].type]);
  }
  Serial.println(main_config.sound_file);
}

uint8_t check_game_solved()
{
  uint8_t game_solved = true, i;
  
  for(i=0; i<MAX_SENSORS; i++)
  {
    switch(main_config.all_sensor_config[i].type)
    {
      case SENSOR_RFID:
        rfids[i]->PCD_Init();
        if(rfids[i]->PICC_IsNewCardPresent() != main_config.all_sensor_config[i].on_off)    // XOR
        {
          game_solved = false;
//          delay(50);
        }
        else if(rfids[i]->PICC_ReadCardSerial() != main_config.all_sensor_config[i].on_off)
        {
          game_solved = false;
          //delay(50);
        }
        rfids[i]->PICC_IsNewCardPresent();
        break;
      case SENSOR_IR:
        if(mcp.digitalRead(io_to_expio[i]) == main_config.all_sensor_config[i].on_off)
          game_solved = false;
        break;
      case SENSOR_HALL:
        if(mcp.digitalRead(io_to_expio[i]) == main_config.all_sensor_config[i].on_off)
          game_solved = false;
        break;
      case SENSOR_TOUCH:
        break;
    }
  }
  return game_solved;
}

void ticker_handler(void)
{
  uint8_t i;
  if(game_solved_counter)
  {
    game_solved_counter--;
    if(!game_solved_counter)
      game_is_solved = WAIT_FOR_CHECK;
  }
  
  for(i=0; i<NO_RELAY_OUTS; i++)
  {
    if(main_config.relays_config[i].on_delay_counter)
    {
      (main_config.relays_config[i].on_delay_counter)--;
      if(!main_config.relays_config[i].on_delay_counter)
        main_config.relays_config[i].state = RELAY_DELAY_ON_EXPIRED;
    }

    if(main_config.relays_config[i].off_delay_counter)
    {
      (main_config.relays_config[i].off_delay_counter)--;
      if(!main_config.relays_config[i].off_delay_counter)
        main_config.relays_config[i].state = RELAY_DELAY_OFF_EXPIRED;
    }
  }  
}
void relay_handler(void)
{
  uint8_t i, detach_ticker = true, set_game_unsolved = true;
  for(i=0; i<NO_RELAY_OUTS; i++)
  {
    if(main_config.relays_config[i].state == RELAY_DELAY_ON_EXPIRED)
    {
      if(main_config.relays_config[i].off_delay)
      {
        Serial.print("Relay");
        Serial.print(i);
        Serial.println(" turning on");
        if(main_config.relays_config[i].nc_no == relay_NC)    // turning on relays
          mcp.digitalWrite(relay_expios[i], 1);
        else
          mcp.digitalWrite(relay_expios[i], 0);
        // Start turning off delay counter  
        main_config.relays_config[i].off_delay_counter = main_config.relays_config[i].off_delay;
        main_config.relays_config[i].state = RELAY_DELAY_OFF_STARTED;
      }
      else
        main_config.relays_config[i].state = RELAY_OFF;
    }

    if(main_config.relays_config[i].state == RELAY_DELAY_OFF_EXPIRED)
    {
      Serial.print("Relay");
      Serial.print(i);
      Serial.println(" turning off");
      if(main_config.relays_config[i].nc_no == relay_NC)
        mcp.digitalWrite(relay_expios[i], 0);
      else
        mcp.digitalWrite(relay_expios[i], 1);  

      main_config.relays_config[i].state = RELAY_OFF;
    }  
  }

  for(i=0; i<NO_RELAY_OUTS; i++)
    if(main_config.relays_config[i].on_delay_counter || main_config.relays_config[i].off_delay_counter)
    {
      detach_ticker = false;
      set_game_unsolved = false;
    }
  if(set_game_unsolved)
    game_is_solved = NOT_SOLVED;
}

void solve_game(void)
{
  uint8_t i;
  if(game_is_solved == NOT_SOLVED)
  {
    Serial.println("->Delayed solved");
    if(main_config.game_solved_delay)
    {
      game_is_solved = DELAYED_SOLVED;
      game_solved_counter = main_config.game_solved_delay;
    }
    else
      game_is_solved = WAIT_FOR_CHECK;
  }

  if(game_is_solved == WAIT_FOR_CHECK)
  {
    Serial.println("Solved");
    game_is_solved = SOLVED;
    sec_ticker.attach(1, ticker_handler);
    for(i=0; i<NO_RELAY_OUTS; i++)
    {
      if(main_config.relays_config[i].on_delay)
      {
        main_config.relays_config[i].on_delay_counter = main_config.relays_config[i].on_delay;
        main_config.relays_config[i].state = RELAY_DELAY_ON_STARTED;
      }
      else 
        main_config.relays_config[i].state = RELAY_DELAY_ON_EXPIRED;
    }
  }
}

//===============================================================
//                  SETUP
//===============================================================
void setup(void){
  uint8_t i;
  
  EEPROM.begin(NEEDED_EEPROM_SIZE);
  EEPROM.get(CONFIG_EEPROM_ADDR, main_config);
  if(main_config.sound_file[0] == 0xFF)
    reset_config();

  mcp.begin();
  
  Serial.begin(115200);
  Serial.println("");
  SPI.begin();

  print_config();
  
  WiFi.mode(WIFI_AP);           //Only Access point
  WiFi.softAP(ssid, password);  //Start HOTspot removing password will disable security
 
  IPAddress myIP = WiFi.softAPIP(); //Get IP address
  Serial.print("HotSpt IP:");
  Serial.println(myIP);

  MDNS.begin("modulocks");
  server.on("/", handleRoot);      //Which routine to handle at root location
  server.on("/config", HTTP_POST, handleConfig);
  
  server.begin();                  //Start server
  Serial.println("HTTP server started");

  reconfig();

  for(i=0; i<NO_RELAY_OUTS; i++)
    mcp.pinMode(relay_expios[i], OUTPUT);
  
}
//===============================================================
//                     LOOP
//===============================================================
void loop(void){
  server.handleClient();          //Handle client requests
  if(check_game_solved())
  {
    solve_game();
  } 
  else if(game_is_solved != SOLVED)
  {
    game_is_solved = NOT_SOLVED;
  }

  if(game_is_solved == SOLVED)
    relay_handler();
  delay(50);
}