#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <FS.h>
#include <ArduinoJson.h>

// WebServer object on port 80; we'll create it later once the WiFi Manager has found a network
ESP8266WebServer *myServer;
bool handleForm();  // web server handler for parameters from form
bool handleRoot();  // web server handler for root document

String getContentType(String filename); // convert the file extension to the MIME type
bool handleFileRead(String path);       // send the right file to the client (if it exists)

// This structure holds the config parms
struct Config {
  unsigned int projpin;   // which GPIO triggers projector
  unsigned int projpulse; // millseconds to pulse the projector relay
  unsigned int campin;    // which GPIO triggers camera
  unsigned int campulse;  // how long to pulse the camera trigger
  unsigned int slides;    // number of slides to scan
  unsigned int slideinterval; // pause after taking pic before advancing to next slide
  unsigned int settle;    // milliseconds to wait after advancing before taking pic
};
const char *cfgfile = "/config.json";  
Config cfg;                         // <- global configuration object

// pre-declare config functions
void loadConfiguration(const char *filename, Config &config); 
void saveConfiguration(const char *filename, const Config &config);
void printFile(const char *filename);

// pre-declare the function that controls the sequence of camera / projector operations
bool runSlideScanner();



void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;
  //reset settings - for testing
  //wifiManager.resetSettings();

  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  wifiManager.setTimeout(180);
  
  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  if(!wifiManager.autoConnect("AutoConnectAP")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  } 

  //if you get here you have connected to the WiFi
  Serial.println("Connected to WiFi:)");

  // Initialize SPIFFS
  if(!SPIFFS.begin()) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  // Should load default config if run for the first time
  Serial.println(F("Loading configuration..."));
  loadConfiguration(cfgfile, cfg);

  // Create configuration file
  Serial.println(F("Saving configuration..."));
  saveConfiguration(cfgfile, cfg);

  // Start a webserver on port 80
  myServer = new ESP8266WebServer(80);
  myServer->on("/action", HTTP_POST, handleForm);
  myServer->onNotFound([]() {                              // If the client requests any URI
    if (!handleFileRead(myServer->uri()))                  // send it if it exists
      myServer->send(404, "text/plain", "404: Not Found"); // otherwise, respond with a 404 (Not Found) error
  });

  myServer->begin();                           // Actually start the server
  Serial.println("HTTP server started");
 
}


void loop(void) {
  myServer->handleClient();
}

String getContentType(String filename) { // convert the file extension to the MIME type
  if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  return "text/plain";
}

bool handleRoot() {
  return handleFileRead("index.html");
}

bool handleFileRead(String path) { // send the right file to the client (if it exists)
  Serial.println("handleFileRead: " + path);
  if (path.endsWith("/")) path += "index.html";         // If a folder is requested, send the index file
  String contentType = getContentType(path);            // Get the MIME type
  if (SPIFFS.exists(path)) {                            // If the file exists
    File file = SPIFFS.open(path, "r");                 // Open it
    size_t sent = myServer->streamFile(file, contentType); // And send it to the client
    file.close();                                       // Then close the file again
    return true;
  }
  Serial.println("\tFile Not Found");
  return false;                                         // If the file doesn't exist, return false
}

bool handleForm() {
  Serial.println("handleForm: ");
  if (!myServer->hasArg("button")) {
    myServer->send(400, "text/plain", "400: Invalid Request");
    return false;
  } else {
    // we know we've got a button click; not sure if it's 'Save' or 'Start'
    // but either way, let's unpack the parameters
    cfg.projpin = myServer->arg("projpin").toInt(); 
    cfg.projpulse = myServer->arg("projpulse").toInt(); 
    cfg.slides = myServer->arg("slides").toInt(); 
    cfg.slideinterval = myServer->arg("slideinterval").toInt();
    cfg.campin = myServer->arg("campin").toInt(); 
    cfg.campulse = myServer->arg("campulse").toInt(); 
    cfg.settle = myServer->arg("settle").toInt(); 
  
    if (myServer->arg("button").startsWith("Save")) {
      // update saved configuration parameteres
      saveConfiguration(cfgfile, cfg);
      myServer->send(200, "text/plain", "Saved new configuration values.");
      
      // Dump config file
      Serial.println(F("Print config file..."));
      printFile(cfgfile);

      return true;
    } else if (myServer->arg("button").startsWith("Start")) {
      myServer->send(200, "text/plain", "Started processing slides... standby.");
      return runSlideScanner(
        cfg.projpin,
        cfg.projpulse,
        cfg.slides,
        cfg.slideinterval,
        cfg.campin,
        cfg.campulse,
        cfg.settle
      );    
    }
  }
}

