#if ARDUINO_USB_CDC_ON_BOOT
#error "The menu option 'Tools / USB CDC On Boot' must be set to 'Disabled'"
#elif ARDUINO_USB_DFU_ON_BOOT
#error "The menu option 'Tools / USB DFU On Boot' must be set to 'Disabled'"
#elif ARDUINO_USB_MSC_ON_BOOT
#error "The menu option 'Tools / USB MSC On Boot' must be set to 'Disabled'"
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#include <FS.h>
#include "WiFi.h"
#include "ESPAsyncWebServer.h"
#include "esp_task_wdt.h"
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include "SD_MMC.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "USB.h"
#include "USBMSC.h"
#include "exfathax.h"


#define USETFT false  // use dongle lcd screen

#if USETFT
#include <TFT_eSPI.h>  // https://github.com/stooged/TFT_eSPI
TFT_eSPI tft = TFT_eSPI();  
long tftCnt = 0;
bool tftOn = true;
#endif


                    // use PsFree [ true / false ]
#define PSFREE true // use the newer psfree webkit exploit.
                    // this is fairly stable but may fail which will require you to try and load the payload again.

                    // enable fan threshold [ true / false ]
#define FANMOD true // this will include a function to set the consoles fan ramp up temperature in °C
                    // this will not work if the board is a esp32 and the usb control is disabled.

//-------------------DEFAULT SETTINGS------------------//

                       // use config.ini [ true / false ]
#define USECONFIG true // this will allow you to change these settings below via the admin webpage or the config.ini file.
                       // if you want to permanently use the values below then set this to false.

// create access point
boolean startAP = true;
String AP_SSID = "PS4_WEB_AP";
String AP_PASS = "password";
IPAddress Server_IP(10, 1, 1, 1);
IPAddress Subnet_Mask(255, 255, 255, 0);

// connect to wifi
boolean connectWifi = false;
String WIFI_SSID = "Home_WIFI";
String WIFI_PASS = "password";
String WIFI_HOSTNAME = "ps4.local";

// server port
int WEB_PORT = 80;

// Auto Usb Wait(milliseconds)
int USB_WAIT = 10000;

// Displayed firmware version
#define firmwareVer "1.00"

// ESP sleep after x minutes
boolean espSleep = false;
int TIME2SLEEP = 30; // minutes

//-----------------------------------------------------//

#include "Loader.h"
#include "Pages.h"

#if FANMOD
#include "fan.h"
#endif
DNSServer dnsServer;
AsyncWebServer server(WEB_PORT);
boolean hasEnabled = false;
boolean efhEnabled = false;
long enTime = 0;
int ftemp = 70;
long bootTime = 0;
File upFile;
USBMSC dev;
//USBCDC USBSerial;
#define MOUNT_POINT "/sdcard"
#define PDESC "PS4-Dongle"
#define MDESC "T-D-S3"
sdmmc_card_t *card;


String split(String str, String from, String to)
{
  String tmpstr = str;
  tmpstr.toLowerCase();
  from.toLowerCase();
  to.toLowerCase();
  int pos1 = tmpstr.indexOf(from);
  int pos2 = tmpstr.indexOf(to, pos1 + from.length());
  String retval = str.substring(pos1 + from.length(), pos2);
  return retval;
}

bool instr(String str, String search)
{
  int result = str.indexOf(search);
  if (result == -1)
  {
    return false;
  }
  return true;
}

String formatBytes(size_t bytes)
{
  if (bytes < 1024)
  {
    return String(bytes) + " B";
  }
  else if (bytes < (1024 * 1024))
  {
    return String(bytes / 1024.0) + " KB";
  }
  else if (bytes < (1024 * 1024 * 1024))
  {
    return String(bytes / 1024.0 / 1024.0) + " MB";
  }
  else
  {
    return String(bytes / 1024.0 / 1024.0 / 1024.0) + " GB";
  }
}

String urlencode(String str)
{
  String encodedString = "";
  char c;
  char code0;
  char code1;
  char code2;
  for (int i = 0; i < str.length(); i++)
  {
    c = str.charAt(i);
    if (c == ' ')
    {
      encodedString += '+';
    }
    else if (isalnum(c))
    {
      encodedString += c;
    }
    else
    {
      code1 = (c & 0xf) + '0';
      if ((c & 0xf) > 9)
      {
        code1 = (c & 0xf) - 10 + 'A';
      }
      c = (c >> 4) & 0xf;
      code0 = c + '0';
      if (c > 9)
      {
        code0 = c - 10 + 'A';
      }
      code2 = '\0';
      encodedString += '%';
      encodedString += code0;
      encodedString += code1;
    }
    yield();
  }
  encodedString.replace("%2E", ".");
  return encodedString;
}

void sendwebmsg(AsyncWebServerRequest *request, String htmMsg)
{
  String tmphtm = "<!DOCTYPE html><html><head><link rel=\"stylesheet\" href=\"style.css\"></head><center><br><br><br><br><br><br>" + htmMsg + "</center></html>";
  request->send(200, "text/html", tmphtm);
}

