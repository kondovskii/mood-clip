/*
 * mood-clip
 *
 * A wearable hair clip that displays a mood. Change it with the onboard
 * button, or from a phone over the clip's own WiFi access point.
 *
 * Hardware: LILYGO T-Display-S3 (ESP32-S3, 1.9" 170x320 ST7789) + LiPo.
 *
 *   BOOT button (GPIO0)  - short press: next mood
 *   USER button (GPIO14) - hold 2s: toggle the phone control AP
 *
 * No secrets file, no network credentials. Safe to hand to someone.
 */

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <TFT_eSPI.h>

// ---------------------------------------------------------------- config

#define AP_SSID      "mood-clip"
#define AP_PASSWORD  ""            // "" = open network. 8+ chars if you set one.

static const unsigned long AP_TIMEOUT_MS = 3UL * 60 * 1000;  // auto-off

// ---------------------------------------------------------------- hardware

#define PIN_POWER_ON 15   // MUST be HIGH or the screen is black on battery
#define PIN_LCD_BL   38
#define BTN_BOOT      0   // active LOW
#define BTN_USER     14   // active LOW

TFT_eSPI tft;
TFT_eSprite spr = TFT_eSprite(&tft);

static const int SCREEN_W = 320;
static const int SCREEN_H = 170;

WebServer server(80);
Preferences prefs;

// ---------------------------------------------------------------- moods

struct Mood {
  const char *name;      // shown on the phone button
  const char *face;      // big, centred
  const char *message;   // scrolls underneath
  uint16_t    bg;
  uint16_t    fg;
};

// 565 colours
#define C_GREEN   0x2FEB
#define C_RED     0xF9A6
#define C_PINK    0xFC3B
#define C_PURPLE  0xA97F
#define C_BLUE    0x5D7F
#define C_AMBER   0xFD20
#define C_INK     0x0841

const Mood MOODS[] = {
  { "good",    ":)",   "all good over here",        C_GREEN,  C_INK },
  { "hangry",  ">:(",  "feed me immediately",       C_RED,    C_INK },
  { "tired",   "-_-",  "running on 3% battery",     C_PURPLE, C_INK },
  { "busy",    ":|",   "do not perceive me",        C_AMBER,  C_INK },
  { "happy",   "^-^",  "having a lovely time",      C_PINK,   C_INK },
  { "chill",   ":O",   "no thoughts, head empty",   C_BLUE,   C_INK },
};
static const int MOOD_COUNT = sizeof(MOODS) / sizeof(MOODS[0]);

int  moodIndex   = 0;
char customMsg[64] = "";      // set from the phone; overrides the message

// ---------------------------------------------------------------- state

bool          apActive    = false;
unsigned long apStarted   = 0;
unsigned long lastRequest = 0;

int           scrollX     = SCREEN_W;
unsigned long lastStep    = 0;
static const int SCROLL_DELAY_MS = 16;
static const int GAP_PX          = 60;

// ---------------------------------------------------------------- prototypes

void drawFrame();
void applyMood(int idx);
void startAP();
void stopAP();
void saveState();

// ---------------------------------------------------------------- persistence

void saveState() {
  prefs.begin("moodclip", false);
  prefs.putInt("mood", moodIndex);
  prefs.putString("custom", customMsg);
  prefs.end();
}

void loadState() {
  prefs.begin("moodclip", true);
  moodIndex = prefs.getInt("mood", 0);
  String c  = prefs.getString("custom", "");
  prefs.end();

  if (moodIndex < 0 || moodIndex >= MOOD_COUNT) moodIndex = 0;
  strncpy(customMsg, c.c_str(), sizeof(customMsg) - 1);
  customMsg[sizeof(customMsg) - 1] = '\0';
}

// ---------------------------------------------------------------- display

const char *currentMessage() {
  return customMsg[0] ? customMsg : MOODS[moodIndex].message;
}

void applyMood(int idx) {
  if (idx < 0 || idx >= MOOD_COUNT) return;
  moodIndex  = idx;
  customMsg[0] = '\0';        // picking a preset clears any custom text
  scrollX    = SCREEN_W;      // restart the marquee
  saveState();
  Serial.printf("mood: %s\n", MOODS[moodIndex].name);
}

void drawFrame() {
  const Mood &m = MOODS[moodIndex];

  spr.fillSprite(m.bg);
  spr.setTextColor(m.fg, m.bg);

  // Big face, centred in the upper area
  spr.setTextFont(4);
  spr.setTextSize(2);
  spr.setTextDatum(MC_DATUM);
  spr.drawString(m.face, SCREEN_W / 2, 58);

  // Scrolling message across the lower band
  spr.setTextSize(1);
  spr.setTextDatum(TL_DATUM);
  const char *msg = currentMessage();
  int w = spr.textWidth(msg);

  spr.drawString(msg, scrollX, 124);
  spr.drawString(msg, scrollX + w + GAP_PX, 124);

  // While the AP is up, show how to connect
  if (apActive) {
    spr.setTextFont(2);
    spr.drawString("wifi: " AP_SSID "  ->  192.168.4.1", 6, 4);
  }

  spr.pushSprite(0, 0);

  if (--scrollX < -(w + GAP_PX)) scrollX = 0;
}

// ---------------------------------------------------------------- web page

