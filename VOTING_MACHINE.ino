#include <Adafruit_Fingerprint.h>
#include <LiquidCrystal_I2C.h>

// Pin Definitions
#define BTN_MODE     PB11 // Tactile Toggle Button (Mode Switch)
#define BTN_CAND1    PB12 // Candidate 1 Push Button
#define BTN_CAND2    PB13 // Candidate 2 Push Button
#define BTN_CAND3    PB14 // Candidate 3 Push Button
#define BTN_RESULT   PB15 // Result / Next Voter / Clear Hold Button
#define BUZZER_PIN   PA8  // Active Buzzer Positive Pin

// Hardware Serial 2 for STM32 Blue Pill (PA2 = TX2, PA3 = RX2)
Uart Serial2(PA3, PA2); 

Adafruit_Fingerprint finger = Adafruit_Fingerprint(&Serial2);
LiquidCrystal_I2C lcd(0x27, 16, 2);

// System State Variables
bool isEnrollMode = false;
int currentEnrollID = 1;

// State tracking for non-blocking button presses
bool lastModeBtnState = HIGH;
bool lastResultBtnState = HIGH;
unsigned long resultPressTime = 0;
bool resultHeld = false;

// Temporary RAM Storage for Current Session
int votes[3] = {0, 0, 0};
bool hasVoted[128] = {false}; // Tracks IDs 1 to 127

void setup() {
  pinMode(BTN_MODE, INPUT_PULLUP);
  pinMode(BTN_CAND1, INPUT_PULLUP);
  pinMode(BTN_CAND2, INPUT_PULLUP);
  pinMode(BTN_CAND3, INPUT_PULLUP);
  pinMode(BTN_RESULT, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);

  lcd.init();
  lcd.backlight();
  lcd.print("System Booting..");

  finger.begin(57600);
  if (!finger.verifyPassword()) {
    lcd.setCursor(0, 1);
    lcd.print("Sensor Error!");
    while (1) { delay(1); }
  }

  // Sync the ID count with the sensor's memory on startup
  finger.getTemplateCount();
  currentEnrollID = finger.templateCount + 1;

  beep(1, 100);
  updateModeScreen();
}

void loop() {
  // 1. Non-blocking Edge Detection for Mode Button
  bool currentModeBtn = digitalRead(BTN_MODE);
  if (currentModeBtn == LOW && lastModeBtnState == HIGH) {
    isEnrollMode = !isEnrollMode; 
    
    // Always sync ID with memory when entering enroll mode
    if (isEnrollMode) {
      finger.getTemplateCount();
      currentEnrollID = finger.templateCount + 1;
    }
    
    beep(1, 80);
    updateModeScreen();
  }
  lastModeBtnState = currentModeBtn;

  // 2. Run active mode logic
  if (isEnrollMode) {
    handleEnrollMode();
  } else {
    handleVotingMode();
  }
}

// ==========================================
// VOTING MODE
// ==========================================
void handleVotingMode() {
  bool currentResBtn = digitalRead(BTN_RESULT);
  if (currentResBtn == LOW && lastResultBtnState == HIGH) {
    displayResults();
    updateModeScreen();
  }
  lastResultBtnState = currentResBtn;

  int fingerStatus = getFingerprintID();
  
  if (fingerStatus > 0) {
    if (hasVoted[fingerStatus]) {
      lcd.clear();
      lcd.print("ID "); lcd.print(fingerStatus);
      lcd.setCursor(0, 1);
      lcd.print("Already Voted!");
      beep(3, 80);
      waitUntilFingerRemoved(); 
      updateModeScreen();
    } else {
      castVote(fingerStatus);
    }
  } else if (fingerStatus == -2) {
    lcd.clear();
    lcd.print("Unknown Finger!");
    lcd.setCursor(0, 1);
    lcd.print("Access Denied");
    beep(3, 150); 
    waitUntilFingerRemoved(); 
    updateModeScreen();
  }
}

void castVote(int fID) {
  // Immediate audio-visual feedback
  lcd.clear();
  lcd.print("Access Granted!");
  lcd.setCursor(0, 1);
  lcd.print("Voter ID: "); lcd.print(fID);
  beep(1, 150);
  delay(1000); 

  // Switch to voting prompt
  lcd.clear();
  lcd.print("Please Vote Now");
  lcd.setCursor(0, 1);
  lcd.print("Press 1, 2 or 3");

  bool lastC1 = HIGH, lastC2 = HIGH, lastC3 = HIGH;

  while (true) {
    if (digitalRead(BTN_MODE) == LOW) return; // Allow aborting

    bool currC1 = digitalRead(BTN_CAND1);
    bool currC2 = digitalRead(BTN_CAND2);
    bool currC3 = digitalRead(BTN_CAND3);

    if (currC1 == LOW && lastC1 == HIGH) { saveVote(0, fID); break; }
    if (currC2 == LOW && lastC2 == HIGH) { saveVote(1, fID); break; }
    if (currC3 == LOW && lastC3 == HIGH) { saveVote(2, fID); break; }

    lastC1 = currC1;
    lastC2 = currC2;
    lastC3 = currC3;
  }
}

void saveVote(int candIndex, int fID) {
  votes[candIndex]++;
  hasVoted[fID] = true;

  lcd.clear();
  lcd.print("Vote Recorded!");
  lcd.setCursor(0, 1);
  lcd.print("Cand "); lcd.print(candIndex + 1);

  beep(2, 100);
  delay(1200); 

  waitUntilFingerRemoved(); 
  updateModeScreen();
}