void handleFwUpdate(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
{
  if (!index)
  {
    String path = request->url();
    if (path != "/update.html")
    {
      request->send(500, "text/plain", "Internal Server Error");
      return;
    }
    if (!filename.equals("fwupdate.bin"))
    {
      sendwebmsg(request, "Invalid update file: " + filename);
      return;
    }
    if (!filename.startsWith("/"))
    {
      filename = "/" + filename;
    }
    //USBSerial.printf("Update Start: %s\n", filename.c_str());
    if (!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000))
    {
      Update.printError(Serial);
      sendwebmsg(request, "Update Failed: " + String(Update.errorString()));
    }
  }
  if (!Update.hasError())
  {
    if (Update.write(data, len) != len)
    {
      Update.printError(Serial);
      sendwebmsg(request, "Update Failed: " + String(Update.errorString()));
    }
  }
  if (final)
  {
    if (Update.end(true))
    {
      //USBSerial.printf("Update Success: %uB\n", index+len);
      String tmphtm = "<!DOCTYPE html><html><head><meta http-equiv=\"refresh\" content=\"8; url=/info.html\"><style type=\"text/css\">body {background-color: #1451AE; color: #ffffff; font-size: 20px; font-weight: bold; margin: 0 0 0 0.0; padding: 0.4em 0.4em 0.4em 0.6em;}</style></head><center><br><br><br><br><br><br>Update Success, Rebooting.</center></html>";
      request->send(200, "text/html", tmphtm);
      delay(1000);
      ESP.restart();
    }
    else
    {
      Update.printError(Serial);
    }
  }
}

void handlePayloads(AsyncWebServerRequest *request)
{
  File dir = SD_MMC.open("/");
  String output = "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>ESP Server</title><link rel=\"stylesheet\" href=\"style.css\"><style>body { background-color: #1451AE; color: #ffffff; font-size: 14px; font-weight: bold; margin: 0 0 0 0.0; overflow-y:hidden; text-shadow: 3px 2px DodgerBlue;}</style><script>function setpayload(payload,title,waittime){ sessionStorage.setItem('payload', payload); sessionStorage.setItem('title', title); sessionStorage.setItem('waittime', waittime);  window.open('loader.html', '_self');}</script></head><body><center><h1>9.00 Payloads</h1>";
  int cntr = 0;
  int payloadCount = 0;
  if (USB_WAIT < 5000)
  {
    USB_WAIT = 5000;
  } // correct unrealistic timing values
  if (USB_WAIT > 25000)
  {
    USB_WAIT = 25000;
  }

  while (dir)
  {
    File file = dir.openNextFile();
    if (!file)
    {
      dir.close();
      break;
    }
    String fname = String(file.name());
    if (fname.endsWith(".gz"))
    {
      fname = fname.substring(0, fname.length() - 3);
    }
    if (fname.length() > 0 && fname.endsWith(".bin") && !file.isDirectory())
    {
      payloadCount++;
      String fnamev = fname;
      fnamev.replace(".bin", "");
      output += "<a onclick=\"setpayload('" + urlencode(fname) + "','" + fnamev + "','" + String(USB_WAIT) + "')\"><button class=\"btn\">" + fnamev + "</button></a>&nbsp;";
      cntr++;
      if (cntr == 4)
      {
        cntr = 0;
        output += "<p></p>";
      }
    }
    file.close();
    esp_task_wdt_reset();
  }

#if FANMOD
  payloadCount++;
  output += "<br><p><a onclick='setfantemp()'><button class='btn'>Set Fan Threshold</button></a><select id='temp' class='slct'></select></p><script>function setfantemp(){var e = document.getElementById('temp');var temp = e.value;var xhr = new XMLHttpRequest();xhr.open('POST', 'setftemp', true);xhr.onload = function(e) {if (this.status == 200) {sessionStorage.setItem('payload', 'fant.bin'); sessionStorage.setItem('title', 'Fan Temp ' + temp + ' &deg;C'); localStorage.setItem('temp', temp); sessionStorage.setItem('waittime', '10000');  window.open('loader.html', '_self');}};xhr.send('temp=' + temp);}var stmp = localStorage.getItem('temp');if (!stmp){stmp = 70;}for(var i=55; i<=85; i=i+5){var s = document.getElementById('temp');var o = document.createElement('option');s.options.add(o);o.text = i + String.fromCharCode(32,176,67);o.value = i;if (i == stmp){o.selected = true;}}</script>";
#endif

  if (payloadCount == 0)
  {
    output += "<msg>No .bin payloads found<br>You need to upload the payloads to the ESP32 board.<br>in the arduino ide select <b>Tools</b> &gt; <b>ESP32 Sketch Data Upload</b><br>or<br>Using a pc/laptop connect to <b>" + AP_SSID + "</b> and navigate to <a href=\"/admin.html\"><u>http://" + WIFI_HOSTNAME + "/admin.html</u></a> and upload the .bin payloads using the <b>File Uploader</b></msg></center></body></html>";
  }
  output += "</center></body></html>";
  request->send(200, "text/html", output);
}