String buildPage() {
  String h = F(
    "<!doctype html><html><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>mood clip</title><style>"
    "body{font-family:system-ui,sans-serif;background:#111;color:#eee;"
    "margin:0;padding:24px;text-align:center}"
    "h1{font-size:20px;font-weight:600;margin:0 0 20px}"
    ".grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}"
    "button{font-size:17px;padding:20px 8px;border:0;border-radius:14px;"
    "background:#2a2a2a;color:#eee;cursor:pointer}"
    "button:active{background:#3d3d3d}"
    "input{width:100%;box-sizing:border-box;font-size:16px;padding:14px;"
    "margin-top:22px;border-radius:12px;border:1px solid #333;"
    "background:#1c1c1c;color:#eee}"
    "#s{margin-top:18px;font-size:14px;color:#888;min-height:20px}"
    "</style></head><body><h1>pick a mood</h1><div class=grid>");

  for (int i = 0; i < MOOD_COUNT; i++) {
    h += "<button onclick=\"set(" + String(i) + ")\">";
    h += MOODS[i].face;
    h += "<br>";
    h += MOODS[i].name;
    h += "</button>";
  }

  h += F(
    "</div>"
    "<input id=t maxlength=60 placeholder='or type your own...'>"
    "<div id=s></div>"
    "<script>"
    "function say(m){document.getElementById('s').textContent=m}"
    "function set(i){fetch('/set?m='+i).then(()=>say('done'))}"
    "document.getElementById('t').addEventListener('keydown',e=>{"
    "if(e.key==='Enter'){"
    "fetch('/custom?t='+encodeURIComponent(e.target.value))"
    ".then(()=>say('sent'));}});"
    "</script></body></html>");

  return h;
}

void handleRoot() {
  lastRequest = millis();
  server.send(200, "text/html", buildPage());
}

void handleSet() {
  lastRequest = millis();
  if (server.hasArg("m")) applyMood(server.arg("m").toInt());
  server.send(200, "text/plain", "ok");
}

void handleCustom() {
  lastRequest = millis();
  if (server.hasArg("t")) {
    String t = server.arg("t");
    t.trim();
    strncpy(customMsg, t.c_str(), sizeof(customMsg) - 1);
    customMsg[sizeof(customMsg) - 1] = '\0';
    scrollX = SCREEN_W;
    saveState();
    Serial.printf("custom: %s\n", customMsg);
  }
  server.send(200, "text/plain", "ok");
}

// ---------------------------------------------------------------- ap

void startAP() {
  WiFi.mode(WIFI_AP);
  if (strlen(AP_PASSWORD) >= 8) {
    WiFi.softAP(AP_SSID, AP_PASSWORD);
  } else {
    WiFi.softAP(AP_SSID);          // open network
  }

  server.on("/", handleRoot);
  server.on("/set", handleSet);
  server.on("/custom", handleCustom);
  server.onNotFound(handleRoot);   // any URL lands on the page
  server.begin();

  apActive    = true;
  apStarted   = millis();
  lastRequest = millis();

  Serial.print("ap: up at ");
  Serial.println(WiFi.softAPIP());
}

void stopAP() {
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  apActive = false;
  Serial.println("ap: down");
}

// ---------------------------------------------------------------- buttons

struct Button {
  uint8_t       pin;
  bool          down      = false;
  unsigned long downAt    = 0;
  bool          longFired = false;
};

Button bBoot{BTN_BOOT};
Button bUser{BTN_USER};

enum BtnEvent { BTN_NONE, BTN_SHORT, BTN_LONG };

// Poll once per loop. Returns an event at most once per press.
// A long press fires the moment the threshold is crossed; the following
// release is then swallowed so you never get both events from one press.
BtnEvent poll(Button &b, unsigned long longMs) {
  bool nowDown = (digitalRead(b.pin) == LOW);
  BtnEvent ev  = BTN_NONE;

  if (nowDown && !b.down) {               // pressed
    b.downAt    = millis();
    b.longFired = false;
  } else if (nowDown && b.down) {         // held
    if (longMs && !b.longFired && millis() - b.downAt >= longMs) {
      b.longFired = true;
      ev = BTN_LONG;
    }
  } else if (!nowDown && b.down) {        // released
    unsigned long held = millis() - b.downAt;
    if (!b.longFired && held > 40) ev = BTN_SHORT;   // 40ms debounce
  }

  b.down = nowDown;
  return ev;
}

// ---------------------------------------------------------------- setup

void setup() {
  pinMode(PIN_POWER_ON, OUTPUT);
  digitalWrite(PIN_POWER_ON, HIGH);

  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, HIGH);

  pinMode(BTN_BOOT, INPUT_PULLUP);
  pinMode(BTN_USER, INPUT_PULLUP);

  Serial.begin(115200);
  delay(300);
  Serial.println("\nmood-clip starting");

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  spr.createSprite(SCREEN_W, SCREEN_H);

  loadState();
  Serial.printf("mood: %s (restored)\n", MOODS[moodIndex].name);

  WiFi.mode(WIFI_OFF);   // radio off until asked for — saves a lot of battery
}

// ---------------------------------------------------------------- loop

void loop() {
  unsigned long now = millis();

  // BOOT: short press cycles to the next mood
  if (poll(bBoot, 0) == BTN_SHORT) {
    applyMood((moodIndex + 1) % MOOD_COUNT);
  }

  // USER: hold 2s to toggle phone control
  if (poll(bUser, 2000) == BTN_LONG) {
    if (apActive) stopAP(); else startAP();
  }

  if (apActive) {
    server.handleClient();
    if (now - lastRequest > AP_TIMEOUT_MS) {
      Serial.println("ap: idle timeout");
      stopAP();
    }
  }

  if (now - lastStep >= SCROLL_DELAY_MS) {
    lastStep = now;
    drawFrame();
  }
}
