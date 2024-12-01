#include <Arduino.h>

/***************************************************************************************************************************************
                                                                                                                                                        
    Project:         Webspector PLUS- WebServer based Spectrum Analyzer
    Target Platform: ESP32                                                                                                                                                                                                                                                                                       *
    Version: 1.0
    Hardware setup: See github
                                                                                                                                                                                                                                                                                                            
    Mark Donners
    The Electronic Engineer
    Website:   www.theelectronicengineer.nl
    facebook:  https://www.facebook.com/TheelectronicEngineer
    youtube:   https://www.youtube.com/channel/UCm5wy-2RoXGjG2F9wpDFF3w
    github:    https://github.com/donnersm
                                                                                                                                                      
 *   This sketch consists of 2 files; this one and the Settings.h file. Make sure both are in the same directory!                                                                                                                                                  
 *   
 *   All settings that you can change are in the Settings.h file
 *   Do not change settings below
 ****************************************************************************************************************************************/

#define VERSION     "V1.0"

//included files
#include "Settings.h"
#include "data.h"
// Declare the debugging level then include the header file
#define DEBUGLEVEL DEBUGLEVEL_DEBUGGING
// #define DEBUGLEVEL DEBUGLEVEL_NONE
#include "debug.h"

//general libaries
#include <arduinoFFT.h>                                 //libary for FFT analysis
#include <EasyButton.h>                                 //libary for handling buttons
#include <SPIFFS.h>
#include <ArduinoJson.h>

//libaries for webinterface
#define HTTP_PORT 80
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>

AsyncWebServer server(HTTP_PORT);
AsyncWebSocket ws("/ws");
//#include <WiFi.h>
//#include <WebServer.h>
//#include <WebSocketsServer.h>
//#include <Ticker.h>
//#include <WiFiManager.h>                                //The magic setup for wifi! If you need to setup your WIFI, hold the mode button during boot up.

//Driver for I2S ADC Conversion
#include <driver/i2s.h>
#include <driver/adc.h>

/********************************************************
 * Variables and stuff that don't need changing         *
 * ADC1_CHANNEL_0 GPI036 VP                             *
 * ADC1_CHANNEL_3 GPI039 VN                             *
 * ADC1_CHANNEL_6 GPI034 D34                            *
 * ADC1_CHANNEL_7 GPI035 D35                            *
 * ADC1_CHANNEL_4 GPI032 D32                            *
 * ADC1_CHANNEL_5 GPI033 D33                            *
/*******************************************************/
const i2s_port_t I2S_PORT = I2S_NUM_0;                  //*
#define ADC_INPUT ADC1_CHANNEL_0                        //*
#define ARRAYSIZE(a)    (sizeof(a)/sizeof(a[0]))        //*
uint16_t offset = (int)ADC_INPUT * 0x1000 + 0xFFF;      //*
double vReal[SAMPLEBLOCK];                              //*
double vImag[SAMPLEBLOCK];                              //*
int16_t samples[SAMPLEBLOCK];                           //*
byte peak[65];                                          //*
int oldBarHeights[65];                                  //*
volatile float FreqBins[65];                            //*
float FreqBinsOld[65];                                  //*
float FreqBinsNew[65];                                  //*
/********************************************************/

/* Create FFT object */
ArduinoFFT<double> FFT = ArduinoFFT<double>();

//************* web server setup *************************************************************************************************************************
TaskHandle_t WebserverTask;                             // setting up the task handler for webserver                                                  //**
bool      webtoken =          false;                    // this is a flag so that the webserver noise when the other core has new data                //**
//WebServer server(80);                                   // more webserver stuff                                                                       //**
//WiFiManager wm;                                         // Wifi Manager init                                                                          //**
//WebSocketsServer webSocket = WebSocketsServer(81);      // Adding a websocket to the server                                                           //**
//************* web server setup end**********************************************************************************************************************

// ----------------------------------------------------------------------------
// Definition of the LED component
// ----------------------------------------------------------------------------

struct Led
{
    // state variables
    uint8_t pin;
    bool on;

    // methods for update state of onboard led
    void update()
    {
        digitalWrite(pin, on ? HIGH : LOW);
    }
};

// ----------------------------------------------------------------------------
// Definition of objects
// ----------------------------------------------------------------------------

Led onboard_led = {LED_BUILTIN, false};

// ----------------------------------------------------------------------------
// SPIFFS initialization
// ----------------------------------------------------------------------------