void displayResults() {
  lcd.clear();
  lcd.print("1:"); lcd.print(votes[0]);
  lcd.print(" 2:"); lcd.print(votes[1]);
  lcd.print(" 3:"); lcd.print(votes[2]);

  lcd.setCursor(0, 1);
  if (votes[0] > votes[1] && votes[0] > votes[2]) lcd.print("Winner: Cand 1");
  else if (votes[1] > votes[0] && votes[1] > votes[2]) lcd.print("Winner: Cand 2");
  else if (votes[2] > votes[0] && votes[2] > votes[1]) lcd.print("Winner: Cand 3");
  else lcd.print("Result: Tie!");

  beep(1, 400);
  delay(5000); 
}

// ==========================================
// ENROLLMENT MODE
// ==========================================
void handleEnrollMode() {
  bool currentResBtn = digitalRead(BTN_RESULT);

  if (currentResBtn == LOW && lastResultBtnState == HIGH) {
    resultPressTime = millis();
    resultHeld = false;
  } else if (currentResBtn == LOW && lastResultBtnState == LOW) {
    if (!resultHeld && (millis() - resultPressTime > 5000)) {
      clearAllData();
      resultHeld = true;
    }
  } else if (currentResBtn == HIGH && lastResultBtnState == LOW) {
    if (!resultHeld) {
      currentEnrollID++;
      beep(1, 80);
      updateModeScreen();
    }
  }
  lastResultBtnState = currentResBtn;

  if (enrollFinger(currentEnrollID)) {
    finger.getTemplateCount();
    currentEnrollID = finger.templateCount + 1;
    updateModeScreen();
  }
}

bool enrollFinger(int id) {
  int p = finger.getImage();
  if (p == FINGERPRINT_NOFINGER) return false; 
  if (p != FINGERPRINT_OK) return false;       

  // Step 1: Valid image detected, convert it
  p = finger.image2Tz(1);
  if (p != FINGERPRINT_OK) {
    lcd.clear(); lcd.print("Bad Image"); beep(2, 100); 
    waitUntilFingerRemoved(); 
    updateModeScreen(); return false; 
  }

  // FIX: DUPLICATE CHECK
  // Immediately search the database to see if this print already exists
  p = finger.fingerSearch();
  if (p == FINGERPRINT_OK) {
    // Fingerprint found in the system!
    lcd.clear();
    lcd.print("Already Enrolled");
    lcd.setCursor(0, 1);
    lcd.print("Matches ID: "); lcd.print(finger.fingerID);
    beep(4, 100); // 4 fast, distinct error beeps
    waitUntilFingerRemoved();
    updateModeScreen();
    return false; // Return false so currentEnrollID does not increment!
  }

  // If no duplicate is found, proceed normally...
  lcd.clear();
  lcd.print("Remove Finger");
  beep(1, 50);
  waitUntilFingerRemoved(); 

  lcd.clear();
  lcd.print("Place Again...");
  
  p = -1;
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    if (digitalRead(BTN_MODE) == LOW) { delay(200); return false; } 
  }

  delay(250);
  p = finger.getImage(); 

  p = finger.image2Tz(2);
  if (p != FINGERPRINT_OK) {
    lcd.clear(); lcd.print("Bad Image"); beep(2, 100); 
    waitUntilFingerRemoved(); 
    updateModeScreen(); return false;
  }

  p = finger.createModel();
  if (p != FINGERPRINT_OK) {
    lcd.clear(); lcd.print("Not Matched!"); beep(3, 100); 
    waitUntilFingerRemoved(); 
    updateModeScreen(); return false;
  }

  if (finger.storeModel(id) == FINGERPRINT_OK) {
    lcd.clear();
    lcd.print("Saved: Voter "); lcd.print(id);
    beep(2, 100);
    waitUntilFingerRemoved(); 
    return true;
  }

  lcd.clear();
  lcd.print("Enroll Failed!");
  beep(3, 80);
  waitUntilFingerRemoved();
  updateModeScreen();
  return false;
}

// ==========================================
// UTILITY FUNCTIONS
// ==========================================
void waitUntilFingerRemoved() {
  delay(500); 
  while (finger.getImage() != FINGERPRINT_NOFINGER) {
    if (digitalRead(BTN_MODE) == LOW) return; 
    delay(50);
  }
}

void clearAllData() {
  lcd.clear();
  lcd.print("Clearing DB...");
  finger.emptyDatabase(); 

  votes[0] = 0; votes[1] = 0; votes[2] = 0;
  for (int i = 0; i < 128; i++) hasVoted[i] = false;

  currentEnrollID = 1;
  lcd.setCursor(0, 1);
  lcd.print("DB Cleared!");
  beep(3, 150);
  waitUntilFingerRemoved(); 
  updateModeScreen();
}

void updateModeScreen() {
  lcd.clear();
  if (isEnrollMode) {
    lcd.print("ENROLL MODE");
    lcd.setCursor(0, 1);
    lcd.print("Ready ID: "); lcd.print(currentEnrollID);
  } else {
    lcd.print("VOTING MODE");
    lcd.setCursor(0, 1);
    lcd.print("Place Finger...");
  }
}

int getFingerprintID() {
  uint8_t p = finger.getImage();
  if (p == FINGERPRINT_NOFINGER) return 0;
  if (p != FINGERPRINT_OK) return -1;

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) return -1;

  p = finger.fingerSearch();
  if (p == FINGERPRINT_OK) {
    return finger.fingerID; 
  } else if (p == FINGERPRINT_NOTFOUND) {
    return -2;              
  }
  
  return -1; 
}

void beep(int times, int duration) {
  for (int i = 0; i < times; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(duration);
    digitalWrite(BUZZER_PIN, LOW);
    if (times > 1) delay(duration);
  }
}