#if USECONFIG
void handleConfig(AsyncWebServerRequest *request)
{
  if (request->hasParam("ap_ssid", true) && request->hasParam("ap_pass", true) && request->hasParam("web_ip", true) && request->hasParam("web_port", true) && request->hasParam("subnet", true) && request->hasParam("wifi_ssid", true) && request->hasParam("wifi_pass", true) && request->hasParam("wifi_host", true) && request->hasParam("usbwait", true))
  {
    AP_SSID = request->getParam("ap_ssid", true)->value();
    if (!request->getParam("ap_pass", true)->value().equals("********"))
    {
      AP_PASS = request->getParam("ap_pass", true)->value();
    }
    WIFI_SSID = request->getParam("wifi_ssid", true)->value();
    if (!request->getParam("wifi_pass", true)->value().equals("********"))
    {
      WIFI_PASS = request->getParam("wifi_pass", true)->value();
    }
    String tmpip = request->getParam("web_ip", true)->value();
    String tmpwport = request->getParam("web_port", true)->value();
    String tmpsubn = request->getParam("subnet", true)->value();
    String WIFI_HOSTNAME = request->getParam("wifi_host", true)->value();
    String tmpua = "false";
    String tmpcw = "false";
    String tmpslp = "false";
    if (request->hasParam("useap", true))
    {
      tmpua = "true";
    }
    if (request->hasParam("usewifi", true))
    {
      tmpcw = "true";
    }
    if (request->hasParam("espsleep", true))
    {
      tmpslp = "true";
    }
    if (tmpua.equals("false") && tmpcw.equals("false"))
    {
      tmpua = "true";
    }
    int USB_WAIT = request->getParam("usbwait", true)->value().toInt();
    int TIME2SLEEP = request->getParam("sleeptime", true)->value().toInt();
    File iniFile = SD_MMC.open("/config.ini", "w");
    if (iniFile)
    {
      iniFile.print("\r\nAP_SSID=" + AP_SSID + "\r\nAP_PASS=" + AP_PASS + "\r\nWEBSERVER_IP=" + tmpip + "\r\nWEBSERVER_PORT=" + tmpwport + "\r\nSUBNET_MASK=" + tmpsubn + "\r\nWIFI_SSID=" + WIFI_SSID + "\r\nWIFI_PASS=" + WIFI_PASS + "\r\nWIFI_HOST=" + WIFI_HOSTNAME + "\r\nUSEAP=" + tmpua + "\r\nCONWIFI=" + tmpcw + "\r\nUSBWAIT=" + USB_WAIT + "\r\nESPSLEEP=" + tmpslp + "\r\nSLEEPTIME=" + TIME2SLEEP + "\r\n");
      iniFile.close();
    }
    String htmStr = "<!DOCTYPE html><html><head><meta http-equiv=\"refresh\" content=\"8; url=/info.html\"><style type=\"text/css\">#loader {z-index: 1;width: 50px;height: 50px;margin: 0 0 0 0;border: 6px solid #f3f3f3;border-radius: 50%;border-top: 6px solid #3498db;width: 50px;height: 50px;-webkit-animation: spin 2s linear infinite;animation: spin 2s linear infinite; } @-webkit-keyframes spin {0%{-webkit-transform: rotate(0deg);}100%{-webkit-transform: rotate(360deg);}}@keyframes spin{0%{ transform: rotate(0deg);}100%{transform: rotate(360deg);}}body {background-color: #1451AE; color: #ffffff; font-size: 20px; font-weight: bold; margin: 0 0 0 0.0; padding: 0.4em 0.4em 0.4em 0.6em;} #msgfmt {font-size: 16px; font-weight: normal;}#status {font-size: 16px; font-weight: normal;}</style></head><center><br><br><br><br><br><p id=\"status\"><div id='loader'></div><br>Config saved<br>Rebooting</p></center></html>";
    request->send(200, "text/html", htmStr);
    delay(1000);
    ESP.restart();
  }
  else
  {
    request->redirect("/config.html");
  }
}
#endif

void handleReboot(AsyncWebServerRequest *request)
{
  //USBSerial.print("Rebooting ESP");
  AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", rebooting_gz, sizeof(rebooting_gz));
  response->addHeader("Content-Encoding", "gzip");
  request->send(response);
  delay(1000);
  ESP.restart();
}