// Loads the configuration from a file
void loadConfiguration(const char *filename, Config &config) {
  // Open file for reading
  File file = SPIFFS.open(filename, "r");

  // Allocate a temporary JsonDocument
  // Don't forget to change the capacity to match your requirements.
  // Use arduinojson.org/v6/assistant to compute the capacity.
  StaticJsonDocument<256> doc;

  // Deserialize the JSON document
  DeserializationError error = deserializeJson(doc, file);
  if (error)
    Serial.println(F("Failed to read file, using default configuration"));

  // Copy values from the JsonDocument to the Config
  config.projpin = doc["projpin"]; // 2
  config.projpulse = doc["projpulse"]; // 350
  config.slides = doc["slides"]; // 80
  config.slideinterval = doc["slideinterval"]; // 3000
  config.campin = doc["campin"]; // 4
  config.campulse = doc["campulse"]; // 350
  config.settle = doc["settle"]; // 500

  // Close the file (Curiously, File's destructor doesn't close the file)
  file.close();
}

// Saves the configuration to a file
void saveConfiguration(const char *filename, const Config &config) {
  // Delete existing file, otherwise the configuration is appended to the file
  SPIFFS.remove(filename);

  // Open file for writing
  File file = SPIFFS.open(filename, "w");
  if (!file) {
    Serial.println(F("Failed to create file"));
    return;
  }

  // Allocate a temporary JsonDocument
  // Don't forget to change the capacity to match your requirements.
  // Use arduinojson.org/assistant to compute the capacity.
  StaticJsonDocument<256> doc;

  // Set the values in the document
  doc["projpin"] = config.projpin;
  doc["projpulse"] = config.projpulse;
  doc["slides"] = config.slides;
  doc["slideinterval"] = config.slideinterval;
  doc["campin"] = config.campin;
  doc["campulse"] = config.campulse;
  doc["settle"] = config.settle;


  // Serialize JSON to file
  if (serializeJson(doc, file) == 0) {
    Serial.println(F("Failed to write to file"));
  }

  // Close the file
  file.close();
}

// Prints the content of a file to the Serial
void printFile(const char *filename) {
  // Open file for reading
  File file = SPIFFS.open(filename, "r");
  if (!file) {
    Serial.println(F("Failed to read file"));
    return;
  }

  // Extract each characters by one by one
  while (file.available()) {
    Serial.print((char)file.read());
  }
  Serial.println();

  // Close the file
  file.close();
}

bool runSlideScanner(
  unsigned int projpin,
  unsigned int projpulse,
  unsigned int slides,
  unsigned int slideinterval,
  unsigned int campin,
  unsigned int campulse,
  unsigned int settle 
) {
  Serial.println("Running processing batch with the following parameters:");
  Serial.println(projpin); 
  Serial.println(projpulse); 
  Serial.println(slides); 
  Serial.println(slideinterval);
  Serial.println(campin); 
  Serial.println(campulse); 
  Serial.println(settle);     
  
  // Initialise projector relay
  pinMode(cfg.projpin, OUTPUT);
  digitalWrite(cfg.projpin, HIGH);

  // Initialise camera relay
  pinMode(cfg.campin, OUTPUT);
  digitalWrite(cfg.campin, HIGH);
  Serial.println("Relay switches should now be open");

  for (int slide=0; slide < slides; slide++) {
    // Advance slide   
    digitalWrite(projpin, LOW); // closed
    delay(projpulse);
    digitalWrite(projpin, HIGH);  // open again

    // Pause to settle camera
    delay(settle);

    // Trigger camera
    digitalWrite(campin, LOW);  // closed
    delay(campulse);
    digitalWrite(campin, HIGH); // open again

    // Wait before advancing to next slide
    delay(slideinterval);    
  }
  return true;
}