void initSPIFFS()
{
    if (!SPIFFS.begin())
    {
        debuglnD("Cannot mount SPIFFS volume...");
        while (1)
        {
            onboard_led.on = millis() % 200 < 50;
            onboard_led.update();
        }
    }
}

// ----------------------------------------------------------------------------
// Connecting to the WiFi network
// ----------------------------------------------------------------------------

void initWiFi()
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.printf("Trying to connect [%s] ", WiFi.macAddress().c_str());
    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.print(".");
        delay(500);
    }
    Serial.printf(" %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("Listo!\nAbre http://%s.local en navegador\n", WEB_NAME);
    Serial.print("o en la IP: ");
    Serial.println(WiFi.localIP());

    if (!MDNS.begin(WEB_NAME))
    {
        debuglnD("Error configurando mDNS!");
        while (1)
        {
            delay(1000);
        }
    }
    debuglnD("mDNS configurado");
}

String processor(const String &var)
{
}

void onRootRequest(AsyncWebServerRequest *request)
{
    request->send(SPIFFS, "/index.html", "text/html", false, processor);
}

// Initialize webserver URLs
void initWebServer()
{
    server.on("/", onRootRequest);
    server.on("/wifi-info", HTTP_GET, [](AsyncWebServerRequest *request)
    {
      AsyncResponseStream *response = request->beginResponseStream("application/json");
    DynamicJsonDocument json(1024);
    json["status"] = "ok";
    json["ssid"] = WiFi.SSID();
    json["ip"] = WiFi.localIP().toString();
    json["rssi"] = WiFi.RSSI();
    serializeJson(json, *response);
    request->send(response); });

    server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
    server.onNotFound([](AsyncWebServerRequest *request)
                    { request->send(400, "text/plain", "Not found"); });
    //ElegantOTA.begin(&server); // Start ElegantOTA
    server.begin();
    debuglnD("HTTP server started");
    MDNS.addService("http", "tcp", 80);
}

// ----------------------------------------------------------------------------
// WebSocket initialization
// ----------------------------------------------------------------------------

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len)
{
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
    switch (type)
    {
    case WS_EVT_CONNECT:
        Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
        break;
    case WS_EVT_DISCONNECT:
        Serial.printf("WebSocket client #%u disconnected\n", client->id());
        break;
    case WS_EVT_DATA:
        handleWebSocketMessage(arg, data, len);
        break;
    case WS_EVT_PONG:
        Serial.printf("ws[%s][%u] pong[%u]: %s\n", server->url(), client->id(), len, (len) ? (char *)data : "");
    case WS_EVT_ERROR:
        Serial.printf("ws[%s][%u] error(%u): %s\n", server->url(), client->id(), *((uint16_t *)arg), (char *)data);
        break;
    }
}

void initWebSocket()
{
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
    debuglnD("WebSocket server started");
}

//****************************************************************************************
// below is is function that is needed when changing the number of bands during runtime, do not change
void SetNumberofBands(int bandnumber) {
  switch (bandnumber)
  {
    case 8:
      for (int x = 0; x < bandnumber; x++)
      {
        BandCutoffTable[x] = BandCutoffTable8[x];
        labels[x] = labels8[x];
      }
      break;

    case 16:
      for (int x = 0; x < bandnumber; x++)
      {
        BandCutoffTable[x] = BandCutoffTable16[x];
        labels[x] = labels16[x];
      }
      break;

    case 24:
      for (int x = 0; x < bandnumber; x++)
      {
        BandCutoffTable[x] = BandCutoffTable24[x];
        labels[x] = labels24[x];
      }

      break;

    case 32:
      for (int x = 0; x < bandnumber; x++)
      {
        BandCutoffTable[x] = BandCutoffTable32[x];
        labels[x] = labels32[x];
      }

      break;
    case 64:
      for (int x = 0; x < bandnumber; x++)
      {
        BandCutoffTable[x] = BandCutoffTable64[x];
        labels[x] = labels64[x];
      }

      break;
  }

}