#if USECONFIG
void handleConfigHtml(AsyncWebServerRequest *request)
{
  String tmpUa = "";
  String tmpCw = "";
  String tmpSlp = "";
  if (startAP)
  {
    tmpUa = "checked";
  }
  if (connectWifi)
  {
    tmpCw = "checked";
  }
  if (espSleep)
  {
    tmpSlp = "checked";
  }

  String htmStr = "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>Config Editor</title><style type=\"text/css\">body {background-color: #1451AE; color: #ffffff; font-size: 14px;font-weight: bold;margin: 0 0 0 0.0;padding: 0.4em 0.4em 0.4em 0.6em;}input[type=\"submit\"]:hover {background: #ffffff;color: green;}input[type=\"submit\"]:active{outline-color: green;color: green;background: #ffffff; }table {font-family: arial, sans-serif;border-collapse: collapse;}td {border: 1px solid #dddddd;text-align: left;padding: 8px;}th {border: 1px solid #dddddd; background-color:gray;text-align: center;padding: 8px;}</style></head><body><form action=\"/config.html\" method=\"post\"><center><table><tr><th colspan=\"2\"><center>Access Point</center></th></tr><tr><td>AP SSID:</td><td><input name=\"ap_ssid\" value=\"" + AP_SSID + "\"></td></tr><tr><td>AP PASSWORD:</td><td><input name=\"ap_pass\" value=\"********\"></td></tr><tr><td>AP IP:</td><td><input name=\"web_ip\" value=\"" + Server_IP.toString() + "\"></td></tr><tr><td>SUBNET MASK:</td><td><input name=\"subnet\" value=\"" + Subnet_Mask.toString() + "\"></td></tr><tr><td>START AP:</td><td><input type=\"checkbox\" name=\"useap\" " + tmpUa + "></td></tr><tr><th colspan=\"2\"><center>Web Server</center></th></tr><tr><td>WEBSERVER PORT:</td><td><input name=\"web_port\" value=\"" + String(WEB_PORT) + "\"></td></tr><tr><th colspan=\"2\"><center>Wifi Connection</center></th></tr><tr><td>WIFI SSID:</td><td><input name=\"wifi_ssid\" value=\"" + WIFI_SSID + "\"></td></tr><tr><td>WIFI PASSWORD:</td><td><input name=\"wifi_pass\" value=\"********\"></td></tr><tr><td>WIFI HOSTNAME:</td><td><input name=\"wifi_host\" value=\"" + WIFI_HOSTNAME + "\"></td></tr><tr><td>CONNECT WIFI:</td><td><input type=\"checkbox\" name=\"usewifi\" " + tmpCw + "></td></tr><tr><th colspan=\"2\"><center>Auto USB Wait</center></th></tr><tr><td>WAIT TIME(ms):</td><td><input name=\"usbwait\" value=\"" + USB_WAIT + "\"></td></tr><tr><th colspan=\"2\"><center>ESP Sleep Mode</center></th></tr><tr><td>ENABLE SLEEP:</td><td><input type=\"checkbox\" name=\"espsleep\" " + tmpSlp + "></td></tr><tr><td>TIME TO SLEEP(minutes):</td><td><input name=\"sleeptime\" value=\"" + TIME2SLEEP + "\"></td></tr></table><br><input id=\"savecfg\" type=\"submit\" value=\"Save Config\"></center></form></body></html>";
  request->send(200, "text/html", htmStr);
}
#endif

void handleFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
{
  if (!index)
  {
    String path = request->url();
    if (path != "/upload.html")
    {
      request->send(500, "text/plain", "Internal Server Error");
      return;
    }
    if (!filename.startsWith("/"))
    {
      filename = "/" + filename;
    }
    if (filename.equals("/config.ini"))
    {
      return;
    }
    //USBSerial.printf("Upload Start: %s\n", filename.c_str());
    upFile = SD_MMC.open(filename, "w");
  }
  if (upFile)
  {
    upFile.write(data, len);
  }
  if (final)
  {
    upFile.close();
    //USBSerial.printf("upload Success: %uB\n", index+len);
  }
}

void handleConsoleUpdate(String rgn, AsyncWebServerRequest *request)
{
  String Version = "05.050.000";
  String sVersion = "05.050.000";
  String lblVersion = "5.05";
  String imgSize = "0";
  String imgPath = "";
  String xmlStr = "<?xml version=\"1.0\" ?><update_data_list><region id=\"" + rgn + "\"><force_update><system level0_system_ex_version=\"0\" level0_system_version=\"" + Version + "\" level1_system_ex_version=\"0\" level1_system_version=\"" + Version + "\"/></force_update><system_pup ex_version=\"0\" label=\"" + lblVersion + "\" sdk_version=\"" + sVersion + "\" version=\"" + Version + "\"><update_data update_type=\"full\"><image size=\"" + imgSize + "\">" + imgPath + "</image></update_data></system_pup><recovery_pup type=\"default\"><system_pup ex_version=\"0\" label=\"" + lblVersion + "\" sdk_version=\"" + sVersion + "\" version=\"" + Version + "\"/><image size=\"" + imgSize + "\">" + imgPath + "</image></recovery_pup></region></update_data_list>";
  request->send(200, "text/xml", xmlStr);
}

void handleInfo(AsyncWebServerRequest *request)
{
  float flashFreq = (float)ESP.getFlashChipSpeed() / 1000.0 / 1000.0;
  FlashMode_t ideMode = ESP.getFlashChipMode();
  String mcuType = CONFIG_IDF_TARGET;
  mcuType.toUpperCase();
  String output = "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>System Information</title><link rel=\"stylesheet\" href=\"style.css\"></head>";
  output += "<hr>###### Software ######<br><br>";
  output += "Firmware version " + String(firmwareVer) + "<br>";
  output += "SDK version: " + String(ESP.getSdkVersion()) + "<br><hr>";
  output += "###### Board ######<br><br>";
  output += "MCU: " + mcuType + "<br>";
  output += "Board: LilyGo T-Dongle-S3<br>";
  output += "Chip Id: " + String(ESP.getChipModel()) + "<br>";
  output += "CPU frequency: " + String(ESP.getCpuFreqMHz()) + "MHz<br>";
  output += "Cores: " + String(ESP.getChipCores()) + "<br><hr>";
  output += "###### Flash chip information ######<br><br>";
  output += "Flash chip Id: " + String(ESP.getFlashChipMode()) + "<br>";
  output += "Estimated Flash size: " + formatBytes(ESP.getFlashChipSize()) + "<br>";
  output += "Flash frequency: " + String(flashFreq) + " MHz<br>";
  output += "Flash write mode: " + String((ideMode == FM_QIO? "QIO" : ideMode == FM_QOUT? "QOUT": ideMode == FM_DIO? "DIO": ideMode == FM_DOUT? "DOUT": "UNKNOWN")) + "<br><hr>";
  output += "###### Storage information ######<br><br>";
  output += "Storage Device: SD<br>";
  output += "Total Size: " + formatBytes(SD_MMC.totalBytes()) + "<br>";
  output += "Used Space: " + formatBytes(SD_MMC.usedBytes()) + "<br>";
  output += "Free Space: " + formatBytes(SD_MMC.totalBytes() - SD_MMC.usedBytes()) + "<br><hr>";
  output += "###### Ram information ######<br><br>";
  output += "Ram size: " + formatBytes(ESP.getHeapSize()) + "<br>";
  output += "Free ram: " + formatBytes(ESP.getFreeHeap()) + "<br>";
  output += "Max alloc ram: " + formatBytes(ESP.getMaxAllocHeap()) + "<br><hr>";
  output += "###### Sketch information ######<br><br>";
  output += "Sketch hash: " + ESP.getSketchMD5() + "<br>";
  output += "Sketch size: " + formatBytes(ESP.getSketchSize()) + "<br>";
  output += "Free space available: " + formatBytes(ESP.getFreeSketchSpace() - ESP.getSketchSize()) + "<br><hr>";
  output += "</html>";
  request->send(200, "text/html", output);
}

#if USECONFIG
void writeConfig()
{
  File iniFile = SD_MMC.open("/config.ini", "w");
  if (iniFile)
  {
    String tmpua = "false";
    String tmpcw = "false";
    String tmpslp = "false";
    if (startAP)
    {
      tmpua = "true";
    }
    if (connectWifi)
    {
      tmpcw = "true";
    }
    if (espSleep)
    {
      tmpslp = "true";
    }
    iniFile.print("\r\nAP_SSID=" + AP_SSID + "\r\nAP_PASS=" + AP_PASS + "\r\nWEBSERVER_IP=" + Server_IP.toString() + "\r\nWEBSERVER_PORT=" + String(WEB_PORT) + "\r\nSUBNET_MASK=" + Subnet_Mask.toString() + "\r\nWIFI_SSID=" + WIFI_SSID + "\r\nWIFI_PASS=" + WIFI_PASS + "\r\nWIFI_HOST=" + WIFI_HOSTNAME + "\r\nUSEAP=" + tmpua + "\r\nCONWIFI=" + tmpcw + "\r\nUSBWAIT=" + USB_WAIT + "\r\nESPSLEEP=" + tmpslp + "\r\nSLEEPTIME=" + TIME2SLEEP + "\r\n");
    iniFile.close();
  }
}
#endif





void startAccessPoint()
{
  if (startAP)
  {
    //USBSerial.println("SSID: " + AP_SSID);
    //USBSerial.println("Password: " + AP_PASS);
    //USBSerial.println("");
    //USBSerial.println("WEB Server IP: " + Server_IP.toString());
    //USBSerial.println("Subnet: " + Subnet_Mask.toString());
    //USBSerial.println("WEB Server Port: " + String(WEB_PORT));
    //USBSerial.println("");
    WiFi.softAPConfig(Server_IP, Server_IP, Subnet_Mask);
    WiFi.softAP(AP_SSID.c_str(), AP_PASS.c_str());
    //USBSerial.println("WIFI AP started");
    dnsServer.setTTL(30);
    dnsServer.setErrorReplyCode(DNSReplyCode::ServerFailure);
    dnsServer.start(53, "*", Server_IP);
#if USETFT
    tft.print("AP: ");
    tft.println(AP_SSID);
    if (!connectWifi)
    {
      tft.print("Host: ");
      tft.println(WIFI_HOSTNAME);
    }
    tft.print("IP: ");
    tft.println(Server_IP.toString());
#endif
    //USBSerial.println("DNS server started");
    //USBSerial.println("DNS Server IP: " + Server_IP.toString());
  }
}