//*************Button setup ******************************************************************************************************************************
EasyButton ModeBut(MODE_BUTTON_PIN);                    //defining the button                                                                         //**
// Mode button 1 short press                                                                                                                          //**
// will result in changing the number of bands                                                                                                        //**
void onPressed() {                                                                                                                                    //**
  Serial.println("Mode Button has been pressed!");                                                                                                    //**
  if (numBands == 8)numBands = 16;                                                                                                                    //**
  else if (numBands == 16)numBands = 24;                                                                                                              //**
  else if (numBands == 24)numBands = 32;                                                                                                              //**
  else if (numBands == 32)numBands = 64;                                                                                                              //**
  else if (numBands == 64)numBands = 8;                                                                                                               //**
  SetNumberofBands(numBands);                                                                                                                         //**
  Serial.printf("New number of bands=%d\n", numBands);                                                                                                //**
}                                                                                                                                                     //**
//*************Button setup end***************************************************************************************************************************

//****************************************************************************************
// Return the frequency corresponding to the Nth sample bucket.  Skips the first two
// buckets which are overall amplitude and something else.
int BucketFrequency(int iBucket) {
  if (iBucket <= 1)return 0;
  int iOffset = iBucket - 2;
  return iOffset * (I2S_SAMPLE_RATE / 2) / (SAMPLEBLOCK / 2);
}

//****************************************************************************************
/*
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  // Do something with the data from the client
  if (type == WStype_TEXT) {
    Serial.println("websocket event Triggered");
  }
}*/
//****************************************************************************************

//****************************************************************************************
void SendData() {
  String json = "[";
  for (int i = 0; i < numBands; i++) {
    if (i > 0) {
      json += ", ";
    }
    json += "{\"bin\":";
    json += "\"" + labels[i] + "\"";
    json += ", \"value\":";
    json += String(FreqBins[i]);
    json += "}";
  }
  json += "]";
  webSocket.broadcastTXT(json.c_str(), json.length());
}
//****************************************************************************************

//****************************************************************************************
//Task1code: webserver runs on separate core so that WIFI low signal doesn't freeze up program on other core
void Task1code( void * pvParameters ) {
  delay(3000);
  Serial.print("Webserver task is  running on core ");
  Serial.println(xPortGetCoreID());
  int gHue = 0;
  for (;;) {
    wm.process();
    webSocket.loop();
    server.handleClient();
    if (webtoken == true) {
      // lets smooth out the data we send
      for (int cnt = 0; cnt < numBands; cnt++) {
        FreqBinsNew[cnt] = FreqBins[cnt];
        if (FreqBinsNew[cnt] < FreqBinsOld[cnt]) {
          FreqBins[cnt] = max(FreqBinsOld[cnt] - Speedfilter, FreqBinsNew[cnt]);
          if (FreqBins[cnt] > 1.0)FreqBins[cnt] = 1; //to prevent glitch when changing number of channels during runtime
        }
        else if (FreqBinsNew[cnt] > FreqBinsOld[cnt]) {
          FreqBins[cnt] = FreqBinsNew[cnt];
        }
        FreqBinsOld[cnt] = FreqBins[cnt];
      }
      // done smoothing now send the data
      SendData(); // webbrowser
      webtoken = false;
    }
  }
}
//****************************************************************************************

//****************************************************************************************
// below is the function to initialize the I2S functionality, do not change
void setupI2S() {
  Serial.println("Configuring I2S...");
  esp_err_t err;

  // The I2S config as per the example
  const i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_ADC_BUILT_IN),
    .sample_rate = I2S_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT, // could only get it to work with 32bits
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, // although the SEL config should be left, it seems to transmit on right
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,  // I2S format
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,     // Interrupt level 1
    .dma_buf_count = 2,                           // number of buffers
    .dma_buf_len = SAMPLEBLOCK,                     // samples per buffer
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  // Configuring the I2S driver and pins.
  // This function must be called before any I2S driver read/write operations.

  Serial.printf("Attempting to setup I2S ADC with sampling frequency %d Hz\n", I2S_SAMPLE_RATE);
  if(ESP_OK != i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL)){
    Serial.printf("Error installing I2S. Halt!");
    while(true);
  }
  if(ESP_OK != i2s_set_adc_mode(ADC_UNIT_1, ADC_INPUT)){
    Serial.printf("Error setting up ADC. Halt!");
    while(true);
  }

  Serial.printf("I2S ADC setup ok\n");
}
//****************************************************************************************

//****************************************************************************************
// Below is the Setup function that is run once on startup 