void connectToWIFI()
{
  if (connectWifi && WIFI_SSID.length() > 0 && WIFI_PASS.length() > 0)
  {
    WiFi.setAutoConnect(true);
    WiFi.setAutoReconnect(true);
    WiFi.hostname(WIFI_HOSTNAME);
    WiFi.begin(WIFI_SSID.c_str(), WIFI_PASS.c_str());
    //USBSerial.println("WIFI connecting");
    if (WiFi.waitForConnectResult() != WL_CONNECTED)
    {
      //USBSerial.println("Wifi failed to connect");
#if USETFT
      tft.setTextColor(TFT_RED, TFT_BLACK);          
      tft.println("Failed to connect to:");
      tft.setTextColor(TFT_WHITE, TFT_BLACK);  
      tft.println(WIFI_SSID);
#endif 
    }
    else
    {
      IPAddress LAN_IP = WiFi.localIP();
      if (LAN_IP)
      {
        //USBSerial.println("Wifi Connected");
        //USBSerial.println("WEB Server LAN IP: " + LAN_IP.toString());
        //USBSerial.println("WEB Server Port: " + String(WEB_PORT));
        //USBSerial.println("WEB Server Hostname: " + WIFI_HOSTNAME);
        String mdnsHost = WIFI_HOSTNAME;
        mdnsHost.replace(".local", "");
        MDNS.begin(mdnsHost.c_str());
#if USETFT        
        tft.print("Host: ");
        tft.println(WIFI_HOSTNAME);
        tft.print("IP: ");
        tft.println(LAN_IP.toString()); 
#endif
        if (!startAP)
        {
          dnsServer.setTTL(30);
          dnsServer.setErrorReplyCode(DNSReplyCode::ServerFailure);
          dnsServer.start(53, "*", LAN_IP);
          //USBSerial.println("DNS server started");
          //USBSerial.println("DNS Server IP: " + LAN_IP.toString());
        }
      }
    }
  }
}


void setup()
{

  //USBSerial.begin(115200);
  //USBSerial.println("Version: " + firmwareVer);
  //USBSerial.begin();

  pinMode(38, OUTPUT);
  digitalWrite(38, HIGH); 
 
#if USETFT   
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.pushImage(0, 0, 160, 320, logo);
  digitalWrite(38, LOW);
#endif 

  setup_SD();

  SD_MMC.setPins(12, 16, 14, 17, 21, 18);
  if (SD_MMC.begin())
  {

#if USECONFIG
    if (SD_MMC.exists("/config.ini"))
    {
      File iniFile = SD_MMC.open("/config.ini", "r");
      if (iniFile)
      {
        String iniData;
        while (iniFile.available())
        {
          char chnk = iniFile.read();
          iniData += chnk;
        }
        iniFile.close();

        if (instr(iniData, "AP_SSID="))
        {
          AP_SSID = split(iniData, "AP_SSID=", "\r\n");
          AP_SSID.trim();
        }

        if (instr(iniData, "AP_PASS="))
        {
          AP_PASS = split(iniData, "AP_PASS=", "\r\n");
          AP_PASS.trim();
        }

        if (instr(iniData, "WEBSERVER_IP="))
        {
          String strwIp = split(iniData, "WEBSERVER_IP=", "\r\n");
          strwIp.trim();
          Server_IP.fromString(strwIp);
        }

        if (instr(iniData, "SUBNET_MASK="))
        {
          String strsIp = split(iniData, "SUBNET_MASK=", "\r\n");
          strsIp.trim();
          Subnet_Mask.fromString(strsIp);
        }

        if (instr(iniData, "WIFI_SSID="))
        {
          WIFI_SSID = split(iniData, "WIFI_SSID=", "\r\n");
          WIFI_SSID.trim();
        }

        if (instr(iniData, "WIFI_PASS="))
        {
          WIFI_PASS = split(iniData, "WIFI_PASS=", "\r\n");
          WIFI_PASS.trim();
        }

        if (instr(iniData, "WIFI_HOST="))
        {
          WIFI_HOSTNAME = split(iniData, "WIFI_HOST=", "\r\n");
          WIFI_HOSTNAME.trim();
        }

        if (instr(iniData, "USEAP="))
        {
          String strua = split(iniData, "USEAP=", "\r\n");
          strua.trim();
          if (strua.equals("true"))
          {
            startAP = true;
          }
          else
          {
            startAP = false;
          }
        }

        if (instr(iniData, "CONWIFI="))
        {
          String strcw = split(iniData, "CONWIFI=", "\r\n");
          strcw.trim();
          if (strcw.equals("true"))
          {
            connectWifi = true;
          }
          else
          {
            connectWifi = false;
          }
        }

        if (instr(iniData, "USBWAIT="))
        {
          String strusw = split(iniData, "USBWAIT=", "\r\n");
          strusw.trim();
          USB_WAIT = strusw.toInt();
        }

        if (instr(iniData, "ESPSLEEP="))
        {
          String strsl = split(iniData, "ESPSLEEP=", "\r\n");
          strsl.trim();
          if (strsl.equals("true"))
          {
            espSleep = true;
          }
          else
          {
            espSleep = false;
          }
        }

        if (instr(iniData, "SLEEPTIME="))
        {
          String strslt = split(iniData, "SLEEPTIME=", "\r\n");
          strslt.trim();
          TIME2SLEEP = strslt.toInt();
        }
      }
    }
    else
    {
      writeConfig();
    }
#endif
    efhEnabled = false;
    dev.vendorID(MDESC);
    dev.productID(PDESC);
    dev.productRevision(firmwareVer);
    dev.onRead(onRead);
    if (SD_MMC.exists("/efh.tmp"))
    {
      SD_MMC.remove("/efh.tmp");
      efhEnabled = true;
      dev.mediaPresent(true);
      dev.begin(8192, 512);
      USB.begin();
      enTime = millis();
      hasEnabled = true;
    }
    else
    {
      dev.onWrite(onWrite);
      dev.mediaPresent(true);
      dev.begin(card->csd.capacity, card->csd.sector_size);
      USB.productName(PDESC);
      USB.manufacturerName(MDESC);
      USB.begin();
    }
  }
  else
  {
    //USBSerial.println("Filesystem failed to mount");
  }

#if USETFT  
  delay(3000);
  tft.fillScreen(TFT_BLACK);
  tft.setTextFont(1); //16 col 5 row
  tft.setTextColor(TFT_SKYBLUE, TFT_BLACK);   
  tft.setCursor(0, 0, 2);
#endif

startAccessPoint();
connectToWIFI();

  server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(200, "text/plain", "Microsoft Connect Test"); });

#if USECONFIG
  server.on("/config.ini", HTTP_ANY, [](AsyncWebServerRequest *request)
            { request->send(404); });
#endif

#if USECONFIG
  server.on("/config.html", HTTP_GET, [](AsyncWebServerRequest *request)
            { handleConfigHtml(request); });

  server.on("/config.html", HTTP_POST, [](AsyncWebServerRequest *request)
            { handleConfig(request); });
#endif

  server.on("/admin.html", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", admin_gz, sizeof(admin_gz));
    response->addHeader("Content-Encoding", "gzip");
    request->send(response); });

  server.on("/reboot.html", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", reboot_gz, sizeof(reboot_gz));
    response->addHeader("Content-Encoding", "gzip");
    request->send(response); });

  server.on("/reboot.html", HTTP_POST, [](AsyncWebServerRequest *request)
            { handleReboot(request); });

  server.on("/update.html", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", update_gz, sizeof(update_gz));
    response->addHeader("Content-Encoding", "gzip");
    request->send(response); });

  server.on(
      "/update.html", HTTP_POST, [](AsyncWebServerRequest *request) {},
      handleFwUpdate);

  server.on("/info.html", HTTP_GET, [](AsyncWebServerRequest *request)
            { handleInfo(request); });

  server.on("/usbon", HTTP_POST, [](AsyncWebServerRequest *request)
            {
    enableUSB();
    request->send(200, "text/plain", "ok"); });

  server.on("/usboff", HTTP_POST, [](AsyncWebServerRequest *request)
            {
    disableUSB();
    request->send(200, "text/plain", "ok"); });

#if FANMOD
  server.on("/setftemp", HTTP_POST, [](AsyncWebServerRequest *request)
            {
    if (request->hasParam("temp", true)) {
      ftemp = request->getParam("temp", true)->value().toInt();
      request->send(200, "text/plain", "ok");
    } else {
      request->send(404);
    } });

  server.on("/fant.bin", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    if (ftemp < 55 || ftemp > 85) { ftemp = 70; }
    uint8_t *fant = (uint8_t *)malloc(sizeof(uint8_t) * sizeof(fan));
    memcpy_P(fant, fan, sizeof(fan));
    fant[250] = ftemp;
    fant[368] = ftemp;
    AsyncWebServerResponse *response = request->beginResponse_P(200, "application/octet-stream", fant, sizeof(fan));
    request->send(response);
    free(fant); });