void setup() {

  initSPIFFS();
  initWiFi();
  initWebSocket();
  initWebServer();
  //create a task that will be executed in the Task1code() function, with priority 1 and executed on core 0
  // this will run the webinterface datatransfer.
  xTaskCreatePinnedToCore(
    Task1code,                                          /* Task function. */
    "WebserverTask",                                    /* name of task. */
    10000,                                              /* Stack size of task */
    NULL,                                               /* parameter of the task */
    4,                                                  /* priority of the task */
    &WebserverTask,                                     /* Task handle to keep track of created task */
    0);                                                 /* pin task to core 0 */

  delay(500);
  Serial.begin(115200);
  Serial.println("Setting up Audio Input I2S");
  // Initialize the I2S peripheral
  setupI2S();
  delay(100);
  i2s_adc_enable(I2S_NUM_0);
  Serial.println("Audio input setup completed");
  ModeBut.onPressed(onPressed);


  //if (digitalRead(MODE_BUTTON_PIN) == 0) {              //reset saved settings is mode button is pressed and hold during startup
  //  Serial.println("button pressed on startup, WIFI settings will be reset");
  //  wm.resetSettings();
  //}

  //wm.setConfigPortalBlocking(false);                    //Try to connect WiFi, then create AP but if no success then don't block the program
  // If needed, it will be handled in core 0 later
 // wm.autoConnect("ESP32_AP", "");

  Serial.println(Projectinfo);                          // print some info about the project
  //server.on("/", []() {                                 // this will load the actual html webpage to be displayed
  //  server.send_P(200, "text/html", webpage);
  //});

  //server.begin();                                       // now start the server
  //Serial.println("HTTP server started");
  //webSocket.begin();
  //webSocket.onEvent(webSocketEvent);
  SetNumberofBands(numBands);

 
}
//****************************************************************************************


//****************************************************************************************
// Main loop running on 1 core while the web update is running on another core
void loop() {
  size_t bytesRead = 0;
  int TempADC = 0;
  ModeBut.read();

  //############ Step 1: read samples from the I2S Buffer ##################
  i2s_read(I2S_PORT,
           (void*)samples,
           sizeof(samples),
           &bytesRead,   // workaround This is the actual buffer size last half will be empty but why?
           portMAX_DELAY); // no timeout

  if (bytesRead != sizeof(samples)) {
    Serial.printf("Could only read %u bytes of %u in FillBufferI2S()\n", bytesRead, sizeof(samples));
  }

  //############ Step 2: compensate for Channel number and offset, safe all to vReal Array   ############
  for (uint16_t i = 0; i < ARRAYSIZE(samples); i++) {
    vReal[i] = offset - samples[i];
    vImag[i] = 0.0; //Imaginary part must be zeroed in case of looping to avoid wrong calculations and overflows
  }

  //############ Step 3: Do FFT on the VReal array  ############
  // compute FFT
  FFT.dcRemoval();
  FFT.windowing(vReal, SAMPLEBLOCK, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  FFT.compute(vReal, vImag, SAMPLEBLOCK, FFT_FORWARD);
  FFT.complexToMagnitude(vReal, vImag, SAMPLEBLOCK);
  FFT.majorPeak(vReal, SAMPLEBLOCK, I2S_SAMPLE_RATE);
  for (int i = 0; i < numBands; i++) {
    FreqBins[i] = 0;
  }
  
  //############ Step 4: Fill the frequency bins with the FFT Samples ############
  for (int i = 2; i < SAMPLEBLOCK / 2; i++) {
    if (vReal[i] > NoiseTresshold) {
      int freq = BucketFrequency(i);
      int iBand = 0;
      while (iBand < numBands) {
        if (freq < BandCutoffTable[iBand])break;
        iBand++;
      }
      if (iBand > numBands)iBand = numBands;
      FreqBins[iBand] += vReal[i];
    }
  }


  //############ Step 5: Averaging and making it all fit on screen
  static float lastAllBandsPeak = 0.0f;
  float allBandsPeak = 0;
  for (int i = 0; i < numBands; i++) {
    if (FreqBins[i] > allBandsPeak) {
      allBandsPeak = FreqBins[i];
    }
  }
  if (allBandsPeak < 1)allBandsPeak = 1;
  allBandsPeak = max(allBandsPeak, ((lastAllBandsPeak * (GAIN_DAMPEN - 1)) + allBandsPeak) / GAIN_DAMPEN); // Dampen rate of change a little bit on way down
  lastAllBandsPeak = allBandsPeak;
  if (allBandsPeak < 80000)allBandsPeak = 80000;
  for (int i = 0; i < numBands; i++)FreqBins[i] /= (allBandsPeak * 1.0f);
  webtoken = true;                  // set marker so that other core can process data
} // loop end
//****************************************************************************************