#endif

  server.serveStatic("/", SD_MMC, "/").setDefaultFile("index.html");

  server.onNotFound([](AsyncWebServerRequest *request)
                    {
    //USBSerial.println(request->url());
    String path = request->url();
    if (instr(path, "/update/ps4/")) {
      String Region = split(path, "/update/ps4/list/", "/");
      handleConsoleUpdate(Region, request);
      return;
    }
    if (instr(path, "/document/") && instr(path, "/ps4/")) {
      request->redirect("http://" + WIFI_HOSTNAME + "/index.html");
      return;
    }
    if (path.endsWith("index.html") || path.endsWith("index.htm") || path.endsWith("/")) {
      AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", index_gz, sizeof(index_gz));
      response->addHeader("Content-Encoding", "gzip");
      request->send(response);
      return;
    }
    if (path.endsWith("style.css")) {
      AsyncWebServerResponse *response = request->beginResponse_P(200, "text/css", style_gz, sizeof(style_gz));
      response->addHeader("Content-Encoding", "gzip");
      request->send(response);
      return;
    }
#if PSFREE
    if (path.endsWith("exploit.js")) {
      AsyncWebServerResponse *response = request->beginResponse_P(200, "text/javascript", psf_gz, sizeof(psf_gz));
      response->addHeader("Content-Encoding", "gzip");
      request->send(response);
      return;
    }
#endif

    if (path.endsWith("payloads.html")) {
      handlePayloads(request);
      return;
    }
    
    if (path.endsWith("loader.html")) {
      AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", loader_gz, sizeof(loader_gz));
      response->addHeader("Content-Encoding", "gzip");
      request->send(response);
      return;
    }

    request->send(404); });

  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  server.begin();
  //USBSerial.println("HTTP server started");

  if (TIME2SLEEP < 5)
  {
    TIME2SLEEP = 5;
  } // min sleep time
  bootTime = millis();
}

void setup_SD(void)
{
  esp_err_t ret;
  const char mount_point[] = MOUNT_POINT;
  esp_vfs_fat_sdmmc_mount_config_t mount_config = {.format_if_mount_failed = false, .max_files = 5, .allocation_unit_size = 16 * 1024};
  sdmmc_host_t host = {
      .flags = SDMMC_HOST_FLAG_4BIT | SDMMC_HOST_FLAG_DDR,
      .slot = SDMMC_HOST_SLOT_1,
      .max_freq_khz = SDMMC_FREQ_DEFAULT,
      .io_voltage = 3.3f,
      .init = &sdmmc_host_init,
      .set_bus_width = &sdmmc_host_set_bus_width,
      .get_bus_width = &sdmmc_host_get_slot_width,
      .set_bus_ddr_mode = &sdmmc_host_set_bus_ddr_mode,
      .set_card_clk = &sdmmc_host_set_card_clk,
      .do_transaction = &sdmmc_host_do_transaction,
      .deinit = &sdmmc_host_deinit,
      .io_int_enable = sdmmc_host_io_int_enable,
      .io_int_wait = sdmmc_host_io_int_wait,
      .command_timeout_ms = 0,
  };
  sdmmc_slot_config_t slot_config = {
      .clk = (gpio_num_t)12,
      .cmd = (gpio_num_t)16,
      .d0 = (gpio_num_t)14,
      .d1 = (gpio_num_t)17,
      .d2 = (gpio_num_t)21,
      .d3 = (gpio_num_t)18,
      .cd = SDMMC_SLOT_NO_CD,
      .wp = SDMMC_SLOT_NO_WP,
      .width = 4,
      .flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP,
  };
  gpio_set_pull_mode((gpio_num_t)16, GPIO_PULLUP_ONLY);
  gpio_set_pull_mode((gpio_num_t)14, GPIO_PULLUP_ONLY);
  gpio_set_pull_mode((gpio_num_t)17, GPIO_PULLUP_ONLY);
  gpio_set_pull_mode((gpio_num_t)21, GPIO_PULLUP_ONLY);
  gpio_set_pull_mode((gpio_num_t)18, GPIO_PULLUP_ONLY);
  ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &card);
}

static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
  uint32_t count = (bufsize / card->csd.sector_size);
  sdmmc_write_sectors(card, buffer + offset, lba, count);
  return bufsize;
}

static int32_t onRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
  if (efhEnabled)
  {
    if (lba > 4){lba = 4;}
    memcpy(buffer, exfathax[lba] + offset, bufsize);
  }
  else
  {
    uint32_t count = (bufsize / card->csd.sector_size);
    sdmmc_read_sectors(card, buffer + offset, lba, count);
  }
  return bufsize;
}

void enableUSB()
{
  File tmp = SD_MMC.open("/efh.tmp", "w");
  tmp.close();
  dev.end();
  ESP.restart();
}

void disableUSB()
{
  enTime = 0;
  hasEnabled = false;
  dev.end();
  ESP.restart();
}

void loop()
{
  if (espSleep)
  {
    if (millis() >= (bootTime + (TIME2SLEEP * 60000)))
    {
      //USBSerial.print("Esp sleep"); 
      digitalWrite(38, HIGH); 
      esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
      esp_deep_sleep_start();
      return;
    }
  }
  if (hasEnabled && millis() >= (enTime + 15000))
  {
    disableUSB();
  }
#if USETFT    
  if (millis() >= (tftCnt + 60000) && tftOn){ 
    tftCnt = 0;
    tftOn = false;
    digitalWrite(38, HIGH);
    return;
  }
  if (digitalRead(0) == LOW){
    if (tftCnt == 0){
       tftCnt = millis();
       digitalWrite(38, LOW);
       tftOn = true;
    }
  }
#endif
  dnsServer.processNextRequest();
}

#else
#error "Selected board not supported"
#endif
