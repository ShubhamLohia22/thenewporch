// =======================================================================
// DEVICE: Seeed Studio XIAO ESP32-C3
// OTA PASSWORD: admin123
// =======================================================================
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <time.h>
#include <DNSServer.h>

DNSServer dnsServer;
const byte DNS_PORT = 53;
bool inSetupMode = false;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
Preferences prefs;

const char *ssid = "SHAKUNI";
const char *password = "Oct221995";

// -------- DYNAMIC NETWORK & PC CONFIG --------
String net_ssid = "SHAKUNI";
String net_pass = "Oct221995";
String ip_esp = "192.168.1.17";
String ip_gateway = "192.168.1.1";

String ip_desk_lan = "192.168.1.9";
String ip_desk_wifi = "192.168.1.20";
String ip_lap_lan = "192.168.1.19";
String ip_lap_wifi = "192.168.1.18";

String ui_user = "admin";
String ui_pass = "admin";

IPAddress targetIPs[4]; // Populated dynamically in setup()
const int numIPs = 4;
IPAddress local_IP;
IPAddress gateway;
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);
IPAddress secondaryDNS(8, 8, 4, 4);

// -------- MASTER CONTROL SYSTEM --------
volatile bool controlsLocked = false;
unsigned long ambientStartTime = 0;
unsigned long lastActivityTime = 0;
unsigned long globalAwakeStartTime = 0; // Fixes the compilation error!
String tempStatus = "";                 // <--- STORES 2-SECOND UI FLASHES
unsigned long tempStatusTime = 0;
volatile bool cancelEffect = false;
bool globalSoundEnabled = true;
int maxHeadBright = 255;
int currentDay = -1;
unsigned long lastTimeCheck = 0;

enum Action {
  ACTION_NONE,
  ACTION_IGNITION,
  ACTION_USB_EFFECT,
  ACTION_SHUTDOWN,
  ACTION_SOFT_SHUTDOWN,
  ACTION_ALL_OFF,
  ACTION_RESTART,
  ACTION_AUTO_WAKE,
  ACTION_AUTO_SHUTDOWN,
  ACTION_LOCK_TOGGLE,
  ACTION_SILENT_USB_SYNC
};
volatile Action pendingAction = ACTION_NONE;

// -------- REMINDER & SLEEP CONFIG --------
int lastRemindedHour = -1;
bool reminderEnabled = true;

volatile bool sleepTimerActive = false;
volatile bool sleepTimerPaused = false;
volatile unsigned long sleepStartTime = 0;
volatile unsigned long sleepDurationMs = 0;

// -------- USB KVM CONFIG --------
bool isUsbState2 = false;
volatile bool kvmAutoReset = true; // NEW: Tracks if the KVM loses power

// -------- PC AUTO-SYNC CONFIG (RTOS PROTECTED) --------
volatile bool lastPcOnline = false;
volatile unsigned long lastOfflineTime = 0;
volatile bool needsUsbSwitchPulse = false;
volatile int activeDeviceIndex = -1;
TaskHandle_t PingTaskHandle;
volatile bool isDesktopOnline = false;
volatile bool isLaptopOnline = false;
volatile bool ignoreNetwork = false;

// -------- FUNCTION DECLARATIONS --------
void notifyClients(); // <--- ADDED THIS TO FIX COMPILER ERROR
void setNewMasterAction(Action newAction);
void triggerPC();
void ignitionEffect();
void usbTriggerEffect();
void runJukeboxMode();
void runCarMode();
void runMarioMode();
void executeTheme(int currentTheme);
void allOff();
void handleBonnet();
void handleLeftDoor();
void handleRightDoor();
void runAmbient();
void runIdleAnimation();
void reminderEffect();
void shutdownEffect();
void restartEffect();
void forcePCOff();
void triggerUSB();
void runHighwayRunMode();
bool checkInterrupt();
bool smartDelay(unsigned long ms);
void uiTick();
bool checkPCOnline();
void checkDailyReset();
void checkHourlyReminder();
void runOverloadAnimation();
void restoreLights();
void playLockAnimation();
void playUnlockAnimation();
void playMusicChunk(const int *notes, const int *durations, int length, int tempo, int lightStyle);
void executeShutdown();
void executeSoftShutdown();
void startCaptivePortal();

// ---------------- XIAO ESP32-C3 PIN CONFIG ----------------
#define BONNET_PIN 2      // D0
#define PC_PIN 3          // D1
#define USB_PIN 4         // D2
#define L_HEAD_PIN 5      // D3
#define R_HEAD_PIN 6      // D4
#define L_BRAKE_PIN 7     // D5
#define UNDERBODY_PIN 21  // D6
#define BUZZER_PIN 20     // D7
#define RIGHT_PIN 8       // D8
#define LEFT_PIN 9        // D9
#define R_BRAKE_PIN 10    // D10

// ---------------- HARDWARE STATES ----------------
unsigned long leftDoorPressStart = 0;
bool longPressTriggered = false;

enum Mode { MODE_IDLE,
            MODE_AMBIENT,
            MODE_ON };
Mode currentMode = MODE_IDLE;

String carStatus = "OFF";
bool headlightState = false;
bool brakeState = false;
bool underbodyState = false;
bool ambientState = false;
bool highwayRunState = false;
bool forceHighwayStart = false;
bool jukeboxModeState = false;
bool carModeState = false;
bool marioModeState = false;
bool pcOverloadState = false;

// ---------------- WEB UI HTML (PREMIUM VERTICAL DASHBOARD) ----------------
const char html[] PROGMEM = R"rawliteral(
  <!DOCTYPE html>
  <html lang="en">
  <head>
    <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
    <meta charset="UTF-8">
    <title>Porsche Control</title>
    <link rel="icon" href="data:image/svg+xml,<svg xmlns=%22http://www.w3.org/2000/svg%22 viewBox=%220 0 100 100%22><text y=%22.9em%22 font-size=%2290%22>🏎️</text></svg>">
    <style>
      :root {
        --bg-color: #0b0c10;
        --card-bg: rgba(22, 23, 29, 0.65);
        --card-border: rgba(255, 255, 255, 0.05);
        --card-shadow: 0 8px 32px rgba(0, 0, 0, 0.4);
        --btn-bg: rgba(26, 27, 35, 0.6);
        --btn-hover: rgba(34, 35, 45, 0.8);
        --text-main: #e2e2e5;
        --text-muted: #7a7b86;
        --neon-green: #00ffaa;
        --neon-cyan: #00ddff;
        --neon-yellow: #ffcc00;
        --neon-red: #ff4444;
        --ring-bg: #252630;
      }
      
      * { box-sizing: border-box; -webkit-tap-highlight-color: transparent; }
      
      body { background: var(--bg-color); color: var(--text-main); font-family: 'Inter', -apple-system, sans-serif; margin: 0; padding: 20px 15px 40px 15px; display: flex; flex-direction: column; align-items: center; }
      
      /* DESKTOP MASONRY FIX */
      .dashboard { width: 100%; max-width: 420px; display: flex; flex-direction: column; gap: 20px; margin: 0 auto; }
      @media (min-width: 850px) {
        .dashboard { max-width: 820px; display: block; column-count: 2; column-gap: 20px; }
        .header { column-span: all; margin-bottom: 25px; }
        .card { break-inside: avoid; page-break-inside: avoid; display: inline-block; width: 100%; margin-bottom: 20px; }
      }

      .header { text-align: center; margin-bottom: 10px; }
      .header h1 { font-size: 24px; font-weight: 900; font-style: italic; letter-spacing: 1.5px; margin: 0 0 4px 0; color: #fff; }
      .header .subtitle { font-size: 10px; color: var(--text-muted); letter-spacing: 3px; }

      .card { background: var(--card-bg); border: 1px solid var(--card-border); border-radius: 24px; padding: 24px; box-shadow: var(--card-shadow); position: relative; overflow: hidden; backdrop-filter: blur(16px); -webkit-backdrop-filter: blur(16px); }
      
      .status-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 15px; }
      .status-title { font-size: 11px; font-weight: 700; color: var(--text-muted); letter-spacing: 2px; }
      .status-dot { width: 8px; height: 8px; border-radius: 50%; background: var(--neon-green); box-shadow: 0 0 10px var(--neon-green); }
      
      .main-status-text { font-size: 28px; font-weight: 900; color: var(--neon-green); letter-spacing: 1px; margin-bottom: 25px; text-align: center; transition: all 0.3s ease; }

      .info-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 15px; border-top: 1px solid rgba(255,255,255,0.05); padding-top: 20px; }
      .info-item { display: flex; flex-direction: column; gap: 4px; }
      .info-label { font-size: 10px; color: var(--text-muted); font-weight: 700; letter-spacing: 1px; }
      .info-val { font-size: 13px; font-weight: 800; transition: color 0.3s ease; }

      .btn-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }

      .btn { background: var(--btn-bg); color: var(--text-main); border: 1px solid var(--card-border); border-radius: 16px; padding: 18px 10px; font-size: 11px; font-weight: 800; letter-spacing: 0.5px; cursor: pointer; transition: all 0.3s ease; display: flex; align-items: center; justify-content: center; text-align: center; box-shadow: 0 4px 10px rgba(0,0,0,0.1); backdrop-filter: blur(8px); -webkit-backdrop-filter: blur(8px); }
      .btn:active { transform: scale(0.96); background: var(--btn-hover); }
      .btn-full { grid-column: span 2; }
      
      /* FLAT BUTTONS TO SUPPORT THEMES */
      .btn-master { background: rgba(0, 221, 255, 0.1); border: 1px solid rgba(0, 221, 255, 0.4); color: var(--neon-cyan); font-size: 13px; font-weight: 900; }
      .btn-master:active { background: rgba(0, 221, 255, 0.2); }
      .btn-usb { background: rgba(255, 204, 0, 0.1); border: 1px solid rgba(255, 204, 0, 0.4); color: var(--neon-yellow); font-size: 13px; font-weight: 900; }
      .btn-usb:active { background: rgba(255, 204, 0, 0.2); }

      .accent-green { border-bottom: 2px solid rgba(0, 255, 170, 0.5); }
      .accent-cyan { border-bottom: 2px solid rgba(0, 221, 255, 0.5); }
      .accent-yellow { border-bottom: 2px solid rgba(255, 204, 0, 0.5); }
      .accent-red { border-bottom: 2px solid rgba(255, 68, 68, 0.5); color: var(--neon-red); }

      .slider-container { display: flex; flex-direction: column; gap: 12px; grid-column: span 2; padding: 5px 0;}
      .slider-header { display: flex; justify-content: space-between; align-items: center; font-size: 11px; font-weight: 800; color: var(--text-muted); letter-spacing: 1px;}
      .slider-val-highlight { color: var(--neon-cyan); text-shadow: 0 0 10px rgba(0, 221, 255, 0.3); }
      
      .slider { -webkit-appearance: none; width: 100%; height: 6px; background: var(--ring-bg); border-radius: 10px; outline: none; }
      .slider::-webkit-slider-thumb { -webkit-appearance: none; width: 22px; height: 22px; border-radius: 50%; background: var(--slider-thumb); cursor: pointer; box-shadow: 0 0 10px var(--slider-glow); transition: background 0.3s; }

      .sleep-controls { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 8px; margin-top: 10px; }
      .btn-micro { padding: 12px 0; font-size: 10px; border-radius: 12px; background: rgba(255,255,255,0.02); }

      /* SMART RINGS CSS */
      .ring-wrapper { position: relative; width: 140px; height: 140px; margin: 0 auto 5px auto; }
      .smart-ring { transform: rotate(-90deg); width: 100%; height: 100%; filter: var(--ring-shadow); }
      .ring-track { fill: none; stroke: var(--ring-bg); stroke-width: 8; }
      .ring-progress { fill: none; stroke-width: 8; stroke-linecap: round; transition: stroke-dashoffset 1s linear, stroke-opacity 0.3s ease; }
      .ring-outer { stroke: var(--neon-red); stroke-dasharray: 251; stroke-dashoffset: 251; }
      .ring-middle { stroke: var(--neon-cyan); stroke-dasharray: 176; stroke-dashoffset: 176; }
      .ring-inner { stroke: var(--neon-yellow); stroke-dasharray: 100; stroke-dashoffset: 100; }
   
   /* MODAL STYLES */
      .modal { display: none; position: fixed; z-index: 1000; left: 0; top: 0; width: 100%; height: 100%; background-color: var(--bg-color); flex-direction: column; align-items: center; justify-content: center; padding: 15px; transition: background 0.3s ease; }
      .modal-content { background: var(--card-bg); border: 1px solid var(--card-border); border-radius: 20px; width: 100%; max-width: 450px; max-height: 80vh; overflow-y: auto; padding: 25px; position: relative; box-shadow: var(--card-shadow); backdrop-filter: blur(16px); -webkit-backdrop-filter: blur(16px); }
      .modal-header { font-size: 16px; font-weight: 900; color: var(--text-main); margin-bottom: 20px; text-align: center; letter-spacing: 2px; border-bottom: 1px solid var(--card-border); padding-bottom: 15px; }
      .close-btn { position: absolute; top: 20px; right: 20px; color: var(--neon-red); font-size: 28px; line-height: 20px; font-weight: bold; cursor: pointer; }
      .help-item { border-bottom: 1px solid var(--card-border); padding-bottom: 12px; margin-bottom: 12px; }
      .help-item:last-child { border-bottom: none; margin-bottom: 0; padding-bottom: 0; }
      .help-title { color: var(--text-main); font-size: 11px; font-weight: 900; letter-spacing: 1px; margin-bottom: 4px; }
      .help-desc { color: var(--text-muted); font-size: 11px; line-height: 1.5; font-weight: 500; }

    </style>
  </head>
  <body>
    
    <div class="dashboard">
      <div class="header">
        <h1 style="color: var(--text-main);">PORSCHE CORE</h1>
        <div class="subtitle">ACTIVE SYSTEM MANAGEMENT</div>
        <div style="display: flex; gap: 10px; justify-content: center; margin-top: 15px;">
          <button id="themeBtn" class="btn btn-micro" style="width: 130px; border-color: var(--neon-yellow); color: var(--neon-yellow); background: rgba(255, 204, 0, 0.05);" onclick="toggleTheme()">☀️ DAY MODE</button>
          <button class="btn btn-micro" style="width: 130px; border-color: var(--neon-cyan); color: var(--neon-cyan); background: rgba(0, 221, 255, 0.05);" onclick="openHelp()">📖 SYSTEM MANUAL</button>
        </div>
      </div>

      <!-- MAIN STATUS CARD -->
      <div class="card">
        <div class="status-header">
          <div class="status-title">TELEMETRY</div>
          <div class="status-dot" id="liveDot"></div>
        </div>

        <div style="text-align: center; margin-bottom: 20px;">
          <div class="ring-wrapper">
            <svg viewBox="0 0 100 100" class="smart-ring">
              <circle cx="50" cy="50" r="40" class="ring-track"></circle>
              <circle cx="50" cy="50" r="28" class="ring-track"></circle>
              <circle cx="50" cy="50" r="16" class="ring-track"></circle>
              <circle cx="50" cy="50" r="40" class="ring-progress ring-outer" id="ringSleep"></circle>
              <circle cx="50" cy="50" r="28" class="ring-progress ring-middle" id="ringMaster"></circle>
              <circle cx="50" cy="50" r="16" class="ring-progress ring-inner" id="ringChime"></circle>
            </svg>
          </div>
          <div id="statusText" class="main-status-text" style="margin-bottom: 0;">LOADING...</div>
        </div>
        
        <div class="info-grid">
          <div class="info-item">
            <span class="info-label">DESKTOP</span>
            <span id="pcDot" class="info-val" style="color:var(--text-muted);">SCANNING</span>
          </div>
          <div class="info-item">
            <span class="info-label">LAPTOP</span>
            <span id="lapDot" class="info-val" style="color:var(--text-muted);">SCANNING</span>
          </div>
          <div class="info-item">
            <span class="info-label">AUDIO CHIME</span>
            <span id="audioText" class="info-val" style="color:var(--neon-green);">ON</span>
          </div>
          <div class="info-item">
            <span class="info-label">REMINDER</span>
            <span id="timerText" class="info-val" style="color:var(--neon-yellow);">--:--</span>
          </div>
          <div class="info-item">
            <span class="info-label">SYSTEM LOCK</span>
            <span id="lockText" class="info-val" style="color:var(--neon-green);">UNLOCKED</span>
          </div>
          <div class="info-item">
            <span class="info-label">AUTO SLEEP</span>
            <span id="sleepText" class="info-val" style="color:var(--text-muted);">OFF</span>
          </div>
        </div>
      </div>

      <!-- PRIMARY CONTROLS CARD -->
      <div class="card btn-grid">
        <button class="btn btn-full btn-master" onclick="sendCmd('/masterToggle')">SYSTEM WAKE / SLEEP (MASTER SWITCH)</button>
        
        <div class="slider-container" style="margin-top: 10px; margin-bottom: 5px;">
          <div class="slider-header">
            <span>HEADLIGHT INTENSITY</span>
          </div>
          <input type="range" min="0" max="255" value="255" class="slider" id="brightSlider" oninput="sendSliderVal(this.value)">
        </div>
      </div>

      <div class="card btn-grid">
        <button class="btn accent-yellow" onclick="sendCmd('/syncUsb')">FIX USB DESYNC</button>
        <button class="btn accent-cyan" onclick="sendCmd('/usb')">USB SWITCH</button>
        
        <button id="kvmBtn" class="btn btn-full accent-cyan" onclick="sendCmd('/kvmResetToggle')">AUTO-RESET: ON</button>
        
        <button class="btn accent-cyan" onclick="sendCmd('/underToggle')">UNDERGLOW</button>
        <button class="btn accent-green" onclick="sendCmd('/headToggle')">HEADLIGHTS</button>
        <button class="btn accent-green" onclick="sendCmd('/brakeToggle')">BRAKES</button>
        <button class="btn accent-cyan" onclick="sendCmd('/soundToggle')">SOUND ON/OFF</button>
        
        <button id="btnChime" class="btn accent-yellow" onclick="sendCmd('/reminderToggle')">CHIME ON/OFF</button>
        <button id="btnLock" class="btn accent-green" onclick="sendCmd('/lockToggle')">LOCK / UNLOCK</button>
      </div>

      <!-- ANIMATIONS & MODES CARD -->
      <div class="card btn-grid">
        <button class="btn accent-yellow" onclick="sendCmd('/carmode')">CAR THEMES</button>
        <button class="btn accent-yellow" onclick="sendCmd('/highway')">HIGHWAY RUN</button>
        <button class="btn accent-yellow" onclick="sendCmd('/ambient')">AMBIENT FLOW</button>
        <button class="btn accent-yellow" onclick="sendCmd('/jukebox')">JUKEBOX</button>
        <button class="btn accent-yellow btn-full" onclick="sendCmd('/mario')">MARIO MODE</button>
      </div>

      <!-- POWER & NETWORK MANAGEMENT CARD -->
      <div class="card btn-grid">
        <div class="slider-container">
          <div class="slider-header">
            <span>SHUTDOWN TIMER</span>
            <span id="sleepDisplay" class="slider-val-highlight">60 MINS</span>
          </div>
          <input type="range" min="1" max="106" value="60" class="slider" id="sleepSlider" oninput="updateSleepDisplay(this.value)">
          <div class="sleep-controls">
            <button class="btn btn-micro" style="color:var(--neon-green);" onclick="startSleep()">START</button>
            <button class="btn btn-micro" style="color:var(--neon-yellow);" onclick="sendCmd('/pauseSleep')">PAUSE</button>
            <button class="btn btn-micro" style="color:var(--neon-red);" onclick="sendCmd('/cancelSleep')">CANCEL</button>
          </div>
        </div>
        
        <!-- MOVED DOWN: Power Controls & Network Configuration -->
        <button class="btn accent-green btn-full" style="margin-top:10px;" onclick="sendCmd('/on')">IGNITION (PC ON)</button>
        <button class="btn accent-red" onclick="sendCmd('/softoff')">PC SHUTDOWN</button>
        <button class="btn accent-red" onclick="sendCmd('/restart')">REBOOT PC</button>
        <button class="btn accent-red btn-full" style="background: rgba(255,0,0,0.1); border-color: rgba(255,0,0,0.2);" onclick="sendCmd('/off')">FORCE SHUTDOWN</button>
        
        <!-- FUTURE UPDATE: Network Settings Button -->
        <button class="btn btn-full" style="margin-top:10px;" onclick="window.location.href='/network'">WIFI & IP CONFIG</button>
      </div>

    </div>
<!-- HIDDEN HELP MODAL -->
      <div id="helpModal" class="modal">
        <div class="modal-content">
          <span class="close-btn" onclick="closeHelp()">&times;</span>
          <div class="modal-header">PORSCHE CORE: SYSTEM MANUAL</div>
          <div class="section-title" style="text-align:center; border-bottom:1px solid var(--card-border); padding-bottom:10px; margin-bottom:15px;">PHYSICAL CAR HARDWARE</div>
          
          <div class="help-item">
            <div class="help-title">FRONT BONNET (HOOD)</div>
            <div class="help-desc"><b>Single Tap:</b> Toggles the USB KVM Switch.<br><b>Double Tap:</b> Master System Wake / Sleep.<br><b>Hold 3 Seconds:</b> Toggles System Lock (Child Lock).<br><b>Hold 30 Seconds:</b> Triggers Captive Portal Wi-Fi Setup.</div>
          </div>

          <div class="help-item">
            <div class="help-title">LEFT DOOR (PASSENGER)</div>
            <div class="help-desc"><b>Single Tap:</b> Wakes the Desktop PC (Ignition).<br><b>Double Tap:</b> Reboots the Desktop PC.<br><b>Hold 3 Seconds:</b> Hard-Shutdowns the Desktop PC.</div>
          </div>

          <div class="help-item">
            <div class="help-title">RIGHT DOOR (DRIVER)</div>
            <div class="help-desc"><b>Single Tap:</b> Toggles Ambient Flow.<br><b>Double Tap:</b> Triggers Highway Run.<br><b>3 Taps:</b> Triggers Jukebox.<br><b>4 Taps:</b> Triggers Cinematic Car Themes.<br><b>5+ Taps:</b> Triggers Mario Mode.<br><b>Hold 3 Seconds:</b> Deep System Standby (Forces all off).</div>
          </div>

          <div class="section-title" style="text-align:center; border-bottom:1px solid var(--card-border); padding-bottom:10px; margin-bottom:15px; margin-top:20px;">WEB DASHBOARD CONTROLS</div>
          
          <div class="help-item">
            <div class="help-title">TELEMETRY SMART RINGS</div>
            <div class="help-desc"><b>Function:</b> A clean, center-less 3-tier visualizer for live system timers.<br><b>🔴 Outer Ring (Red):</b> The Shutdown Timer. Appears dynamically when an auto-sleep countdown is active.<br><b>🔵 Middle Ring (Cyan):</b> The 8-Hour Master Limit. Fills to track system stamina and prevent infinite idling.<br><b>🟡 Inner Ring (Yellow):</b> The Hourly Chime. Tracks the 60-minute cycle for the NTP-synced audio reminder.</div>
          </div>

          <div class="help-item">
            <div class="help-title">SYSTEM WAKE / SLEEP (MASTER SWITCH)</div>
            <div class="help-desc"><b>Function:</b> The master power relay for the entire Porsche Core ecosystem.<br><b>Scenarios:</b><br>- <i>Morning Boot:</i> Tap to wake the ESP32, restore internal memory, and prep the lighting matrix.<br>- <i>Night Mode:</i> Safely extinguishes all LEDs, halts animations, and enters deep standby without losing network connection.</div>
          </div>
          
          <div class="help-item">
            <div class="help-title">FIX USB DESYNC</div>
            <div class="help-desc"><b>Function:</b> Silently forces the software memory to align with the physical KVM hardware.<br><b>Scenarios:</b><br>- <i>Hardware Out-Of-Sync:</i> You manually pressed the physical switch on your KVM hub, and now the Web UI says "USB 2" but your physical hardware is on "USB 1". Tap this to fix the UI without sending an electrical pulse that would swap the screens again.</div>
          </div>
          
          <div class="help-item">
            <div class="help-title">HEADLIGHT INTENSITY</div>
            <div class="help-desc"><b>Function:</b> Master hardware limiter for the front white LED PWM signals.<br><b>Scenarios:</b><br>- <i>Late Night:</i> Slide down to 20% to prevent blinding glare while gaming in a dark room.<br>- <i>Daylight:</i> Slide to 100% to ensure start-up sparks and flashes remain visible during high sun.</div>
          </div>
          
          <div class="help-item">
            <div class="help-title">USB SWITCH & SEAMLESS HANDOFF</div>
            <div class="help-desc"><b>Function:</b> Fires a precise electrical pulse to KVM-swap your peripherals (mouse, keyboard, monitors) between computers.<br><b>Scenarios:</b><br>- <i>Manual Swap:</i> Tap to jump from Desktop to Laptop instantly.<br>- <i>Auto-Handoff:</i> If both PCs are running and you shut down the Desktop, the background RTOS tracker intercepts the power-loss and automatically drops you onto the surviving Laptop 15 seconds later.</div>
          </div>
          
          <div class="help-item">
            <div class="help-title">AUTO-RESET (KVM POWER MEMORY)</div>
            <div class="help-desc"><b>Function:</b> Dictates how the system recovers from a complete power loss to the KVM switch.<br><b>Scenarios:</b><br>- <i>ON:</i> Use this if your KVM draws power via USB from the PC. When the PC shuts down, the KVM dies and defaults back to USB 1 upon waking. The car will auto-correct the UI to match.<br>- <i>OFF:</i> Use this if your KVM is plugged directly into a wall outlet and permanently remembers where it was left.</div>
          </div>
          
          <div class="help-item">
            <div class="help-title">SOUND / CHIME CONFIGURATION</div>
            <div class="help-desc"><b>Function:</b> Independent audio routing for the piezo buzzer.<br><b>Scenarios:</b><br>- <i>SOUND:</i> Disables all UI button clicks and animation revving. Perfect for quiet mode.<br>- <i>CHIME:</i> Disables the NTP-synced hourly time-reminder beep without muting your car animations.</div>
          </div>
          
          <div class="help-item">
            <div class="help-title">LOCK / UNLOCK (CHILD LOCK)</div>
            <div class="help-desc"><b>Function:</b> Ignores all physical inputs from the car's doors.<br><b>Scenarios:</b><br>- <i>Cleaning:</i> Engage the lock from your phone before dusting or moving the physical car model to prevent accidental long-presses from hard-crashing your Desktop PC.<br>- <i>Auto-Lock:</i> The system will automatically engage the lock if left idle for 30 seconds.</div>
          </div>
          
          <div class="help-item">
            <div class="help-title">CAR THEMES (SMART DECK)</div>
            <div class="help-desc"><b>Function:</b> Engages the cinematic light and sound engine, pulling from a bank of 80 unique automotive, sci-fi, and elemental themes.<br><b>Scenarios:</b><br>- <i>Smart Memory:</i> The system mathematically guarantees you will never see the same theme twice until all 80 have been played.<br>- <i>Passive Entertainment:</i> Automatically cycles themes to provide a dynamic background show.</div>
          </div>
          
          <div class="help-item">
            <div class="help-title">HIGHWAY RUN (PHYSICS SIMULATOR)</div>
            <div class="help-desc"><b>Function:</b> A physics simulator running inside the ESP32, mimicking a real car driving down a highway.<br><b>Scenarios:</b><br>- <i>Dynamic RPM:</i> Engine audio pitches up and down organically as the virtual car shifts through 5 gears.<br>- <i>Traffic & Weather:</i> The math engine randomly calculates sudden traffic jams (triggering screeching brakes and hazard lights) or sudden rainstorms (dimming the lights and slowing the speed).</div>
          </div>
          
          <div class="help-item">
            <div class="help-title">AMBIENT FLOW</div>
            <div class="help-desc"><b>Function:</b> Exceptionally smooth, mathematically generated sine-wave breathing animations.<br><b>Scenarios:</b><br>- <i>Focus Mode:</i> Cycles seamlessly through 9 distinct patterns (like 180° sweeps, diagonal panning, and tidal waves) every 45 seconds to create a relaxing workspace with zero audio distraction.</div>
          </div>
          
          <div class="help-item">
            <div class="help-title">JUKEBOX & MARIO MODE</div>
            <div class="help-desc"><b>Function:</b> Transforms the car into a retro 8-bit synthesizer with synchronized LED flashes.<br><b>Scenarios:</b><br>- <i>Mario Mode:</i> Plays the iconic theme song, complete with a 50/50 randomized "Victory" or "Death" ending sequence.<br>- <i>Jukebox:</i> Shuffles through 13 retro tracks automatically.</div>
          </div>
          
          <div class="help-item">
            <div class="help-title">SHUTDOWN TIMER (AUTO-SLEEP)</div>
            <div class="help-desc"><b>Function:</b> A hardware-level countdown timer that safely powers down your computers and the car simultaneously.<br><b>Scenarios:</b><br>- <i>Late Night Movie:</i> Set the slider to 120 minutes and hit start. Fall asleep watching a movie, and the car will automatically pulse the motherboard for a soft-shutdown and extinguish all lights when the timer expires.</div>
          </div>
          
          <div class="help-item">
            <div class="help-title">IGNITION (PC ON)</div>
            <div class="help-desc"><b>Function:</b> Bypasses software networking to trigger the motherboard's literal power pins.<br><b>Scenarios:</b><br>- <i>Cold Boot:</i> Turns on your completely dead Desktop PC from your phone anywhere in the house, instantly executing a randomized 1-of-3 start-up spark sequence.</div>
          </div>
          
          <div class="help-item">
            <div class="help-title">PC SHUTDOWN vs. FORCE SHUTDOWN</div>
            <div class="help-desc"><b>Function:</b> Dual-tier power management for connected hardware.<br><b>Scenarios:</b><br>- <i>Soft Shutdown:</i> Sends a safe 500ms pulse to the motherboard to tell Windows to gracefully save your data and shut off.<br>- <i>Force Shutdown:</i> Sends a brutal 6-second continuous pulse to physically cut motherboard power. ONLY use this if Windows has completely blue-screened or frozen.</div>
          </div>
          
          <div class="help-item">
            <div class="help-title">WIFI & IP CONFIG (VALET OVERRIDE)</div>
            <div class="help-desc"><b>Function:</b> The physical backdoor and secure network configuration suite.<br><b>Scenarios:</b><br>- <i>Changing IPs:</i> Connects you to the secure login page to modify your network parameters without re-flashing code.<br>- <i>Forgot Password:</i> Physically open the car's Bonnet (Hood) for exactly 30 seconds. The car will scream, lock the brakes, and broadcast an emergency "PORSCHE-SETUP" Wi-Fi network. Connect to it to bypass the security screen and recover your custom Username/Password.</div>
          </div>
        </div>
      </div>
    <script>
      const uiSound = new Audio('https://actions.google.com/sounds/v1/cartoon/wood_plank_flicks.ogg');
      uiSound.volume = 0.2; 
      function openHelp() { document.getElementById('helpModal').style.display = 'flex'; }
      function closeHelp() { document.getElementById('helpModal').style.display = 'none'; }
      
      // --- DAY / NIGHT ENGINE (Professional Translucent) ---
      const themes = [
        { name: "NIGHT", icon: "☀️ DAY MODE", root: { "--bg-color": "#0b0c10", "--card-bg": "rgba(22, 23, 29, 0.65)", "--card-border": "rgba(255, 255, 255, 0.05)", "--card-shadow": "0 8px 32px rgba(0, 0, 0, 0.4)", "--btn-bg": "rgba(26, 27, 35, 0.6)", "--btn-hover": "rgba(34, 35, 45, 0.8)", "--text-main": "#e2e2e5", "--text-muted": "#7a7b86", "--neon-green": "#00ffaa", "--neon-cyan": "#00ddff", "--neon-yellow": "#ffcc00", "--neon-red": "#ff4444", "--ring-bg": "#252630", "--slider-thumb": "#ffffff", "--slider-glow": "rgba(255, 255, 255, 0.5)", "--glow-spread": "15px", "--ring-shadow": "drop-shadow(0 8px 16px rgba(0,0,0,0.4))" }},
        { name: "DAY", icon: "🌙 NIGHT MODE", root: { "--bg-color": "#e2e8f0", "--card-bg": "rgba(255, 255, 255, 0.45)", "--card-border": "rgba(255, 255, 255, 0.8)", "--card-shadow": "0 8px 32px rgba(0, 0, 0, 0.08)", "--btn-bg": "rgba(255, 255, 255, 0.5)", "--btn-hover": "rgba(255, 255, 255, 0.9)", "--text-main": "#0f172a", "--text-muted": "#64748b", "--neon-green": "#059669", "--neon-cyan": "#0284c7", "--neon-yellow": "#d97706", "--neon-red": "#dc2626", "--ring-bg": "rgba(0, 0, 0, 0.1)", "--slider-thumb": "#0f172a", "--slider-glow": "transparent", "--glow-spread": "0px", "--ring-shadow": "none" }}
      ];
      
      let currentThemeIdx = localStorage.getItem('porsche_theme') ? parseInt(localStorage.getItem('porsche_theme')) : 1;
      
      function applyTheme() {
        const t = themes[currentThemeIdx];
        for (let key in t.root) document.documentElement.style.setProperty(key, t.root[key]);
        document.getElementById('themeBtn').innerText = t.icon;
      }
      
      function toggleTheme() {
        uiSound.currentTime = 0; uiSound.play().catch(e=>console.log(e));
        currentThemeIdx = currentThemeIdx === 0 ? 1 : 0;
        localStorage.setItem('porsche_theme', currentThemeIdx);
        applyTheme();
      }

      // Link JS directly to the dynamic CSS variables so live status matches the theme!
      const colors = { green: "var(--neon-green)", red: "var(--neon-red)", yellow: "var(--neon-yellow)", muted: "var(--text-muted)", cyan: "var(--neon-cyan)" };
      
      let gateway = `ws://${window.location.hostname}/ws`;
      let websocket;

      function initWebSocket() {
        console.log('Opening WebSocket...');
        websocket = new WebSocket(gateway);
        websocket.onclose = () => { setTimeout(initWebSocket, 2000); };
        websocket.onmessage = onMessage;
      }

      function onMessage(event) {
        let d = JSON.parse(event.data);
        
        // Status & Color Matrix
        const st = document.getElementById('statusText'); 
        const dot = document.getElementById('liveDot');
        const ringSleep = document.getElementById('ringSleep');
        const ringMaster = document.getElementById('ringMaster');
        const ringChime = document.getElementById('ringChime');

        st.innerText = d.status;
        if(d.status==="OFF" || d.status==="SYSTEM STANDBY") { setStyle(st, colors.muted, false); dot.style.background = colors.muted; dot.style.boxShadow = "none"; }
        else if(d.status==="SHUTTING DOWN..." || d.status==="SYSTEM LOCKED") { setStyle(st, colors.red, true); dot.style.background = colors.red; dot.style.boxShadow = `0 0 10px ${colors.red}`; }
        else if(d.status==="RESTARTING"||d.status==="CHANGING THEME...") { setStyle(st, colors.yellow, true); dot.style.background = colors.yellow; dot.style.boxShadow = `0 0 10px ${colors.yellow}`; }
        else { setStyle(st, colors.green, true); dot.style.background = colors.green; dot.style.boxShadow = `0 0 10px ${colors.green}`; }
        
        // --- RING ANIMATIONS ---
        if (ringSleep && ringMaster && ringChime) {
            // Sleep Ring (Outer - Red)
            if(d.sleep !== "OFF") {
                ringSleep.style.strokeDashoffset = 251 - (251 * ((d.sleepPct || 0) / 100));
                ringSleep.style.strokeOpacity = "1";
            } else {
                ringSleep.style.strokeOpacity = "0"; 
            }

            // Master Ring (Middle - Cyan)
            let mPct = d.masterPct || 0;
            ringMaster.style.strokeDashoffset = 176 - (176 * (mPct / 100));
            // Show immediately when awake, hide when asleep
            ringMaster.style.strokeOpacity = (d.status === "OFF" || d.status === "SYSTEM STANDBY") ? "0" : "1"; 

            // Chime Ring (Inner - Yellow)
            if (d.timer === "MUTED") {
                ringChime.style.strokeOpacity = "0";
            } else {
                ringChime.style.strokeDashoffset = 100 - (100 * ((d.chimePct || 0) / 100));
                ringChime.style.strokeOpacity = "1"; 
            }
        }

        // PC & Laptop Scanners
        const pcEl = document.getElementById('pcDot'); const lapEl = document.getElementById('lapDot');
        pcEl.innerText = d.pc ? "ONLINE" : "OFFLINE"; setStyle(pcEl, d.pc ? colors.green : colors.muted, false);
        lapEl.innerText = d.lap ? "ONLINE" : "OFFLINE"; setStyle(lapEl, d.lap ? colors.green : colors.muted, false);
        
        // Telemetry Data
        document.getElementById('audioText').innerText = d.audio; setStyle(document.getElementById('audioText'), (d.audio==="ON") ? colors.green : colors.red, true);
        
        document.getElementById('timerText').innerText = d.timer; 
        if(d.timer === "MUTED") { setStyle(document.getElementById('timerText'), colors.muted, true); syncButtonColor('btnChime', colors.muted, 'accent-cyan'); }
        else { setStyle(document.getElementById('timerText'), colors.yellow, true); syncButtonColor('btnChime', colors.yellow, 'accent-yellow'); }
        
        document.getElementById('lockText').innerText = d.lock;
        if(d.lock === "LOCKED") { setStyle(document.getElementById('lockText'), colors.red, true); syncButtonColor('btnLock', colors.red, 'accent-red'); }
        else { setStyle(document.getElementById('lockText'), colors.green, true); syncButtonColor('btnLock', colors.green, 'accent-green'); }
        
        document.getElementById('sleepText').innerText = d.sleep;
        setStyle(document.getElementById('sleepText'), (d.sleep==="OFF") ? colors.muted : (d.sleep==="PAUSED" ? colors.yellow : colors.red), true);
        
        document.getElementById('kvmBtn').innerText = d.kvm;
        if (d.kvm.includes("ON")) syncButtonColor('kvmBtn', colors.cyan, 'accent-cyan'); else syncButtonColor('kvmBtn', colors.muted, 'accent-cyan');
      }

      function calcSleepMins(val) { val = parseInt(val); return val <= 60 ? val : 60 + (val - 60) * 30; }
      function updateSleepDisplay(val) {
        let mins = calcSleepMins(val);
        document.getElementById('sleepDisplay').innerText = mins < 60 ? mins + " MINS" : Math.floor(mins / 60) + (mins % 60 > 0 ? ":30" : "") + " HR";
      }

      function startSleep() { sendCmd('/startSleep?m=' + calcSleepMins(document.getElementById('sleepSlider').value)); }
      
      // Look how clean sendCmd is now! No more fetch chaining!
      function sendCmd(cmd) { 
        uiSound.currentTime = 0; uiSound.play().catch(e => console.log(e)); 
        fetch(cmd).catch(err => console.error(err)); 
      }
      function sendSliderVal(val) { fetch('/setBright?v=' + val); }
      
      // Softened the glow radius to look natural on bright backgrounds
      function setStyle(el, color, glow) { el.style.color = color; el.style.textShadow = "none"; }
      function syncButtonColor(btnId, color, accentClass) { const btn = document.getElementById(btnId); btn.style.color = color; btn.className = `btn ${accentClass}`; }
      
      window.onload = () => { 
        applyTheme(); // Loads your saved theme instantly!
        initWebSocket();
        updateSleepDisplay(document.getElementById('sleepSlider').value); 
      };
    </script>
  </body>
  </html>
)rawliteral";

// ---------------- WEB UI HTML (SECURE LOGIN) ----------------
const char loginHtml[] PROGMEM = R"rawliteral(
  <!DOCTYPE html>
  <html lang="en">
  <head>
    <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no, viewport-fit=cover">
    <title>Porsche Secure Login</title>
    <style>
      :root { --bg-color: #0b0c10; --card-bg: rgba(22, 23, 29, 0.65); --card-border: rgba(255, 255, 255, 0.05); --card-shadow: 0 8px 32px rgba(0, 0, 0, 0.4); --btn-bg: rgba(26, 27, 35, 0.6); --btn-hover: rgba(34, 35, 45, 0.8); --text-main: #e2e2e5; --text-muted: #7a7b86; --neon-cyan: #00ddff; --neon-green: #00ffaa; }
      body { background: var(--bg-color); font-family: 'Inter', sans-serif; display: flex; flex-direction: column; align-items: center; justify-content: center; height: 100vh; margin: 0; transition: background 0.3s ease; }
      .card { background: var(--card-bg); border: 1px solid var(--card-border); border-radius: 24px; padding: 30px 20px; box-shadow: var(--card-shadow); text-align: center; width: calc(100% - 40px); max-width: 350px; box-sizing: border-box; backdrop-filter: blur(16px); -webkit-backdrop-filter: blur(16px); transition: all 0.3s ease; }
      .header-container { border-bottom: 1px solid var(--card-border); padding-bottom: 15px; margin-bottom: 25px; }
      h2 { color: var(--text-main); font-style: italic; font-weight: 900; letter-spacing: 2px; margin: 0 0 5px 0; font-size: 20px; }
      .subtitle { font-size: 9px; color: var(--text-muted); letter-spacing: 2px; font-weight: 700; }
      input { width: 100%; padding: 15px; margin-bottom: 15px; background: var(--btn-bg); border: 1px solid var(--card-border); color: var(--text-main); border-radius: 12px; outline: none; box-sizing: border-box; text-align: center; font-size: 14px; font-weight: bold; letter-spacing: 1px; transition: all 0.3s ease;}
      input:focus { border-color: var(--neon-cyan); box-shadow: 0 0 12px rgba(0,221,255,0.2); }
      .btn { width: 100%; padding: 15px; background: var(--btn-bg); border: 1px solid var(--card-border); color: var(--text-main); font-weight: 900; font-size: 12px; letter-spacing: 2px; border-radius: 12px; cursor: pointer; transition: 0.2s; backdrop-filter: blur(8px); -webkit-backdrop-filter: blur(8px); box-sizing: border-box; }
      .btn:active { transform: scale(0.95); background: var(--btn-hover); }
    </style>
  </head>
  <body>
    <div class="card">
      <div class="header-container">
        <h2>PORSCHE SECURE LOGIN</h2>
        <div class="subtitle">SYSTEM AUTHORIZATION</div>
      </div>
      <form action="/login" method="GET">
        <input type="text" name="u" placeholder="USERNAME" required>
        <input type="password" name="p" placeholder="PASSWORD" required>
        <button class="btn" type="submit" style="color: var(--neon-green); border-bottom: 2px solid var(--neon-green);">AUTHORIZE</button>
      </form>
    </div>
    <script>
      // --- APPLY DAY/NIGHT THEME FROM MAIN UI MEMORY ---
      const themes = [
        { root: { "--bg-color": "#0b0c10", "--card-bg": "rgba(22, 23, 29, 0.65)", "--card-border": "rgba(255, 255, 255, 0.05)", "--card-shadow": "0 8px 32px rgba(0, 0, 0, 0.4)", "--btn-bg": "rgba(26, 27, 35, 0.6)", "--btn-hover": "rgba(34, 35, 45, 0.8)", "--text-main": "#e2e2e5", "--text-muted": "#7a7b86", "--neon-green": "#00ffaa", "--neon-cyan": "#00ddff" }},
        { root: { "--bg-color": "#e2e8f0", "--card-bg": "rgba(255, 255, 255, 0.45)", "--card-border": "rgba(255, 255, 255, 0.8)", "--card-shadow": "0 8px 32px rgba(0, 0, 0, 0.08)", "--btn-bg": "rgba(255, 255, 255, 0.5)", "--btn-hover": "rgba(255, 255, 255, 0.9)", "--text-main": "#0f172a", "--text-muted": "#64748b", "--neon-green": "#059669", "--neon-cyan": "#0284c7" }}
      ];
      let currentThemeIdx = localStorage.getItem('porsche_theme') ? parseInt(localStorage.getItem('porsche_theme')) : 1;
      const t = themes[currentThemeIdx];
      for (let key in t.root) document.documentElement.style.setProperty(key, t.root[key]);
    </script>
  </body>
  </html>
)rawliteral";

// ---------------- WEB UI HTML (NETWORK SETUP) ----------------
const char networkHtml[] PROGMEM = R"rawliteral(
  <!DOCTYPE html>
  <html lang="en">
  <head>
    <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
    <title>Porsche Setup</title>
    <style>
      :root { --bg-color: #0b0c10; --card-bg: rgba(22, 23, 29, 0.65); --card-border: rgba(255, 255, 255, 0.05); --card-shadow: 0 8px 32px rgba(0, 0, 0, 0.4); --btn-bg: rgba(26, 27, 35, 0.6); --btn-hover: rgba(34, 35, 45, 0.8); --text-main: #e2e2e5; --text-muted: #7a7b86; --neon-cyan: #00ddff; --neon-green: #00ffaa; --neon-red: #ff4444; --neon-yellow: #ffcc00; }
      body { background: var(--bg-color); color: var(--text-main); font-family: 'Inter', sans-serif; display: flex; flex-direction: column; align-items: center; padding: 20px 15px; margin: 0; transition: background 0.3s ease; }
      .card { background: var(--card-bg); border: 1px solid var(--card-border); border-radius: 24px; padding: 25px; width: 100%; max-width: 400px; box-shadow: var(--card-shadow); margin-bottom: 20px; backdrop-filter: blur(16px); -webkit-backdrop-filter: blur(16px); transition: all 0.3s ease; }
      h2 { text-align: center; color: var(--neon-cyan); font-style: italic; font-weight: 900; letter-spacing: 1px; margin-top: 0; border-bottom: 1px solid var(--card-border); padding-bottom: 15px; }
      label { font-size: 10px; color: var(--text-muted); font-weight: 800; letter-spacing: 1px; display: block; margin: 15px 0 5px; }
      input { width: 100%; padding: 14px; background: var(--btn-bg); border: 1px solid var(--card-border); color: var(--text-main); border-radius: 12px; outline: none; font-family: monospace; font-size: 13px; box-sizing: border-box; transition: all 0.3s; }
      input:focus { border-color: var(--neon-cyan); box-shadow: 0 0 12px rgba(0,221,255,0.2); }
      .btn-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 15px; margin-top: 25px; }
      .btn { padding: 18px 10px; border: none; border-radius: 16px; font-weight: 800; font-size: 11px; letter-spacing: 1px; cursor: pointer; color: var(--text-main); transition: transform 0.2s; backdrop-filter: blur(8px); -webkit-backdrop-filter: blur(8px); }
      .btn:active { transform: scale(0.95); background: var(--btn-hover); }
      .btn-save { background: var(--btn-bg); border-bottom: 2px solid var(--neon-green); color: var(--neon-green); }
      .btn-back { background: var(--btn-bg); border-bottom: 2px solid var(--neon-red); color: var(--neon-red); }
      .section-title { font-size: 11px; color: var(--neon-yellow); font-weight: 900; letter-spacing: 2px; margin-top: 20px; margin-bottom: -5px; }
    </style>
  </head>
  <body>
    <div class="card">
      <h2>SYSTEM CONFIG</h2>
      <div class="section-title">WEB DASHBOARD SECURITY</div>
      <label>DASHBOARD USERNAME</label><input type="text" id="ui_u">
      <label>DASHBOARD PASSWORD</label><input type="text" id="ui_p"> 
      <div class="section-title">HOME NETWORK</div>
      <label>WIFI SSID</label><input type="text" id="ssid">
      <label>WIFI PASSWORD</label><input type="password" id="pass">
      <div class="section-title">IP ADDRESSES</div>
      <label>ROUTER GATEWAY IP</label><input type="text" id="ip_gateway">
      <label>CAR (ESP32) IP</label><input type="text" id="ip_esp">
      <label>DESKTOP LAN IP</label><input type="text" id="ip_d_lan">
      <label>DESKTOP WIFI IP</label><input type="text" id="ip_d_wifi">
      <label>LAPTOP LAN IP</label><input type="text" id="ip_l_lan">
      <label>LAPTOP WIFI IP</label><input type="text" id="ip_l_wifi">
      <div class="btn-grid">
        <button class="btn btn-back" onclick="window.location.href='/'">CANCEL</button>
        <button class="btn btn-save" onclick="saveNet()">SAVE & REBOOT</button>
      </div>
    </div>
    <script>
      // --- APPLY DAY/NIGHT THEME FROM MAIN UI MEMORY ---
      const themes = [
        { root: { "--bg-color": "#0b0c10", "--card-bg": "rgba(22, 23, 29, 0.65)", "--card-border": "rgba(255, 255, 255, 0.05)", "--card-shadow": "0 8px 32px rgba(0, 0, 0, 0.4)", "--btn-bg": "rgba(26, 27, 35, 0.6)", "--btn-hover": "rgba(34, 35, 45, 0.8)", "--text-main": "#e2e2e5", "--text-muted": "#7a7b86", "--neon-green": "#00ffaa", "--neon-cyan": "#00ddff", "--neon-yellow": "#ffcc00", "--neon-red": "#ff4444" }},
        { root: { "--bg-color": "#e2e8f0", "--card-bg": "rgba(255, 255, 255, 0.45)", "--card-border": "rgba(255, 255, 255, 0.8)", "--card-shadow": "0 8px 32px rgba(0, 0, 0, 0.08)", "--btn-bg": "rgba(255, 255, 255, 0.5)", "--btn-hover": "rgba(255, 255, 255, 0.9)", "--text-main": "#0f172a", "--text-muted": "#64748b", "--neon-green": "#059669", "--neon-cyan": "#0284c7", "--neon-yellow": "#d97706", "--neon-red": "#dc2626" }}
      ];
      let currentThemeIdx = localStorage.getItem('porsche_theme') ? parseInt(localStorage.getItem('porsche_theme')) : 1;
      const t = themes[currentThemeIdx];
      for (let key in t.root) document.documentElement.style.setProperty(key, t.root[key]);

      fetch('/getNetwork').then(r=>r.json()).then(d => {
        document.getElementById('ui_u').value = d.ui_user; document.getElementById('ui_p').value = d.ui_pass;
        document.getElementById('ssid').value = d.ssid; document.getElementById('pass').value = d.pass;
        document.getElementById('ip_gateway').value = d.ip_gateway; document.getElementById('ip_esp').value = d.ip_esp;
        document.getElementById('ip_d_lan').value = d.ip_d_lan; document.getElementById('ip_d_wifi').value = d.ip_d_wifi;
        document.getElementById('ip_l_lan').value = d.ip_l_lan; document.getElementById('ip_l_wifi').value = d.ip_l_wifi;
      });

      function saveNet() {
        let q = `?ui_u=${encodeURIComponent(document.getElementById('ui_u').value)}`+
                `&ui_p=${encodeURIComponent(document.getElementById('ui_p').value)}`+
                `&ssid=${encodeURIComponent(document.getElementById('ssid').value)}`+
                `&pass=${encodeURIComponent(document.getElementById('pass').value)}`+
                `&ip_gateway=${document.getElementById('ip_gateway').value}`+
                `&ip_esp=${document.getElementById('ip_esp').value}`+
                `&ip_d_lan=${document.getElementById('ip_d_lan').value}`+
                `&ip_d_wifi=${document.getElementById('ip_d_wifi').value}`+
                `&ip_l_lan=${document.getElementById('ip_l_lan').value}`+
                `&ip_l_wifi=${document.getElementById('ip_l_wifi').value}`;
        
        document.querySelector('.btn-save').innerText = 'SAVING...';
        fetch('/saveNetwork'+q).then(()=>{
          document.querySelector('.btn-save').innerText = 'REBOOTING...';
          document.querySelector('.btn-save').style.color = '#ffcc00';
          setTimeout(()=>window.location.href='/', 6000); 
        });
      }
    </script>
  </body>
  </html>
)rawliteral";

// ---------------- ADVANCED HARDWARE HELPERS ----------------
void setHead(int left, int right) {
  if (!headlightState) { left = 0; right = 0; } // <--- HARDWARE SHIELD
  analogWrite(L_HEAD_PIN, (left * maxHeadBright) / 255);
  analogWrite(R_HEAD_PIN, (right * maxHeadBright) / 255);
}
void setBrake(int left, int right) {
  if (!brakeState) { left = 0; right = 0; } // <--- HARDWARE SHIELD
  analogWrite(L_BRAKE_PIN, left);
  analogWrite(R_BRAKE_PIN, right);
}
void setUnder(int val) {
  if (!underbodyState) val = 0; // <--- HARDWARE SHIELD
  analogWrite(UNDERBODY_PIN, val);
}
void setAllHead(int val) {
  setHead(val, val);
}
void setAllBrake(int val) {
  setBrake(val, val);
}

void playTone(int freq) {
  if (globalSoundEnabled) tone(BUZZER_PIN, freq);
}
void playToneDur(int freq, int duration) {
  if (globalSoundEnabled) tone(BUZZER_PIN, freq, duration);
}
void stopTone() {
  noTone(BUZZER_PIN);
}

// ---------------- BACKGROUND RTOS NETWORK TASK (THREAD-SAFE) ----------------
bool checkPCOnline() {
  WiFiClient client;
  static int lastGoodIP = 0; 
  // THE LAPTOP FIX: Always use 250ms so Laptops on Wi-Fi have enough time to answer!
  int timeout = 250; 
  
  if (client.connect(targetIPs[lastGoodIP], 135, timeout)) {
    client.stop();
    activeDeviceIndex = lastGoodIP; 
    return true;
  }
  for (int i = 0; i < numIPs; i++) {
    if (i == lastGoodIP) continue; 
    if (client.connect(targetIPs[i], 135, timeout)) {
      lastGoodIP = i; 
      activeDeviceIndex = i; 
      client.stop();
      return true;
    }
  }
  activeDeviceIndex = -1;
  return false; 
}

void PingTask(void * parameter) {
  static int previousActiveCategory = -1; // 0 for PC, 1 for Laptop
  static bool firstBootCalibrated = false; // SILENT CALIBRATION FLAG
  
  for(;;) {
    if (ignoreNetwork) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        continue;
    } 

    bool deskOnline = false;
    bool lapOnline = false;
    WiFiClient client;

    if (client.connect(targetIPs[0], 135, 200)) { deskOnline = true; client.stop(); }
    else if (client.connect(targetIPs[1], 135, 200)) { deskOnline = true; client.stop(); }

    if (client.connect(targetIPs[2], 135, 200)) { lapOnline = true; client.stop(); }
    else if (client.connect(targetIPs[3], 135, 200)) { lapOnline = true; client.stop(); }

    // 🛡️ THE DUAL-SHOCK ABSORBERS (10-Second Buffer)
    // 🚨 THE FIX: Start at 15 so it doesn't pretend a dead PC is alive on reboot!
    static int deskFails = 15; 
    if (!deskOnline) {
      if (deskFails < 15) deskFails++;
      if (deskFails < 10) deskOnline = true; // Pretend Desktop is still alive!
    } else {
      deskFails = 0; // Reset only when genuinely detected
    }

    static int lapFails = 15;
    if (!lapOnline) {
      if (lapFails < 15) lapFails++;
      if (lapFails < 10) lapOnline = true; // Pretend Laptop is still alive!
    } else {
      lapFails = 0; // Reset only when genuinely detected
    }

    isDesktopOnline = deskOnline;
    isLaptopOnline = lapOnline;

    // Calculate Master State based on the buffered data!
    bool isOnline = deskOnline || lapOnline;
    int currentCategory = -1;
    if (deskOnline) currentCategory = 0;
    if (lapOnline) currentCategory = 1;
    
    // 🚨 THE HANDOFF BUG FIX:
    // If BOTH PCs are online, the background tracker MUST follow your UI state!
    if (deskOnline && lapOnline) {
        currentCategory = isUsbState2 ? 1 : 0;
    }
    
    if (isOnline && !lastPcOnline) { // Waking Up!
      bool laptopIsAwake = (currentCategory == 1);
      
      // 🛑 THE KVM POWER LOSS FIX
      // If offline for 2 mins, assume KVM lost power... BUT ONLY if the feature is enabled!
      if (millis() - lastOfflineTime > 120000) {
          if (kvmAutoReset) {
              isUsbState2 = false; 
          }
      }

      // 🚨 THE KVM FLASH FIX: Silently align software memory to hardware reality!
      if (!firstBootCalibrated) {
          firstBootCalibrated = true;
          isUsbState2 = laptopIsAwake;
          prefs.putBool("usb", isUsbState2);
          needsUsbSwitchPulse = false; // NO PHYSICAL PULSE!
      } else {
          if (laptopIsAwake && !isUsbState2) {
             isUsbState2 = true; prefs.putBool("usb", true); needsUsbSwitchPulse = true;
          } else if (!laptopIsAwake && isUsbState2) {
             isUsbState2 = false; prefs.putBool("usb", false); needsUsbSwitchPulse = true;
          } else {
             needsUsbSwitchPulse = false; 
          }
      }
      
      // 🛑 BUG 3 FIX: Only Auto-Wake if the car is currently asleep!
      if (currentMode == MODE_IDLE) {
          setNewMasterAction(ACTION_AUTO_WAKE); 
      }
    }
    else if (!isOnline && lastPcOnline) { // Shutting Down!
      lastOfflineTime = millis(); 
      
      // 🛑 BUG 3 FIX: Only Auto-Shutdown if the car is currently awake!
      if (currentMode != MODE_IDLE) {
          setNewMasterAction(ACTION_AUTO_SHUTDOWN); 
      }
    }
    else if (isOnline && lastPcOnline) { 
      // Seamless Handoff
      if (previousActiveCategory != -1 && currentCategory != -1 && currentCategory != previousActiveCategory) {
          bool laptopIsAwake = (currentCategory == 1);
          if (laptopIsAwake && !isUsbState2) {
              isUsbState2 = true; prefs.putBool("usb", true); carStatus = "USB 2";
              setNewMasterAction(ACTION_USB_EFFECT);
          } else if (!laptopIsAwake && isUsbState2) {
              isUsbState2 = false; prefs.putBool("usb", false); carStatus = "USB 1";
              setNewMasterAction(ACTION_USB_EFFECT);
          }
      }
    }
    
    lastPcOnline = isOnline; 
    if (isOnline && currentCategory != -1) previousActiveCategory = currentCategory;
    
    vTaskDelay(pdMS_TO_TICKS(1000)); 
  }
}

void setNewMasterAction(Action newAction) {
  cancelEffect = true;
  pendingAction = newAction;
}

// =======================================================================
// GLOBAL INTERRUPT & TIMING SYSTEMS
// =======================================================================

void check8HourLimit() {
  static bool timerActive = false;
  if (currentMode == MODE_IDLE) {
    timerActive = false;
    globalAwakeStartTime = 0; // Resets the UI ring to 0% when car sleeps
  } else {
    if (!timerActive) {
      globalAwakeStartTime = millis();
      timerActive = true;
    }
    if (millis() - globalAwakeStartTime >= 28800000UL) {
      timerActive = false;
      executeShutdown();
    }
  }
}

void checkSleepTimer() {
  if (sleepTimerActive && !sleepTimerPaused) {
    if (millis() - sleepStartTime >= sleepDurationMs) {
      sleepTimerActive = false;
      // 🚨 WE ARE BACK TO SOFT SHUTDOWN! 
      // This will now use the exact same USB Jiggle sequence as the Web UI button.
      executeSoftShutdown();  
    }
  }
}

bool checkInterrupt() {
  ArduinoOTA.handle();
  handleBonnet();
  handleLeftDoor();
  handleRightDoor();
  check8HourLimit();
  checkSleepTimer();
  return cancelEffect;
}

void checkHourlyReminder() {
  if (!reminderEnabled) return;
  time_t now;
  time(&now);
  struct tm *timeinfo = localtime(&now);
  if (timeinfo && timeinfo->tm_year > 100) {
    if (timeinfo->tm_min == 0) {
      if (lastRemindedHour != timeinfo->tm_hour) {
        lastRemindedHour = timeinfo->tm_hour;
        reminderEffect();
      }
    }
  }
}

// 🚀 NEW: Forces Web UI to update even while animations block the system!
void wsTick() {
  static unsigned long lastWsUpdate = 0;
  if (millis() - lastWsUpdate > 400) {
    lastWsUpdate = millis();
    ws.cleanupClients();
    notifyClients();
  }
}

bool smartDelay(unsigned long ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    if (checkInterrupt()) return true;
    checkHourlyReminder();
    wsTick(); // <--- MAGIC UI SYNC FIX
    delay(1);
    yield();
  }
  return false;
}

void uiTick() {
  // ⚡ ANTI-LAG FIX: Don't beep the UI button if the car is currently playing music!
  if (!marioModeState && !jukeboxModeState && !carModeState && !highwayRunState && !pcOverloadState) {
    playToneDur(2500, 30);
  }
}

void checkDailyReset() {
  if (millis() - lastTimeCheck > 60000 || lastTimeCheck == 0) {
    lastTimeCheck = millis();
    time_t now;
    time(&now);
    struct tm *timeinfo = localtime(&now);
    if (timeinfo && timeinfo->tm_year > 100) {
      if (currentDay == -1) {
        currentDay = timeinfo->tm_mday;
      } else if (currentDay != timeinfo->tm_mday) {
        maxHeadBright = 255;
        if (headlightState && currentMode == MODE_ON && !highwayRunState && !jukeboxModeState) setAllHead(255);
        currentDay = timeinfo->tm_mday;
      }
    }
  }
}

// =======================================================================
// CORE LOGIC EXECUTORS (Safely runs in Web Thread)
// =======================================================================
void clearModes() {
  jukeboxModeState = false;
  highwayRunState = false;
  ambientState = false;
  carModeState = false;
  marioModeState = false;
}
void executeIgnition() {
  clearModes();
  currentMode = MODE_ON;
  carStatus = "IGNITION TRIGGERED"; // <--- FIX: No more boring "ON"
  setNewMasterAction(ACTION_IGNITION);
}
void executeRestart() {
  uiTick();
  clearModes();
  currentMode = MODE_ON;
  carStatus = "RESTARTING";
  setNewMasterAction(ACTION_RESTART);
}
void executeShutdown() {
  uiTick();
  carStatus = "SHUTTING DOWN..."; // <--- Instant feedback
  setNewMasterAction(ACTION_SHUTDOWN);
}
void executeSoftShutdown() {
  uiTick();
  carStatus = "SHUTTING DOWN..."; // <--- Instant feedback
  setNewMasterAction(ACTION_SOFT_SHUTDOWN);
}
void executeUSB() {
  isUsbState2 = !isUsbState2;
  prefs.putBool("usb", isUsbState2);
  clearModes();
  currentMode = MODE_ON;
  carStatus = isUsbState2 ? "USB 2" : "USB 1";
  setNewMasterAction(ACTION_USB_EFFECT);
}

void executeMarioMode() {
  uiTick(); bool wasOn = marioModeState; allOff(); clearModes();
  if (!wasOn) { 
    marioModeState = true; currentMode = MODE_ON; carStatus = "MARIO"; 
    headlightState = true; brakeState = true; underbodyState = true; // 🔓 Lift Shields
    setNewMasterAction(ACTION_NONE); 
  } else { 
    // THE SMART RETURN FIX: Drops back to IDLE instead of dying!
    currentMode = MODE_ON; carStatus = "SYSTEM IDLE"; 
    headlightState = true; brakeState = true; underbodyState = true;
    setNewMasterAction(ACTION_NONE); 
  }
}

void executeCarMode() {
  uiTick(); bool wasOn = carModeState; allOff(); clearModes();
  if (!wasOn) { 
    carModeState = true; currentMode = MODE_ON; carStatus = "CAR MODE"; 
    headlightState = true; brakeState = true; underbodyState = true; 
    setNewMasterAction(ACTION_NONE); 
  } else { 
    currentMode = MODE_ON; carStatus = "SYSTEM IDLE"; 
    headlightState = true; brakeState = true; underbodyState = true;
    setNewMasterAction(ACTION_NONE); 
  }
}

void executeHighwayRun() {
  uiTick(); bool wasOn = highwayRunState; allOff(); clearModes();
  if (!wasOn) { 
    highwayRunState = true; currentMode = MODE_ON; carStatus = "HIGHWAY RUN"; 
    headlightState = true; brakeState = true; underbodyState = true; 
    setNewMasterAction(ACTION_NONE); forceHighwayStart = true; 
  } else { 
    currentMode = MODE_ON; carStatus = "SYSTEM IDLE"; 
    headlightState = true; brakeState = true; underbodyState = true;
    setNewMasterAction(ACTION_NONE); 
  }
}

void executeJukeboxMode() {
  uiTick(); bool wasOn = jukeboxModeState; allOff(); clearModes();
  if (!wasOn) { 
    jukeboxModeState = true; currentMode = MODE_ON; carStatus = "JUKEBOX"; 
    headlightState = true; brakeState = true; underbodyState = true; 
    setNewMasterAction(ACTION_NONE); 
  } else { 
    currentMode = MODE_ON; carStatus = "SYSTEM IDLE"; 
    headlightState = true; brakeState = true; underbodyState = true;
    setNewMasterAction(ACTION_NONE); 
  }
}

void executeAmbientToggle() {
  uiTick(); bool wasOn = ambientState; allOff(); clearModes();
  if (!wasOn) { 
    ambientState = true; currentMode = MODE_AMBIENT; carStatus = "AMBIENT"; 
    headlightState = true; brakeState = true; underbodyState = true; 
    setNewMasterAction(ACTION_NONE); ambientStartTime = millis(); 
  } else { 
    currentMode = MODE_ON; carStatus = "SYSTEM IDLE"; 
    headlightState = true; brakeState = true; underbodyState = true;
    setNewMasterAction(ACTION_NONE); 
  }
}

void executeHeadToggle() {
  uiTick(); headlightState = !headlightState; restoreLights();
  tempStatus = headlightState ? "HEADLIGHTS ON" : "HEADLIGHTS OFF"; tempStatusTime = millis();
}

void executeBrakeToggle() {
  uiTick(); brakeState = !brakeState; restoreLights();
  tempStatus = brakeState ? "BRAKES ON" : "BRAKES OFF"; tempStatusTime = millis();
}

void executeUnderToggle() {
  uiTick(); underbodyState = !underbodyState; restoreLights();
  tempStatus = underbodyState ? "UNDERGLOW ON" : "UNDERGLOW OFF"; tempStatusTime = millis();
}

void executeSoundToggle() {
  globalSoundEnabled = !globalSoundEnabled; prefs.putBool("sound", globalSoundEnabled);
  if (!globalSoundEnabled) { noTone(BUZZER_PIN); } 
  else { if (!jukeboxModeState && !marioModeState && !carModeState && !highwayRunState && currentMode != MODE_AMBIENT) { tone(BUZZER_PIN, 2000, 100); } }
  tempStatus = globalSoundEnabled ? "SOUND ON" : "SOUND MUTED"; tempStatusTime = millis();
}

void executeReminderToggle() {
  uiTick(); reminderEnabled = !reminderEnabled; prefs.putBool("reminder", reminderEnabled);
  tempStatus = reminderEnabled ? "CHIME ON" : "CHIME MUTED"; tempStatusTime = millis();
}

void executeLockToggle() {
  uiTick();
  controlsLocked = !controlsLocked;
  lastActivityTime = millis();
  
  // Route this to the main loop to prevent LED threading crashes!
  setNewMasterAction(ACTION_LOCK_TOGGLE); 
}

void executeMasterToggle() {
  uiTick(); setNewMasterAction(ACTION_NONE);
  if (currentMode == MODE_IDLE && !headlightState && !brakeState && !underbodyState) {
    clearModes(); currentMode = MODE_ON;
    headlightState = true; brakeState = true; underbodyState = true;
    carStatus = "SYSTEM IDLE"; // <--- FIX: No more "ALL LIGHTS ON"
    setAllHead(255); setAllBrake(255); setUnder(255);
    playToneDur(1200, 50); delay(50); playToneDur(1800, 100);
  } else {
    clearModes(); currentMode = MODE_IDLE;
    carStatus = "SYSTEM STANDBY"; // <--- FIX: Proper sleep text
    setNewMasterAction(ACTION_ALL_OFF);
  }
}

// =======================================================================
// DEBOUNCED HARDWARE MAP
// =======================================================================
void handleBonnet() {
  static unsigned long lastChange = 0;
  static bool stableState = HIGH;
  static int openCount = 0;
  static unsigned long bonnetPressStart = 0;
  static bool bonnetLongPressTriggered = false;
  static bool bonnetResetTriggered = false; // 🚨 NEW

  int reading = digitalRead(BONNET_PIN);

  if (reading != stableState && millis() - lastChange > 50) {
    lastChange = millis();
    if (reading == LOW) {
      bonnetPressStart = millis();
      bonnetLongPressTriggered = false;
      bonnetResetTriggered = false;
    } else {
      if (!bonnetLongPressTriggered && !bonnetResetTriggered) openCount++;
    }
    stableState = reading;
  }
  
  // 3-Second Rule: Lock / Unlock Toggle
  if (stableState == LOW && !bonnetLongPressTriggered && millis() - bonnetPressStart > 3000) {
    bonnetLongPressTriggered = true;
    openCount = 0;
    controlsLocked = !controlsLocked;
    lastActivityTime = millis();
    if (controlsLocked) playLockAnimation();
    else playUnlockAnimation();
  }

  // 30-Second Rule: 🚨 ACTIVATE CAPTIVE PORTAL 🚨
  if (stableState == LOW && !bonnetResetTriggered && millis() - bonnetPressStart > 30000) {
    bonnetResetTriggered = true;
    openCount = 0;
    startCaptivePortal(); 
  }

  if (stableState == HIGH && openCount > 0 && millis() - lastChange > 1000) {
    int count = openCount;
    openCount = 0;
    if (!controlsLocked) {
      if (count == 1) executeUSB();
      else if (count >= 2) executeMasterToggle(); 
    }
  }
}

void handleLeftDoor() {
  static unsigned long lastChange = 0;
  static bool stableState = HIGH;
  static int clickCount = 0;
  static unsigned long lastReleaseTime = 0;
  int reading = digitalRead(LEFT_PIN);
  if (reading != stableState && millis() - lastChange > 50) {
    lastChange = millis();
    if (reading == LOW) {
      leftDoorPressStart = millis();
      longPressTriggered = false;
    } else {
      if (!longPressTriggered) {
        clickCount++;
        lastReleaseTime = millis();
      }
    }
    stableState = reading;
  }
  if (stableState == LOW && !longPressTriggered && millis() - leftDoorPressStart > 3000) {
    longPressTriggered = true;
    clickCount = 0;
    if (!controlsLocked) executeShutdown();
  }
  if (stableState == HIGH && clickCount > 0 && millis() - lastReleaseTime > 500) {
    int count = clickCount;
    clickCount = 0;
    if (!controlsLocked) {
      if (count == 1) executeIgnition();
      else if (count >= 2) executeRestart();
    }
  }
}

void handleRightDoor() {
  static unsigned long lastChange = 0;
  static bool stableState = HIGH;
  static int openCount = 0;
  static unsigned long lastCloseTime = 0;
  static unsigned long rightDoorOpenStart = 0;
  static bool rightLongPressTriggered = false;

  int reading = digitalRead(RIGHT_PIN);

  if (reading != stableState && millis() - lastChange > 50) {
    lastChange = millis();
    if (reading == LOW) { // Door Opened
      rightDoorOpenStart = millis();
      rightLongPressTriggered = false;
    } else { // Door Closed
      if (!rightLongPressTriggered) {
        openCount++;
        lastCloseTime = millis();
      }
    }
    stableState = reading;
  }

  // 🚨 THE LONG PRESS: If left open for 3 seconds, safely turn the car off (Standby)
  if (stableState == LOW && !rightLongPressTriggered && millis() - rightDoorOpenStart > 3000) {
    rightLongPressTriggered = true;
    openCount = 0;
    if (!controlsLocked) {
      clearModes();
      currentMode = MODE_IDLE;
      carStatus = "SYSTEM STANDBY";
      setNewMasterAction(ACTION_ALL_OFF);
    }
  }

  // ⚡ THE ANTI-LAG FIX: Triggers exactly 600ms after you finish moving the door!
  if (stableState == HIGH && openCount > 0 && millis() - lastCloseTime > 600) {
    int count = openCount;
    openCount = 0;
    if (!controlsLocked) {
      if (count == 1) executeAmbientToggle();
      else if (count == 2) executeHighwayRun();
      else if (count == 3) executeJukeboxMode();
      else if (count == 4) executeCarMode();
      else if (count >= 5) executeMarioMode();
    }
  }
}

void startCaptivePortal() {
  inSetupMode = true;
  ignoreNetwork = true; // Stop PingTask from running
  
  // 🚨 Dramatic Visual Feedback to let you know 30 seconds is up!
  setAllHead(0); setUnder(0); setAllBrake(255);
  playToneDur(3000, 1500); 
  smartDelay(1500);
  setAllBrake(0);

  // Turn off Station mode and broadcast our Open Network
  WiFi.mode(WIFI_AP);
  WiFi.softAP("PORSCHE-SETUP"); // No password required
  
  // Start the DNS Server to hijack all phone traffic
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  // Overwrite the Web Server to ONLY show the Network Setup page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", networkHtml);
  });

  // 🪄 THE CAPTIVE PORTAL MAGIC: Redirect all unknown traffic to the Setup Page
  server.onNotFound([](AsyncWebServerRequest *request) {
    request->redirect("http://" + WiFi.softAPIP().toString());
  });

  carStatus = "SETUP MODE";
}

// =======================================================================
// SETUP AND BOOT
// =======================================================================
void setup() {
  prefs.begin("car-config", false);
  kvmAutoReset = prefs.getBool("kvmReset", true);
  globalSoundEnabled = prefs.getBool("sound", true);
  reminderEnabled = prefs.getBool("reminder", true);
  isUsbState2 = prefs.getBool("usb", false);

  pinMode(BONNET_PIN, INPUT_PULLUP);
  pinMode(LEFT_PIN, INPUT_PULLUP);
  pinMode(RIGHT_PIN, INPUT_PULLUP);
  pinMode(L_HEAD_PIN, OUTPUT);
  pinMode(R_HEAD_PIN, OUTPUT);
  pinMode(L_BRAKE_PIN, OUTPUT);
  pinMode(R_BRAKE_PIN, OUTPUT);
  pinMode(UNDERBODY_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  // Explicitly drive hardware triggers LOW instantly to prevent floating pulses at boot
  pinMode(PC_PIN, OUTPUT);
  digitalWrite(PC_PIN, LOW);
  pinMode(USB_PIN, OUTPUT);
  digitalWrite(USB_PIN, LOW);

  setHead(255, 0);
  setBrake(255, 0);
  playToneDur(2000, 100);
  delay(150);
  setHead(0, 255);
  setBrake(0, 255);
  playToneDur(2500, 100);
  delay(150);
  setAllHead(0);
  setAllBrake(0);
  delay(500);

  // --- LOAD NETWORK SETTINGS FROM MEMORY ---
  ui_user = prefs.getString("ui_user", "admin"); 
  ui_pass = prefs.getString("ui_pass", "admin"); 
  net_ssid = prefs.getString("ssid", "SHAKUNI");
  net_pass = prefs.getString("pass", "Oct221995");
  ip_gateway = prefs.getString("ip_gate", "192.168.1.1");
  ip_esp = prefs.getString("ip_esp", "192.168.1.17");
  ip_desk_lan = prefs.getString("ip_d_lan", "192.168.1.9");
  ip_desk_wifi = prefs.getString("ip_d_wifi", "192.168.1.20");
  ip_lap_lan = prefs.getString("ip_l_lan", "192.168.1.19");
  ip_lap_wifi = prefs.getString("ip_l_wifi", "192.168.1.18");

  // Apply IPs to system arrays
  gateway.fromString(ip_gateway);
  local_IP.fromString(ip_esp);
  targetIPs[0].fromString(ip_desk_lan);
  targetIPs[1].fromString(ip_desk_wifi);
  targetIPs[2].fromString(ip_lap_lan);
  targetIPs[3].fromString(ip_lap_wifi);
  // -----------------------------------------

  // ... (Your PinModes stay exactly the same here) ...

  // Connect using Dynamic Memory
  WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS);
  WiFi.setAutoReconnect(true);
  WiFi.begin(net_ssid.c_str(), net_pass.c_str());
  configTime(19800, 0, "pool.ntp.org", "time.google.com");
  WiFi.setSleep(false);
  MDNS.begin("porsche");
  ArduinoOTA.setHostname("PORSCHE_GT3_RS");
  ArduinoOTA.begin();

  // 1. The Main Dashboard Route (Checks for 1-Year Cookie OR Setup Mode)
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    // 🚨 THE VALET BACKDOOR: If 30-second reset is triggered, bypass security immediately!
    if (inSetupMode) {
      request->send(200, "text/html", networkHtml);
      return;
    }
    
    if (request->hasHeader("Cookie") && request->header("Cookie").indexOf("session=porsche_auth") != -1) {
      request->send(200, "text/html", html); // Cookie found! Let them in instantly.
    } else {
      request->send(200, "text/html", loginHtml); // No cookie? Show the custom login screen.
    }
  });

  // 2. The Login Processing Route (Sets the 1-Year Cookie)
  server.on("/login", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("u") && request->hasParam("p")) {
      if (request->getParam("u")->value() == ui_user && request->getParam("p")->value() == ui_pass) {
        AsyncWebServerResponse *response = request->beginResponse(302, "text/plain", "");
        response->addHeader("Location", "/");
        response->addHeader("Set-Cookie", "session=porsche_auth; Max-Age=31536000; Path=/"); // Remember for 1 Full Year!
        request->send(response);
        return;
      }
    }
    request->send(401, "text/plain", "ACCESS DENIED. Go back and try again.");
  });
  server.on("/syncUsb", HTTP_GET, [](AsyncWebServerRequest *request) {
    isUsbState2 = !isUsbState2;
    prefs.putBool("usb", isUsbState2);
    
    // Route it safely into the main loop!
    setNewMasterAction(ACTION_SILENT_USB_SYNC); 
    
    request->send(200, "text/plain", "OK");
  });
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", carStatus);
  });
  server.on("/pcStatus", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{\"pc\":";
    json += isDesktopOnline ? "true" : "false";
    json += ",\"lap\":";
    json += isLaptopOnline ? "true" : "false";
    json += "}";
    request->send(200, "application/json", json);
  });
  server.on("/audioStatus", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", globalSoundEnabled ? "ON" : "MUTED");
  });
  
  server.on("/timer", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!reminderEnabled) {
      request->send(200, "text/plain", "MUTED");
      return;
    }
    time_t now;
    time(&now);
    struct tm *timeinfo = localtime(&now);
    if (timeinfo && timeinfo->tm_year > 100) {
      char str[10];
      sprintf(str, "%02d:%02d", 59 - timeinfo->tm_min, 59 - timeinfo->tm_sec);
      request->send(200, "text/plain", String(str));
    } else request->send(200, "text/plain", "SYNC...");
  });
  server.on("/lockStatus", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", controlsLocked ? "LOCKED" : "UNLOCKED");
  });
  server.on("/setBright", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("v")) {
      maxHeadBright = request->getParam("v")->value().toInt();
      if (headlightState && currentMode == MODE_ON && !highwayRunState && !jukeboxModeState) setAllHead(255);
      tempStatus = "INTENSITY: " + String(map(maxHeadBright, 0, 255, 0, 100)) + "%";
      tempStatusTime = millis();
    }
    request->send(200, "text/plain", "OK");
  });

  server.on("/on", HTTP_GET, [](AsyncWebServerRequest *request) {
    executeIgnition();
    request->send(200, "text/plain", "OK");
  });
  server.on("/off", HTTP_GET, [](AsyncWebServerRequest *request) {
    executeShutdown();
    request->send(200, "text/plain", "OK");
  });
  server.on("/softoff", HTTP_GET, [](AsyncWebServerRequest *request) {
    executeSoftShutdown();
    request->send(200, "text/plain", "OK");
  });
  server.on("/usb", HTTP_GET, [](AsyncWebServerRequest *request) {
    executeUSB();
    request->send(200, "text/plain", "OK");
  });
  server.on("/highway", HTTP_GET, [](AsyncWebServerRequest *request) {
    executeHighwayRun();
    request->send(200, "text/plain", "OK");
  });
  server.on("/jukebox", HTTP_GET, [](AsyncWebServerRequest *request) {
    executeJukeboxMode();
    request->send(200, "text/plain", "OK");
  });
  server.on("/ambient", HTTP_GET, [](AsyncWebServerRequest *request) {
    executeAmbientToggle();
    request->send(200, "text/plain", "OK");
  });
  server.on("/headToggle", HTTP_GET, [](AsyncWebServerRequest *request) {
    executeHeadToggle();
    request->send(200, "text/plain", "OK");
  });
  server.on("/brakeToggle", HTTP_GET, [](AsyncWebServerRequest *request) {
    executeBrakeToggle();
    request->send(200, "text/plain", "OK");
  });
  server.on("/underToggle", HTTP_GET, [](AsyncWebServerRequest *request) {
    executeUnderToggle();
    request->send(200, "text/plain", "OK");
  });
  server.on("/soundToggle", HTTP_GET, [](AsyncWebServerRequest *request) {
    executeSoundToggle();
    request->send(200, "text/plain", "OK");
  });
  server.on("/masterToggle", HTTP_GET, [](AsyncWebServerRequest *request) {
    executeMasterToggle();
    request->send(200, "text/plain", "OK");
  });
  server.on("/restart", HTTP_GET, [](AsyncWebServerRequest *request) {
    executeRestart();
    request->send(200, "text/plain", "OK");
  });
  server.on("/reminderToggle", HTTP_GET, [](AsyncWebServerRequest *request) {
    executeReminderToggle();
    request->send(200, "text/plain", "OK");
  });
  server.on("/lockToggle", HTTP_GET, [](AsyncWebServerRequest *request) {
    executeLockToggle();
    request->send(200, "text/plain", "OK");
  });
  server.on("/carmode", HTTP_GET, [](AsyncWebServerRequest *request) {
    executeCarMode();
    request->send(200, "text/plain", "OK");
  });
  server.on("/mario", HTTP_GET, [](AsyncWebServerRequest *request) {
    executeMarioMode();
    request->send(200, "text/plain", "OK");
  });
  server.on("/overload", HTTP_GET, [](AsyncWebServerRequest *request) {
    pcOverloadState = true;
    cancelEffect = true;
    request->send(200, "text/plain", "OVERLOAD ON");
  });
  server.on("/normal", HTTP_GET, [](AsyncWebServerRequest *request) {
    pcOverloadState = false;
    cancelEffect = true;
    request->send(200, "text/plain", "NORMAL");
  });
  server.on("/kvmResetToggle", HTTP_GET, [](AsyncWebServerRequest *request) {
    kvmAutoReset = !kvmAutoReset;
    prefs.putBool("kvmReset", kvmAutoReset);
    tempStatus = kvmAutoReset ? "AUTO-RESET: ON" : "AUTO-RESET: OFF";
    tempStatusTime = millis();
    request->send(200, "text/plain", "OK");
  });
  server.on("/kvmStatus", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", kvmAutoReset ? "AUTO-RESET: ON" : "AUTO-RESET: OFF");
  });
  server.on("/network", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasHeader("Cookie") && request->header("Cookie").indexOf("session=porsche_auth") != -1) {
      request->send(200, "text/html", networkHtml);
    } else {
      request->redirect("/"); // Kick them back to the login screen!
    }
  });

  server.on("/getNetwork", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"ui_user\":\"" + ui_user + "\",";
    json += "\"ui_pass\":\"" + ui_pass + "\",";
    json += "\"ssid\":\"" + net_ssid + "\",";
    json += "\"pass\":\"" + net_pass + "\",";
    json += "\"ip_gateway\":\"" + ip_gateway + "\",";
    json += "\"ip_esp\":\"" + ip_esp + "\",";
    json += "\"ip_d_lan\":\"" + ip_desk_lan + "\",";
    json += "\"ip_d_wifi\":\"" + ip_desk_wifi + "\",";
    json += "\"ip_l_lan\":\"" + ip_lap_lan + "\",";
    json += "\"ip_l_wifi\":\"" + ip_lap_wifi + "\"";
    json += "}";
    request->send(200, "application/json", json);
  });

  server.on("/saveNetwork", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(request->hasParam("ui_u")) prefs.putString("ui_user", request->getParam("ui_u")->value());
    if(request->hasParam("ui_p")) prefs.putString("ui_pass", request->getParam("ui_p")->value());
    if(request->hasParam("ssid")) prefs.putString("ssid", request->getParam("ssid")->value());
    if(request->hasParam("pass")) prefs.putString("pass", request->getParam("pass")->value());
    if(request->hasParam("ip_gateway")) prefs.putString("ip_gate", request->getParam("ip_gateway")->value());
    if(request->hasParam("ip_esp")) prefs.putString("ip_esp", request->getParam("ip_esp")->value());
    if(request->hasParam("ip_d_lan")) prefs.putString("ip_d_lan", request->getParam("ip_d_lan")->value());
    if(request->hasParam("ip_d_wifi")) prefs.putString("ip_d_wifi", request->getParam("ip_d_wifi")->value());
    if(request->hasParam("ip_l_lan")) prefs.putString("ip_l_lan", request->getParam("ip_l_lan")->value());
    if(request->hasParam("ip_l_wifi")) prefs.putString("ip_l_wifi", request->getParam("ip_l_wifi")->value());

    request->send(200, "text/plain", "SAVED");
    delay(500);
    ESP.restart(); 
  });

  // SHUTDOWN TIMER ROUTES
  server.on("/startSleep", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("m")) {
      unsigned long mins = request->getParam("m")->value().toInt();
      sleepDurationMs = mins * 60000;
      sleepStartTime = millis();
      sleepTimerActive = true;
      sleepTimerPaused = false;
      uiTick();
      tempStatus = "TIMER STARTED";
      tempStatusTime = millis();
    }
    request->send(200, "text/plain", "OK");
  });
  server.on("/pauseSleep", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (sleepTimerActive) {
      if (!sleepTimerPaused) {
        unsigned long passed = millis() - sleepStartTime;
        if (sleepDurationMs > passed) sleepDurationMs -= passed;
        else sleepDurationMs = 0;
        sleepTimerPaused = true;
        tempStatus = "TIMER PAUSED";
      } else {
        sleepStartTime = millis();
        sleepTimerPaused = false;
        tempStatus = "TIMER RESUMED";
      }
      tempStatusTime = millis();
      uiTick();
    }
    request->send(200, "text/plain", "OK");
  });
  server.on("/cancelSleep", HTTP_GET, [](AsyncWebServerRequest *request) {
    sleepTimerActive = false;
    sleepTimerPaused = false;
    uiTick();
    tempStatus = "TIMER CANCELED";
    tempStatusTime = millis();
    request->send(200, "text/plain", "OK");
  });
  server.on("/sleepStatus", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!sleepTimerActive) {
      request->send(200, "text/plain", "OFF");
      return;
    }
    if (sleepTimerPaused) {
      request->send(200, "text/plain", "PAUSED");
      return;
    }
    unsigned long passed = millis() - sleepStartTime;
    if (passed >= sleepDurationMs) {
      request->send(200, "text/plain", "00:00:00");
      return;
    }
    unsigned long remainingSecs = (sleepDurationMs - passed) / 1000;
    unsigned long h = remainingSecs / 3600;
    unsigned long m = (remainingSecs % 3600) / 60;
    unsigned long s = remainingSecs % 60;
    char str[16];
    if (h > 0) sprintf(str, "%02lu:%02lu:%02lu", h, m, s);
    else sprintf(str, "%02lu:%02lu", m, s);
    request->send(200, "text/plain", String(str));
  });
server.addHandler(&ws);
  server.begin();

  // Create Ping Task on Core 0 (Xiao C3 is single core)
  xTaskCreatePinnedToCore(PingTask, "PingTask", 4096, NULL, 1, &PingTaskHandle, 0);
}

void restoreLights() {
  if (headlightState) setAllHead(255);
  else setAllHead(0);
  if (brakeState) setAllBrake(255);
  else setAllBrake(0);
  if (underbodyState) setUnder(255);
  else setUnder(0);
}

void playLockAnimation() {
  setAllHead(0);
  setAllBrake(0);
  setUnder(0);
  playToneDur(300, 150);
  setAllBrake(255);
  smartDelay(150);
  setAllBrake(0);
  smartDelay(150);
  setAllBrake(255);
  smartDelay(150);
  setAllBrake(0);
  restoreLights();
}

void playUnlockAnimation() {
  setAllHead(0);
  setAllBrake(0);
  setUnder(0);
  playToneDur(2000, 150);
  setAllHead(255);
  setUnder(255);
  smartDelay(150);
  setAllHead(0);
  setUnder(0);
  smartDelay(150);
  setAllHead(255);
  setUnder(255);
  smartDelay(150);
  setAllHead(0);
  setUnder(0);
  restoreLights();
}

void checkAutoLock() {
  // If the PC is on, keep the timer reset so it doesn't lock while you're using it
  if (lastPcOnline) lastActivityTime = millis();
  
  // If the car is unlocked and has been idle for 30 seconds...
  if (!controlsLocked && millis() - lastActivityTime > 30000) {
    controlsLocked = true; // 🔒 Set the lock state first
    
    // 🏎️ THE FIX: Route through the Master Switch!
    // This tells the loop to lift the shields and play the animation properly.
    setNewMasterAction(ACTION_LOCK_TOGGLE); 
  }
}

void notifyClients() {
  String json = "{";
  // 🪄 OVERRIDES THE UI WITH TEMP STATUS IF ONE IS ACTIVE
  json += "\"status\":\"" + String(tempStatus != "" ? tempStatus : carStatus) + "\",";
  json += "\"pc\":" + String(isDesktopOnline ? "true" : "false") + ",";
  json += "\"lap\":" + String(isLaptopOnline ? "true" : "false") + ",";
  json += "\"audio\":\"" + String(globalSoundEnabled ? "ON" : "MUTED") + "\",";
  
  String timerStr = "MUTED";
  if (reminderEnabled) {
      time_t now; time(&now); struct tm *ti = localtime(&now);
      if (ti && ti->tm_year > 100) {
          char buf[10]; sprintf(buf, "%02d:%02d", 59 - ti->tm_min, 59 - ti->tm_sec);
          timerStr = String(buf);
      } else timerStr = "SYNC...";
  }
  json += "\"timer\":\"" + timerStr + "\",";
  json += "\"lock\":\"" + String(controlsLocked ? "LOCKED" : "UNLOCKED") + "\",";
  
  String sleepStr = "OFF";
  float sleepPct = 0.0;
  if (sleepTimerActive) {
      if (sleepTimerPaused) {
          sleepStr = "PAUSED";
          unsigned long passed = millis() - sleepStartTime;
          sleepPct = ((float)(sleepDurationMs - passed) / (float)sleepDurationMs) * 100.0;
      }
      else {
          unsigned long passed = millis() - sleepStartTime;
          if (passed >= sleepDurationMs) {
              sleepStr = "00:00:00";
              sleepPct = 0.0;
          } else {
              unsigned long rem = (sleepDurationMs - passed) / 1000;
              char buf[16];
              if (rem / 3600 > 0) sprintf(buf, "%02lu:%02lu:%02lu", rem/3600, (rem%3600)/60, rem%60);
              else sprintf(buf, "%02lu:%02lu", (rem%3600)/60, rem%60);
              sleepStr = String(buf);
              sleepPct = ((float)(sleepDurationMs - passed) / (float)sleepDurationMs) * 100.0;
          }
      }
  }
  json += "\"sleep\":\"" + sleepStr + "\",";
  json += "\"sleepPct\":" + String(sleepPct) + ",";

  // Calculate 8-Hour Master Percent
  float masterPct = 0.0;
  if (currentMode != MODE_IDLE && globalAwakeStartTime > 0) {
      masterPct = ((float)(millis() - globalAwakeStartTime) / 28800000.0) * 100.0;
  }
  json += "\"masterPct\":" + String(masterPct) + ",";

  // Calculate Hourly Chime Percent
  float chimePct = 0.0;
  if (reminderEnabled) {
      time_t now; time(&now); struct tm *ti = localtime(&now);
      if (ti && ti->tm_year > 100) {
          float currentSeconds = (ti->tm_min * 60) + ti->tm_sec;
          chimePct = (currentSeconds / 3600.0) * 100.0;
      }
  }
  json += "\"chimePct\":" + String(chimePct) + ",";

  json += "\"kvm\":\"" + String(kvmAutoReset ? "AUTO-RESET: ON" : "AUTO-RESET: OFF") + "\"";
  json += "}";
  ws.textAll(json);
}

void loop() {
  ArduinoOTA.handle(); checkInterrupt(); checkDailyReset(); checkHourlyReminder(); checkAutoLock();
  
  // 🪄 ERASES THE TEMPORARY TEXT AFTER 2 SECONDS
  if (tempStatus != "" && millis() - tempStatusTime > 2000) {
      tempStatus = "";
  }
  
  wsTick(); // <--- CLEAN REPLACEMENT

  // 🪄 Process DNS Hijacking if in Setup Mode
  if (inSetupMode) dnsServer.processNextRequest();
  
  if (pendingAction != ACTION_NONE) {
    Action act = pendingAction; pendingAction = ACTION_NONE; cancelEffect = false; 
    switch(act) {
        case ACTION_IGNITION: {
            ignoreNetwork = true;
            headlightState = true; brakeState = true; underbodyState = true; // 🔓 Lift Shields
            triggerPC(); ignitionEffect(); 
            smartDelay(2000); // ⏱️ Live Status Hold
            carStatus = "SYSTEM IDLE"; 
            ignoreNetwork = false;
            break;
        }
            
        case ACTION_USB_EFFECT: {
            ignoreNetwork = true;
            headlightState = true; brakeState = true; underbodyState = true; // 🔓 Lift Shields
            
            // 1. Force the UI to show the correct state immediately
            carStatus = isUsbState2 ? "USB 2" : "USB 1"; 
            
            usbTriggerEffect(); 
            
            // 2. THE REAL SPAM FIX: Only reset the text if the 2-second hold IS NOT interrupted!
            if (!cancelEffect) {
                if (!smartDelay(2000)) { 
                    carStatus = (currentMode == MODE_IDLE) ? "SYSTEM STANDBY" : "SYSTEM IDLE"; 
                }
            }
            
            restoreLights(); // 🔒 Restore Shield Reality
            ignoreNetwork = false;
            break;
        }
        
        case ACTION_SILENT_USB_SYNC: {
            // 1. Show the text immediately
            carStatus = isUsbState2 ? "USB 2" : "USB 1"; 
            
            // 2. Hold it on the screen for 2 seconds
            smartDelay(2000); 
            
            // 3. Clear it back to normal (unless you spammed another button!)
            if (!cancelEffect) {
                carStatus = (currentMode == MODE_IDLE) ? "SYSTEM STANDBY" : "SYSTEM IDLE"; 
            }
            break;
        }
            
        case ACTION_SHUTDOWN: {
            ignoreNetwork = true;
            headlightState = true; brakeState = true; underbodyState = true; // 🔓 Lift Shields
            
            // 🚨 Fixes the 8-Hour Auto-Sleep Timer Ghost State
            clearModes(); 
            currentMode = MODE_IDLE; 

            setAllHead(0); setUnder(0); setAllBrake(255); smartDelay(300); 
            
            // 🚨 Fixes the Phantom Boot: Only pushes the button if the PC is actually awake!
            if (isDesktopOnline) {
                forcePCOff(); 
            }
            
            smartDelay(2000); 
            carStatus = "SYSTEM STANDBY"; 
            headlightState = false; brakeState = false; underbodyState = false; // 🔒 Lock Shields
            allOff();
            ignoreNetwork = false;
            break;
        }
        
        case ACTION_SOFT_SHUTDOWN: { 
            ignoreNetwork = true;
            headlightState = true; brakeState = true; underbodyState = true; // 🔓 Lift Shields
            setAllHead(0); setUnder(0); setAllBrake(255); delay(300); 
            restoreLights();
            
            // 1. Initial Pulse to trigger Windows shutdown
            digitalWrite(PC_PIN, HIGH); delay(500); digitalWrite(PC_PIN, LOW);
            delay(15000); 
            
            // 2. The Verification
            if (lastPcOnline) {
                WiFiClient client;
                bool isStillAlive = false;
                if (client.connect(targetIPs[0], 135, 300)) { isStillAlive = true; client.stop(); }
                else if (client.connect(targetIPs[1], 135, 300)) { isStillAlive = true; client.stop(); }
                
                // 3. ONLY pulse again if it stubbornly refused to shut down!
                if (isStillAlive) {
                    digitalWrite(PC_PIN, HIGH); delay(500); digitalWrite(PC_PIN, LOW);
                }
            }
            // 🚨 "Else" trap removed here!
            
            carStatus = "SYSTEM IDLE"; 
            ignoreNetwork = false;
            break;
        }
        
        case ACTION_RESTART: {
            ignoreNetwork = true;
            headlightState = true; brakeState = true; underbodyState = true; // 🔓 Lift Shields
            restartEffect(); 
            smartDelay(2000); 
            carStatus = "SYSTEM IDLE"; 
            ignoreNetwork = false;
            break;
        }
            
        case ACTION_LOCK_TOGGLE: {
            ignoreNetwork = true;
            // 🛑 SAVE PREVIOUS REALITY
            bool oldHead = headlightState; bool oldBrake = brakeState; bool oldUnder = underbodyState;
            String previousStatus = carStatus;

            // 🔓 LIFT SHIELDS FOR ANIMATION
            headlightState = true; brakeState = true; underbodyState = true;
            carStatus = controlsLocked ? "SYSTEM LOCKED" : "SYSTEM UNLOCKED"; 
            
            if (controlsLocked) playLockAnimation(); else playUnlockAnimation();
            
            smartDelay(2000); // ⏱️ Hold Lock Status for 2 seconds

            // 🔒 RESTORE PREVIOUS REALITY
            headlightState = oldHead; brakeState = oldBrake; underbodyState = oldUnder;
            carStatus = previousStatus;
            restoreLights(); 
            ignoreNetwork = false;
            break;
        }

        case ACTION_ALL_OFF: 
            headlightState = false; brakeState = false; underbodyState = false; 
            allOff(); 
            carStatus = "SYSTEM STANDBY"; 
            break;
        
        case ACTION_AUTO_WAKE: {
            ignoreNetwork = true;
            // 🔓 Lift Shields for the startup show
            headlightState = true; brakeState = true; underbodyState = true; 
            
            clearModes(); 
            currentMode = MODE_ON; 
            controlsLocked = false;
            prefs.putBool("usb", isUsbState2); 
            
            ignitionEffect(); 
            
            smartDelay(2000); // ⏱️ Show "SYSTEM IDLE" on UI
            carStatus = "SYSTEM IDLE"; 
            ignoreNetwork = false;
            break;
        }
            
        case ACTION_AUTO_SHUTDOWN: {
            ignoreNetwork = true;
            // 🔓 Lift Shields so the fade-out is visible
            headlightState = true; brakeState = true; underbodyState = true;
            
            clearModes(); 
            currentMode = MODE_IDLE; 
            carStatus = "SYSTEM STANDBY"; 
            
            shutdownEffect(); 
            
            // 🔒 Now Lock the Shields for Standby
            headlightState = false; brakeState = false; underbodyState = false;
            allOff();
            ignoreNetwork = false;
            break;
        }
            
        case ACTION_NONE: break;
    }
  } 
  // THE SCREAMING ENGINE FIX: The Ultimate Kill-Switch
  else if (cancelEffect) { 
      stopTone();     
      restoreLights(); 
      cancelEffect = false; 
  }

  if (pcOverloadState) runOverloadAnimation(); 
  else if (jukeboxModeState) runJukeboxMode(); 
  else if (carModeState) runCarMode(); 
  else if (marioModeState) runMarioMode(); 
  else if (highwayRunState) runHighwayRunMode(); 
  else if (currentMode == MODE_AMBIENT) runAmbient(); 
  else if (currentMode == MODE_ON && pendingAction == ACTION_NONE && !cancelEffect) runIdleAnimation(); 
  
  yield(); // Keep system watchdog happy
}

// =======================================================================
// CORE ANIMATIONS & HARDWARE TRIGGERS
// =======================================================================
void usbTriggerEffect() {
  headlightState = true; brakeState = true; underbodyState = true; // FORCE ON FOR ANIMATION
  triggerUSB();
  
  // --- THE NEW SMART MEMORY DECK FOR USB SWITCH ---
  static int usbDeck[3];
  static int usbIndex = 3; // Forces shuffle on first boot

  if (usbIndex >= 3) {
    for (int i = 0; i < 3; i++) usbDeck[i] = i;
    for (int i = 2; i > 0; i--) {
      int j = random(0, i + 1);
      int temp = usbDeck[i]; usbDeck[i] = usbDeck[j]; usbDeck[j] = temp;
    }
    usbIndex = 0;
  }
  int style = usbDeck[usbIndex++];
  // ----------------------------------------------

  if (style == 0) {
    for (int i = 0; i < 3; i++) {
      setAllHead(255);
      setAllBrake(255);
      setUnder(150);
      for (int freq = 2500; freq > 1000; freq -= 150) {
        playTone(freq);
        if (smartDelay(10)) {
          stopTone();
          return;
        }
      }
      stopTone();
      setAllHead(0);
      setAllBrake(0);
      setUnder(0);
      if (smartDelay(100)) return;
    }
  } else if (style == 1) {
    for (int i = 0; i < 2; i++) {
      setHead(255, 0);
      playTone(2000);
      smartDelay(60);
      stopTone();
      setHead(0, 255);
      playTone(2500);
      smartDelay(60);
      stopTone();
    }
    setAllHead(255);
    setAllBrake(255);
    setUnder(255);
    playTone(3000);
    smartDelay(100);
    stopTone();
  } else {
    for (int i = 0; i < 255; i += 10) {
      setUnder(i);
      playTone(1000 + i * 5);
      if (smartDelay(15)) {
        stopTone();
        return;
      }
    }
    setAllHead(255);
    setAllBrake(255);
    stopTone();
  }
  for (int i = 0; i < 255; i += 3) {
    setAllHead(i);
    setAllBrake(i);
    setUnder(i);
    if (smartDelay(15)) return;
  }
  headlightState = true;
  brakeState = true;
  underbodyState = true;
}

void ignitionEffect() {
  headlightState = true; brakeState = true; underbodyState = true; // FORCE ON FOR ANIMATION
  
  // --- THE ULTIMATE SMART MEMORY DECK (20 EFFECTS) ---
  static int ignitionDeck[20];
  static int ignitionIndex = 20; // Forces shuffle on first boot

  if (ignitionIndex >= 20) {
    for (int i = 0; i < 20; i++) ignitionDeck[i] = i;
    for (int i = 19; i > 0; i--) {
      int j = random(0, i + 1);
      int temp = ignitionDeck[i]; ignitionDeck[i] = ignitionDeck[j]; ignitionDeck[j] = temp;
    }
    ignitionIndex = 0;
  }
  int style = ignitionDeck[ignitionIndex++];
  // ---------------------------------------------------

  if (style == 0) {
    // 0: The Classic Fade & Chirp
    for (int i = 0; i < 255; i += 5) { setUnder(i); if (smartDelay(10)) return; }
    setBrake(255, 0); playTone(1800); smartDelay(80); stopTone(); smartDelay(50);
    setBrake(255, 255); playTone(2200); smartDelay(150); stopTone();
    for (int i = 0; i < 255; i += 4) { setHead(i, i); if (smartDelay(8)) return; }
    
  } else if (style == 1) {
    // 1: Electrical Spark / Glitch Boot
    playTone(3000);
    for (int i = 0; i < 5; i++) {
      setAllHead(255); setAllBrake(255); setUnder(255); smartDelay(random(10, 50));
      setAllHead(0); setAllBrake(0); setUnder(0); smartDelay(random(10, 50));
    }
    stopTone();
    for (int i = 0; i < 255; i += 2) { setAllHead(i); setAllBrake(i); setUnder(i); if (smartDelay(5)) return; }
    
  } else if (style == 2) {
    // 2: The Power Ramp
    for (int f = 200; f < 1000; f += 10) {
      int bright = map(f, 200, 1000, 0, 255);
      setAllHead(bright); setAllBrake(bright); setUnder(bright); playTone(f);
      if (smartDelay(15)) { stopTone(); return; }
    }
    stopTone();
    
  } else if (style == 3) {
    // 3: The Jet Turbine Spin-Up
    setAllHead(0); setAllBrake(0);
    for (int f = 300; f < 2500; f += 25) {
      setUnder(map(f, 300, 2500, 0, 255)); playTone(f);
      if (smartDelay(10)) { stopTone(); return; }
    }
    stopTone(); setAllHead(255); setAllBrake(255); playTone(3000); smartDelay(150); stopTone();
    
  } else if (style == 4) {
    // 4: The System Diagnostic Check
    setAllHead(0); setAllBrake(0); setUnder(0);
    setHead(255, 0); playTone(800); if(smartDelay(150)) return; stopTone(); if(smartDelay(100)) return;
    setHead(255, 255); playTone(1000); if(smartDelay(150)) return; stopTone(); if(smartDelay(100)) return;
    setBrake(255, 0); playTone(1200); if(smartDelay(150)) return; stopTone(); if(smartDelay(100)) return;
    setBrake(255, 255); playTone(1400); if(smartDelay(150)) return; stopTone(); if(smartDelay(100)) return;
    setUnder(255); playTone(1600); if(smartDelay(150)) return; stopTone(); if(smartDelay(100)) return;
    setAllHead(0); setAllBrake(0); setUnder(0); if(smartDelay(200)) return;
    setAllHead(255); setAllBrake(255); setUnder(255); playTone(2500); smartDelay(200); stopTone();
    
  } else if (style == 5) {
    // 5: The Broken Neon Tube
    for(int i = 0; i < 6; i++) {
      int onTime = random(10, 60); int offTime = random(20, 150);
      setAllHead(255); setUnder(100); setAllBrake(50); playTone(120);
      if(smartDelay(onTime)) { stopTone(); return; }
      setAllHead(0); setUnder(0); setAllBrake(0); stopTone(); if(smartDelay(offTime)) return;
    }
    setAllHead(255); setAllBrake(255); setUnder(255); playTone(2500); smartDelay(150); stopTone();
    
  } else if (style == 6) {
    // 6: The Warp Drive
    for(int delayTime = 120; delayTime > 10; delayTime -= 15) {
      setAllHead(255); setAllBrake(0); setUnder(0); playTone(2000); if(smartDelay(delayTime)) { stopTone(); return; }
      setAllHead(0); setAllBrake(0); setUnder(255); playTone(1500); if(smartDelay(delayTime)) { stopTone(); return; }
      setAllHead(0); setAllBrake(255); setUnder(0); playTone(1000); if(smartDelay(delayTime)) { stopTone(); return; }
    }
    setAllHead(255); setAllBrake(255); setUnder(255); playTone(3500); smartDelay(300); stopTone();
    
  } else if (style == 7) {
    // 7: The Interceptor Boot
    for(int i = 0; i < 4; i++) {
      setHead(255, 0); setBrake(255, 0); setUnder(255); playTone(1200); if(smartDelay(80)) { stopTone(); return; }
      setHead(0, 255); setBrake(0, 255); setUnder(0); playTone(1600); if(smartDelay(80)) { stopTone(); return; }
    }
    stopTone();
    
  } else if (style == 8) {
    // 8: V8 Cold Start Sputter
    for(int i=0; i<4; i++) {
        setAllBrake(255); playTone(100); if(smartDelay(50)) return;
        setAllBrake(0); stopTone(); if(smartDelay(120 - (i*25))) return;
    }
    setAllHead(255); setAllBrake(255); setUnder(255); playTone(400); smartDelay(200); stopTone();

  } else if (style == 9) {
    // 9: The Heartbeat
    for(int i=0; i<3; i++) {
        setAllHead(255); setUnder(100); playTone(150); if(smartDelay(80)) return;
        setAllHead(0); setUnder(0); stopTone(); if(smartDelay(80)) return;
        setAllHead(255); setUnder(100); playTone(150); if(smartDelay(80)) return;
        setAllHead(0); setUnder(0); stopTone(); if(smartDelay(400)) return;
    }
    
  } else if (style == 10) {
    // 10: Cyberpunk Scan
    for(int i=0; i<255; i+=10) { setUnder(i); playTone(1000+i*2); if(smartDelay(15)) { stopTone(); return; } }
    setAllBrake(255); playTone(2500); if(smartDelay(100)) return;
    setAllHead(255); playTone(3000); if(smartDelay(200)) return; stopTone();
    
  } else if (style == 11) {
    // 11: The Bass Drop
    for(int f=1000; f<3000; f+=100) { playTone(f); setAllHead(f/15); if(smartDelay(25)) { stopTone(); return; } }
    stopTone(); setAllHead(0); if(smartDelay(400)) return;
    setAllHead(255); setAllBrake(255); setUnder(255); playTone(100); if(smartDelay(600)) return; stopTone();
    
  } else if (style == 12) {
    // 12: Machine Gun Wake
    for(int i=0; i<12; i++) {
        setAllHead(255); setAllBrake(255); playTone(1200); if(smartDelay(20)) return;
        setAllHead(0); setAllBrake(0); stopTone(); if(smartDelay(20)) return;
    }
    setUnder(255);
    
  } else if (style == 13) {
    // 13: Alien Abduction (Sine Wave Frequency)
    for(int i=0; i<255; i+=2) {
        setUnder(i); setAllHead(i/2); 
        playTone(1500 + (sin(i*0.2)*500)); 
        if(smartDelay(15)) { stopTone(); return; }
    }
    stopTone();
    
  } else if (style == 14) {
    // 14: Ghost in the Machine
    setAllHead(5); setUnder(5); if(smartDelay(800)) return;
    setAllHead(20); setUnder(20); playTone(500); if(smartDelay(40)) return; stopTone(); if(smartDelay(400)) return;
    setAllHead(0); setUnder(0); if(smartDelay(200)) return;
    
  } else if (style == 15) {
    // 15: Arcade Coin-Up
    int tones[] = { 1047, 1319, 1568, 2093, 2637, 3136 };
    for(int i=0; i<6; i++) {
       setHead((i%2)*255, ((i+1)%2)*255); setBrake(((i+1)%2)*255, (i%2)*255); setUnder(i*40);
       playTone(tones[i]); if(smartDelay(60)) { stopTone(); return; }
    }
    stopTone();
    
  } else if (style == 16) {
    // 16: The Submarine Sonar
    for(int i=0; i<2; i++) {
       setAllHead(255); playTone(2500); if(smartDelay(50)) return;
       setAllHead(40); setUnder(80); playTone(200); if(smartDelay(500)) return; stopTone(); if(smartDelay(400)) return;
    }
    
  } else if (style == 17) {
    // 17: F1 Start Lights
    for(int i=1; i<=5; i++) {
       setAllBrake(i*50); playTone(800); if(smartDelay(100)) return; stopTone(); if(smartDelay(800)) return;
    }
    setAllHead(255); setAllBrake(255); setUnder(255); playTone(2000); if(smartDelay(800)) return; stopTone();
    
  } else if (style == 18) {
    // 18: Lightning Strike
    for(int i=0; i<4; i++) {
        setAllHead(255); setAllBrake(255); setUnder(255); if(smartDelay(random(10,40))) return;
        setAllHead(0); setAllBrake(0); setUnder(0); if(smartDelay(random(10,100))) return;
    }
    playTone(100); setUnder(100); setAllHead(50); if(smartDelay(800)) return; stopTone();
    
  } else {
    // 19: The Pure Crescendo
    for(int i=0; i<255; i+=1) {
        setAllHead(i); setAllBrake(i); setUnder(i); playTone(200 + i*8); 
        if(smartDelay(10)) { stopTone(); return; }
    }
    stopTone();
  }

  // ----------------------------------------------
  // GUARANTEE ALL LIGHTS ARE ON AT THE END
  setAllHead(255);
  setAllBrake(255);
  setUnder(255);
  headlightState = true;
  brakeState = true;
  underbodyState = true;

  // FIRING THE KVM USB SWAP PULSE (IF NEEDED)
  if (needsUsbSwitchPulse) {
    unsigned long startWait = millis();
    while (millis() - startWait < 1000) {
      wsTick(); // <--- FIXES UI LAG DURING KVM SYNC
      delay(1);
      yield();
    }
    triggerUSB();
  }
  needsUsbSwitchPulse = false;
}

void reminderEffect() {
  // 🛑 SAVE REALITY
  bool oldHead = headlightState; bool oldBrake = brakeState; bool oldUnder = underbodyState;
  bool oldCarMode = carModeState;

  // --- THE NEW SMART MEMORY DECK ---
  static int reminderPlaylist[80];
  static int reminderIndex = 0;
  static bool needsShuffle = true;

  if (needsShuffle) {
    for (int i = 0; i < 80; i++) reminderPlaylist[i] = i; // Fill the deck 0-79
    for (int i = 79; i > 0; i--) {                      // Fisher-Yates Shuffle
      int j = random(0, i + 1);
      int temp = reminderPlaylist[i];
      reminderPlaylist[i] = reminderPlaylist[j];
      reminderPlaylist[j] = temp;
    }
    needsShuffle = false;
    reminderIndex = 0;
  }
  
  // Pick the top card from the shuffled deck
  int currentTheme = reminderPlaylist[reminderIndex];
  
  reminderIndex++;
  if (reminderIndex > 79) needsShuffle = true; // Reshuffle after 80 hours!
  // ---------------------------------

  // 🔓 LIFT SHIELDS
  headlightState = true; brakeState = true; underbodyState = true;
  carModeState = true;  

  setAllHead(0); setAllBrake(0); setUnder(0); stopTone();

  // Signature Intro
  setAllHead(255); setUnder(255); playToneDur(2000, 100); smartDelay(150); 
  setAllHead(0); setUnder(0); smartDelay(100);
  setAllHead(255); setUnder(255); playToneDur(2000, 100); smartDelay(150); 
  setAllHead(0); setUnder(0); smartDelay(400);

  // 🚨 Play the smart memory theme instead of pure random chance
  executeTheme(currentTheme);

  // 🔒 RESTORE REALITY
  headlightState = oldHead; brakeState = oldBrake; underbodyState = oldUnder;
  carModeState = oldCarMode;  

  stopTone();
  restoreLights(); 
}

void restartEffect() {
  for (int i = 255; i >= 0; i -= 10) {
    setAllHead(i);
    setAllBrake(i);
    setUnder(i);
    if (smartDelay(10)) return;
  }
  for (int freq = 1000; freq > 300; freq -= 50) {
    playTone(freq);
    if (smartDelay(20)) {
      stopTone();
      return;
    }
  }
  stopTone();
  triggerPC();
  for (int w = 0; w < 25; w++) {
    setAllBrake(100);
    if (smartDelay(500)) return;
    setAllBrake(0);
    if (smartDelay(500)) return;
  }
  triggerPC();
  carStatus = "SYSTEM ACTIVE"; // <--- FIX: Changed from "ON"
  ignitionEffect();
}

void shutdownEffect() {
  for (int i = 255; i >= 0; i -= 5) {
    setAllHead(i);
    setAllBrake(i);
    setUnder(i);
    if (smartDelay(10)) return;
  }
  for (int freq = 1500; freq > 200; freq -= 30) {
    playTone(freq);
    if (smartDelay(15)) {
      stopTone();
      return;
    }
  }
  stopTone();
  allOff();
  
  // THE SYSTEM-WIDE FIX: Only erase memory when the car goes to sleep!
  headlightState = false;
  brakeState = false;
  underbodyState = false;
}

void allOff() {
  setAllHead(0);
  setAllBrake(0);
  setUnder(0);
  stopTone();
  // We completely removed the boolean memory wipes from here!
}
void triggerPC() {
  digitalWrite(PC_PIN, HIGH);
  smartDelay(150);
  digitalWrite(PC_PIN, LOW);
}
void forcePCOff() {
  digitalWrite(PC_PIN, HIGH);
  if (smartDelay(6000)) {
    digitalWrite(PC_PIN, LOW);
    return;
  }
  digitalWrite(PC_PIN, LOW);
}
void triggerUSB() {
  digitalWrite(USB_PIN, HIGH);
  smartDelay(150);
  digitalWrite(USB_PIN, LOW);
}

void runAmbient() {
  if (!ambientState) {
    stopTone();
    return;
  }
  stopTone();

  unsigned long currentMillis = millis();
  static unsigned long lastLightUpdate = 0;

  if (currentMillis - lastLightUpdate >= 30) {
    lastLightUpdate = currentMillis;

    // ====================================================================
    // 🌡️ 1. THE THERMAL COOLDOWN ENGINE
    // ====================================================================
    // Ambient mode acts as a "cooldown" phase when you switch to it.
    // Over the first 30 minutes (1,800,000 ms), the car simulates glowing hot brakes fading to cool.
    unsigned long elapsedAmbient = currentMillis - ambientStartTime;
    float cooldownFactor = 0.0; // 1.0 = Max heat, 0.0 = completely cool
    if (elapsedAmbient < 1800000) {
      cooldownFactor = 1.0 - ((float)elapsedAmbient / 1800000.0);
    }

    // ====================================================================
    // 🕰️ 2. CIRCADIAN RHYTHM & 🌐 DIGITAL HEARTBEAT
    // ====================================================================
    time_t now; time(&now); struct tm *ti = localtime(&now);
    int hour = (ti && ti->tm_year > 100) ? ti->tm_hour : 12; // Default to noon if no internet time
    
    float timeSpeedMod = 1.0;
    int maxEnvBright = 255;
    
    if (hour >= 0 && hour <= 6) { timeSpeedMod = 0.3; maxEnvBright = 25; }      // Deep Night: 15% brightness, ultra slow
    else if (hour >= 19 && hour <= 23) { timeSpeedMod = 0.6; maxEnvBright = 100; } // Evening: 40% brightness, relaxed
    else { timeSpeedMod = 1.0; maxEnvBright = 180; }                            // Day: Crisp and energetic
    
    // The Digital Heartbeat: If neither PC is online, the breathing speed physically halves!
    if (!lastPcOnline) { timeSpeedMod *= 0.5; }

    // ====================================================================
    // 🧪 3. DECOUPLED BIOLUMINESCENCE (Prime Number Math)
    // ====================================================================
    static float p1 = 0, p2 = 0, p3 = 0, p4 = 0, p5 = 0;
    float baseStep = 0.005 * timeSpeedMod;
    
    // Every light zone is assigned a prime number, so their phases almost never overlap!
    p1 += baseStep * 2.0;  // Left Headlight
    p2 += baseStep * 3.0;  // Right Headlight
    p3 += baseStep * 5.0;  // Left Brake
    p4 += baseStep * 7.0;  // Right Brake
    p5 += baseStep * 11.0; // Underglow
    
    // Wrap to prevent float overflow over long periods
    auto wrap = [](float &p) { if(p > 628.318) p -= 628.318; };
    wrap(p1); wrap(p2); wrap(p3); wrap(p4); wrap(p5);

    // ====================================================================
    // 🌊 4. CONSTRUCTIVE INTERFERENCE (The Ocean Tide & Golden Ratio)
    // ====================================================================
    // This overlaps a normal sine wave with a Golden Ratio wave (1.618)
    // This guarantees the swell pattern is mathematically infinite and non-looping.
    auto oceanWave = [](float p) {
       return ( (sin(p) + sin(p * 1.618)) / 2.0 + 1.0 ) / 2.0; 
    };

    float hL_wave = oceanWave(p1);
    float hR_wave = oceanWave(p2);
    float bL_wave = oceanWave(p3);
    float bR_wave = oceanWave(p4);
    float uV_wave = oceanWave(p5);

    // ====================================================================
    // 🐕‍🦺 5. REM SLEEP (The Dreaming Pet)
    // ====================================================================
    static unsigned long lastRemSleep = currentMillis;
    static bool inRemSleep = false;
    static float remPhase = 0.0;
    
    // Trigger REM sleep randomly every 15 to 30 minutes
    if (!inRemSleep && currentMillis - lastRemSleep > random(900000, 1800000)) {
        inRemSleep = true;
        remPhase = 0.0;
    }
    
    if (inRemSleep) {
        remPhase += 0.004; // Slow swell for the "eye opening" effect
        if (remPhase > 3.14) { 
            inRemSleep = false; 
            lastRemSleep = currentMillis; 
            remPhase = 0.0; 
        }
    }
    // Only boost light if dreaming AND the car isn't currently doing a Thermal Cooldown
    float remBoost = (inRemSleep && cooldownFactor < 0.2) ? (sin(remPhase) * 60.0) : 0;

    // ====================================================================
    // 🎨 FINAL CALCULATION & OVERRIDES
    // ====================================================================
    int final_hL = (int)(hL_wave * maxEnvBright);
    int final_hR = (int)(hR_wave * maxEnvBright);
    int final_bL = (int)(bL_wave * maxEnvBright);
    int final_bR = (int)(bR_wave * maxEnvBright);
    int final_uV = (int)(uV_wave * maxEnvBright);

    // Override 1: Thermal Cooldown (Kills headlights, forces red brakes to glow hot)
    if (cooldownFactor > 0.0) {
       final_hL = (int)(final_hL * (1.0 - cooldownFactor)); // Suppress Headlights
       final_hR = (int)(final_hR * (1.0 - cooldownFactor));
       
       // Calculate the "Hot Carbon" pulse
       float heatPulse = (sin(currentMillis * 0.002) + 1.0) / 2.0;
       int heatBright = 30 + (int)(225 * heatPulse * cooldownFactor);
       
       // Override the back and bottom of the car to glow red-hot
       final_bL = max(final_bL, heatBright);
       final_bR = max(final_bR, heatBright);
       final_uV = max(final_uV, heatBright);
    }

    // Override 2: REM Sleep (Cracks the left headlight open slightly)
    final_hL = min(255, final_hL + (int)remBoost);

    // Apply Math to Physical Hardware
    if (headlightState) setHead(final_hL, final_hR); else setAllHead(0);
    if (brakeState) setBrake(final_bL, final_bR); else setAllBrake(0);
    if (underbodyState) setUnder(final_uV); else setUnder(0);
  }
}

void playMusicChunk(const int *notes, const int *durations, int length, int tempo, int lightStyle) {
  for (int i = 0; i < length; i++) {
    if (!jukeboxModeState && !marioModeState) {
      stopTone();
      allOff();
      return;
    }
    int noteDuration = tempo * durations[i];
    if (notes[i] > 0) {
      if (lightStyle == 0) {
        setAllHead(255);
        setAllBrake(0);
        setUnder(100);
      } else if (lightStyle == 1) {
        setAllHead(0);
        setAllBrake(255);
        setUnder(255);
      } else {
        setHead(255, 255);
        setBrake(255, 255);
        setUnder(255);
      }
      playTone(notes[i]);
    } else {
      stopTone();
      allOff();
    }
    if (smartDelay(noteDuration)) {
      stopTone();
      allOff();
      return;
    }
    stopTone();
    allOff();
    if (smartDelay(tempo / 2)) return;
  }
}

void runJukeboxMode() {
  static bool isFreshStart = true;
  if (!jukeboxModeState) { isFreshStart = true; return; }

  static int playlist[13];
  static int trackIndex = 0;
  static bool needsShuffle = true;

  if (isFreshStart) { 
    needsShuffle = true; 
    trackIndex = 0; 
    isFreshStart = false; 
  }

// 🚨 THE JUKEBOX BRAIN: Fill and shuffle the songs
  if (needsShuffle) {
    for (int i = 0; i < 13; i++) playlist[i] = i + 1; // Fill with 1-13
    for (int i = 12; i > 0; i--) {                  // Fisher-Yates Shuffle
      int j = random(0, i + 1);
      int temp = playlist[i];
      playlist[i] = playlist[j];
      playlist[j] = temp;
    }
    needsShuffle = false;
    trackIndex = 0;
  }

  int currentSong = playlist[trackIndex];
  switch (currentSong) {
    case 1:
      {
        static const int n[] = { 494, 659, 784, 740, 659, 988, 880, 740, 659, 784, 740, 622, 698, 494, 0 };
        static const int d[] = { 2, 3, 1, 2, 4, 2, 6, 6, 3, 1, 2, 4, 2, 6, 4 };
        playMusicChunk(n, d, 15, 160, 0);
        break;
      }
    case 2:
      {
        static const int n[] = { 262, 330, 392, 494, 523, 494, 392, 330 };
        static const int d[] = { 1, 1, 1, 1, 1, 1, 1, 1 };
        for (int i = 0; i < 8; i++) playMusicChunk(n, d, 8, 140, 1);
        break;
      }
    case 3:
      {
        static const int n[] = { 392, 262, 311, 349, 392, 262, 311, 349 };
        static const int d[] = { 2, 2, 1, 1, 2, 2, 1, 1 };
        for (int i = 0; i < 3; i++) playMusicChunk(n, d, 8, 200, 1);
        break;
      }
    case 4:
      {
        static const int n[] = { 659, 587, 370, 415, 544, 494, 294, 330, 494, 440, 277, 330, 440 };
        static const int d[] = { 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2, 4 };
        playMusicChunk(n, d, 13, 120, 2);
        break;
      }
    case 5:
      {
        static const int n[] = { 440, 440, 440, 440, 659, 0, 494, 523, 494, 440, 392, 494, 440, 494, 440, 392, 440, 494, 523, 0, 494, 440, 392, 330, 880, 659, 784, 659, 0, 659, 587, 523, 440, 523, 587, 659, 698, 0, 587, 523, 494, 440 };
        static const int d[] = { 4, 2, 2, 4, 2, 2, 2, 2, 2, 2, 4, 4, 2, 2, 2, 2, 2, 2, 2, 2, 8, 2, 2, 8, 4, 4, 4, 2, 2, 4, 4, 4, 4, 4, 4, 4, 2, 2, 16, 2, 2, 2 };
        playMusicChunk(n, d, 42, 125, 1);
        break;
      }
    case 6:
      {
        static const int n[] = { 294, 294, 294, 330, 349, 349, 349, 392, 330, 330, 294, 262, 294, 0 };
        static const int d[] = { 1, 1, 2, 1, 2, 1, 2, 1, 2, 1, 1, 1, 3, 2 };
        playMusicChunk(n, d, 14, 130, 2);
        break;
      }
    case 7:
      {
        static const int n[] = { 262, 262, 220, 220, 233, 233, 0 };
        static const int d[] = { 1, 1, 1, 1, 1, 1, 4 };
        for (int i = 0; i < 4; i++) playMusicChunk(n, d, 7, 180, 2);
        break;
      }
    case 8:
      {
        static const int n[] = { 330, 330, 330, 330, 330, 330, 330, 392, 262, 294, 330, 349, 349, 349, 349, 349, 330, 330, 330, 294, 294, 330, 294, 392 };
        static const int d[] = { 1, 1, 2, 1, 1, 2, 1, 1, 1, 1, 4, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2 };
        playMusicChunk(n, d, 24, 130, 0);
        break;
      }
    case 9:
      {
        static const int n[] = { 659, 494, 523, 587, 523, 494, 440, 440, 523, 659, 587, 523, 494, 494, 523, 587, 659, 523, 440, 440, 0 };
        static const int d[] = { 2, 1, 1, 2, 1, 1, 2, 1, 1, 2, 1, 1, 2, 1, 1, 2, 2, 2, 2, 2, 4 };
        playMusicChunk(n, d, 21, 140, 2);
        break;
      }
    case 10:
      {
        static const int n[] = { 294, 392, 440, 466, 440, 392, 294, 0, 311, 349, 392 };
        static const int d[] = { 2, 2, 2, 2, 1, 1, 4, 1, 2, 2, 4 };
        playMusicChunk(n, d, 11, 160, 1);
        break;
      }
    case 11:
      {
        static const int n[] = { 0, 0, 0, 311, 330, 0, 370, 392, 0, 311, 330, 370, 392, 523, 494, 330, 392, 494, 466, 440, 392, 330, 294, 330, 0, 0, 311, 330, 0, 370, 392, 0, 311, 330, 370, 392, 523, 494, 392, 494, 659, 622, 587, 0, 0, 311, 330, 0, 370, 392, 0, 311, 330, 370, 392, 523, 494, 330, 392, 494, 466, 440, 392, 330, 294, 330, 0, 0, 659, 587, 494, 440, 392, 330, 466, 440, 466, 440, 466, 440, 466, 440, 392, 330, 294, 330, 330, 330 };
        static const int d[] = { 16, 8, 4, 4, 12, 4, 4, 12, 4, 4, 6, 4, 6, 4, 6, 4, 6, 4, 16, 3, 3, 3, 3, 16, 8, 4, 8, 12, 4, 4, 12, 4, 4, 6, 4, 6, 4, 6, 4, 6, 4, 32, 16, 8, 4, 4, 12, 4, 4, 12, 4, 4, 6, 4, 6, 4, 6, 4, 6, 4, 16, 3, 3, 3, 3, 12, 8, 8, 6, 4, 6, 4, 6, 6, 2, 6, 2, 6, 2, 6, 2, 6, 3, 3, 3, 2, 2, 16 };
        playMusicChunk(n, d, 88, 62, 2);
        break;
      }
    case 12:
      {
        static const int n[] = { 466, 466, 466, 698, 1047, 932, 880, 784, 1397, 1047, 932, 880, 784, 1397, 1047, 932, 880, 932, 784, 523, 523, 523, 698, 1047, 932, 880, 784, 1397, 1047, 932, 880, 784, 1397, 1047, 932, 880, 932, 784, 523, 523, 587, 587, 932, 880, 784, 698, 698, 784, 880, 784, 587, 659, 523, 523, 587, 587, 932, 880, 784, 698, 1047, 784, 784, 0, 523, 587, 587, 932, 880, 784, 698, 698, 784, 880, 784, 587, 659, 1047, 1047, 1397, 1245, 1109, 1047, 932, 831, 784, 698, 1047 };
        static const int d[] = { 4, 4, 4, 16, 16, 4, 4, 4, 16, 8, 4, 4, 4, 16, 8, 4, 4, 4, 16, 4, 4, 4, 16, 16, 4, 4, 4, 16, 8, 4, 4, 4, 16, 8, 4, 4, 4, 16, 6, 2, 12, 4, 4, 4, 4, 4, 4, 4, 4, 8, 4, 8, 6, 2, 12, 4, 4, 4, 4, 4, 6, 2, 16, 4, 4, 12, 4, 4, 4, 4, 4, 4, 4, 4, 8, 4, 8, 6, 2, 8, 4, 8, 4, 8, 4, 8, 4, 32 };
        playMusicChunk(n, d, 88, 69, 1);
        break;
      }
    case 13:
      {
        static const int n[] = { 392, 392, 440, 392, 523, 494, 392, 392, 440, 392, 587, 523, 392, 392, 784, 659, 523, 494, 440, 698, 698, 659, 523, 587, 523 };
        static const int d[] = { 1, 1, 2, 2, 2, 4, 1, 1, 2, 2, 2, 4, 1, 1, 2, 2, 2, 2, 4, 1, 1, 2, 2, 2, 4 };
        playMusicChunk(n, d, 25, 140, 2);
        break;
      }
  }

  trackIndex++;
  if (trackIndex > 12) {
    needsShuffle = true;
    jukeboxModeState = false;
    currentMode = MODE_ON;
    carStatus = "SYSTEM IDLE";
    headlightState = true;
    brakeState = true;
    underbodyState = true;
    setNewMasterAction(ACTION_NONE);
  }
}

void runCarMode() {
  static bool isFreshStart = true;
  if (!carModeState) {
    isFreshStart = true;
    return;
  }

  static const char *const themeNames[80] = {
    "F1 RACE START", "TURBO FLUTTER", "POLICE STROBE", "VIPER ALARM", "SYSTEM GLITCH",
    "V8 REVVING", "KITT SCANNER", "EMP SURGE", "SONAR PING", "AUTOBOT",
    "PIT LIMITER", "EURO POLICE", "BLOW-OFF VALVE", "V12 IDLE", "DRAG STRIP",
    "CYBER BOOT", "REV MATCH", "VIP ESCORT", "ALARM ARMED", "FINAL BOSS",
    "SAFETY CAR", "2-STEP REV", "HYDRAULICS", "RACE COUNTDOWN", "UFO HOVER",
    "MACHINE GUN", "MATRIX GLITCH", "POLICE YELP", "ENGINE DIE", "BATTERY DYING",
    "FLATLINE", "RAVE PARTY", "MORSE S.O.S", "BASS DROP", "THUNDERSTORM",
    "ALARM CHIRP", "WARNING BEACON", "SYSTEM REBOOT", "LEVEL UP", "THE NUKE",
    "AFTERBURNER", "LASER RICOCHET", "DEEP SONAR", "NUCLEAR SIREN", "HELICOPTER",
    "DIAL-UP MODEM", "GATLING GUN", "RAILGUN", "SPACE INVADERS", "POLTERGEIST",
    "DEFIBRILLATOR", "BOSS BATTLE", "POWER OUTAGE", "FORMULA E", "TRACTOR BEAM",
    "TERMINATOR", "TIME BOMB", "PINBALL", "DJ STROBE", "JUMP SCARE",
    "PAPARAZZI", "GEIGER COUNTER", "WARP DRIVE", "SLOT MACHINE", "DARTH VADER",
    "8-BIT INVADERS", "WELDING SPARKS", "GHOST FADE", "SUBWOOFER", "COIN DROP",
    "JET TURBINE", "RAINDROPS", "MEGAPHONE", "TYPEWRITER", "CHAINSAW",
    "NEON SIGN", "LASER SCANNER", "ADRENALINE", "ALIEN RADIO", "CENTURY FINALE"
  };

  static int playlist[80];
  static int trackIndex = 0;
  static bool needsShuffle = true;

  if (isFreshStart) {
    needsShuffle = true;
    trackIndex = 0;
    isFreshStart = false;
  }

  if (needsShuffle) {
    for (int i = 0; i < 80; i++) playlist[i] = i;
    for (int i = 79; i > 0; i--) {
      int j = random(0, i + 1);
      int temp = playlist[i];
      playlist[i] = playlist[j];
      playlist[j] = temp;
    }
    needsShuffle = false;
    trackIndex = 0;
  }

  int currentTheme = playlist[trackIndex];
  carStatus = themeNames[currentTheme];

  allOff();
  stopTone();
  unsigned long syncTime = millis();
  while (millis() - syncTime < 1500) {
    if (checkInterrupt()) return;
    wsTick(); // <--- FIXES CAR THEME UI DELAY
    yield();
  }

  executeTheme(currentTheme);

  if (!carModeState || cancelEffect) return;

  trackIndex++;
  if (trackIndex > 79) {
    needsShuffle = true;
    trackIndex = 0;
  }
}

void executeTheme(int currentTheme) {
  switch (currentTheme) {
    case 0:
      {
        setAllHead(0);
        setUnder(0);
        for (int i = 1; i <= 5; i++) {
          if (!carModeState) return;
          setAllBrake(i * 50);
          playTone(400);
          smartDelay(400);
          stopTone();
          smartDelay(600);
        }
        smartDelay(800);
        setAllHead(255);
        setAllBrake(0);
        setUnder(255);
        playTone(1000);
        smartDelay(800);
        stopTone();
        for (int f = 400; f < 1800; f += 15) {
          playTone(f);
          setUnder(255 - (f / 8));
          smartDelay(15);
        }
        stopTone();
        allOff();
        break;
      }
    case 1:
      {
        for (int f = 300; f < 2000; f += 20) {
          if (!carModeState) return;
          playTone(f);
          setAllHead(f / 8);
          smartDelay(10);
        }
        for (int i = 0; i < 6; i++) {
          playTone(1800 - (i * 100));
          setAllHead(255);
          setUnder(255);
          smartDelay(30);
          stopTone();
          setAllHead(0);
          setUnder(0);
          smartDelay(30);
        }
        stopTone();
        allOff();
        break;
      }
    case 2:
      {
        for (int i = 0; i < 8; i++) {
          if (!carModeState) return;
          setHead(255, 0);
          setBrake(255, 0);
          setUnder(255);
          playTone(800);
          smartDelay(40);
          allOff();
          smartDelay(40);
          setHead(255, 0);
          setBrake(255, 0);
          setUnder(255);
          smartDelay(40);
          allOff();
          smartDelay(80);
          setHead(0, 255);
          setBrake(0, 255);
          setUnder(255);
          playTone(1200);
          smartDelay(40);
          allOff();
          smartDelay(40);
          setHead(0, 255);
          setBrake(0, 255);
          setUnder(255);
          smartDelay(40);
          allOff();
          smartDelay(80);
        }
        stopTone();
        allOff();
        break;
      }
    case 3:
      {
        for (int i = 0; i < 3; i++) {
          for (int f = 600; f < 1200; f += 20) {
            if (!carModeState) return;
            playTone(f);
            setAllHead(255);
            setAllBrake(255);
            smartDelay(5);
          }
          for (int f = 1200; f > 600; f -= 20) {
            playTone(f);
            setAllHead(0);
            setAllBrake(0);
            smartDelay(5);
          }
        }
        for (int i = 0; i < 4; i++) {
          playTone(1500);
          setAllHead(255);
          setAllBrake(255);
          setUnder(255);
          smartDelay(100);
          stopTone();
          allOff();
          smartDelay(100);
        }
        break;
      }
    case 4:
      {
        for (int i = 0; i < 25; i++) {
          if (!carModeState) return;
          if (random(0, 2) == 0) setAllHead(255);
          else setAllHead(0);
          if (random(0, 2) == 0) setAllBrake(255);
          else setAllBrake(0);
          if (random(0, 2) == 0) setUnder(255);
          else setUnder(0);
          playTone(random(800, 3500));
          smartDelay(random(10, 60));
        }
        stopTone();
        allOff();
        break;
      }
    case 5:
      {
        setUnder(50);
        for (int revs = 0; revs < 4; revs++) {
          if (!carModeState) return;
          int peak = random(800, 1500);
          for (int f = 100; f < peak; f += 30) {
            playTone(f);
            setAllBrake(f / 6);
            setUnder(f / 6);
            smartDelay(15);
          }
          for (int f = peak; f > 100; f -= 10) {
            playTone(f);
            setAllBrake(f / 6);
            setUnder(f / 6);
            smartDelay(10);
          }
          smartDelay(random(100, 400));
        }
        stopTone();
        allOff();
        break;
      }
    case 6:
      {
        for (int i = 0; i < 4; i++) {
          if (!carModeState) return;
          int p[] = { L_HEAD_PIN, L_BRAKE_PIN, UNDERBODY_PIN, R_BRAKE_PIN, R_HEAD_PIN };
          for (int j = 0; j < 5; j++) {
            allOff();
            analogWrite(p[j], 255);
            playTone(200 + (j * 50));
            smartDelay(120);
          }
          for (int j = 3; j >= 0; j--) {
            allOff();
            analogWrite(p[j], 255);
            playTone(200 + (j * 50));
            smartDelay(120);
          }
        }
        stopTone();
        allOff();
        break;
      }
    case 7:
      {
        for (int i = 0; i < 255; i += 2) {
          if (!carModeState) return;
          setAllHead(i);
          setAllBrake(i);
          setUnder(i);
          playTone(100 + i * 8);
          smartDelay(15);
        }
        for (int i = 0; i < 8; i++) {
          allOff();
          stopTone();
          smartDelay(30);
          setAllHead(255);
          setAllBrake(255);
          setUnder(255);
          playTone(3000);
          smartDelay(30);
        }
        stopTone();
        allOff();
        break;
      }
    case 8:
      {
        for (int i = 0; i < 4; i++) {
          if (!carModeState) return;
          setAllHead(0);
          setAllBrake(0);
          setUnder(10);
          for (int f = 200; f < 600; f += 20) {
            playTone(f);
            smartDelay(15);
          }
          setAllHead(255);
          setAllBrake(255);
          setUnder(255);
          playTone(2000);
          smartDelay(100);
          stopTone();
          allOff();
          smartDelay(1000);
        }
        break;
      }
    case 9:
      {
        setAllHead(255);
        setAllBrake(255);
        setUnder(255);
        for (int step = 0; step < 5; step++) {
          if (!carModeState) return;
          for (int f = 1200 - (step * 200); f > 800 - (step * 200); f -= 40) {
            playTone(f);
            smartDelay(10);
          }
          setAllHead(200 - (step * 40));
          setAllBrake(200 - (step * 40));
          setUnder(200 - (step * 40));
          smartDelay(50);
        }
        stopTone();
        allOff();
        break;
      }
    case 10:
      {
        for (int i = 0; i < 15; i++) {
          if (!carModeState) return;
          setAllHead(255);
          setAllBrake(255);
          setUnder(255);
          playTone(1800);
          smartDelay(40);
          allOff();
          stopTone();
          smartDelay(40);
        }
        break;
      }
    case 11:
      {
        for (int i = 0; i < 6; i++) {
          if (!carModeState) return;
          setHead(255, 0);
          setBrake(255, 0);
          setUnder(255);
          playTone(1200);
          smartDelay(400);
          setHead(0, 255);
          setBrake(0, 255);
          setUnder(0);
          playTone(1600);
          smartDelay(400);
        }
        allOff();
        stopTone();
        break;
      }
    case 12:
      {
        for (int f = 500; f < 2500; f += 30) {
          if (!carModeState) return;
          playTone(f);
          setUnder(f / 10);
          setAllHead(f / 10);
          smartDelay(15);
        }
        setAllBrake(255);
        setAllHead(255);
        playTone(3500);
        smartDelay(50);
        for (int f = 3500; f > 800; f -= 150) {
          if (!carModeState) return;
          playTone(f);
          setUnder(255);
          setAllHead(0);
          smartDelay(10);
        }
        allOff();
        stopTone();
        break;
      }
    case 13:
      {
        setUnder(80);
        for (int i = 0; i < 15; i++) {
          if (!carModeState) return;
          setAllBrake(255);
          playTone(120);
          smartDelay(30);
          setAllBrake(40);
          stopTone();
          smartDelay(random(60, 100));
        }
        allOff();
        break;
      }
    case 14:
      {
        for (int i = 0; i < 3; i++) {
          if (!carModeState) return;
          setAllBrake(255);
          playTone(600);
          smartDelay(300);
          setAllBrake(0);
          stopTone();
          smartDelay(400);
        }
        setAllHead(255);
        setUnder(255);
        playTone(1800);
        smartDelay(1000);
        allOff();
        stopTone();
        break;
      }
    case 15:
      {
        for (int i = 0; i < 25; i++) {
          if (!carModeState) return;
          int h = random(0, 255);
          setAllHead(h);
          setUnder(h);
          setAllBrake(255 - h);
          playTone(random(500, 3500));
          smartDelay(random(15, 50));
        }
        setAllHead(255);
        setUnder(255);
        setAllBrake(255);
        playTone(1200);
        smartDelay(600);
        allOff();
        stopTone();
        break;
      }
    case 16:
      {
        for (int rev = 0; rev < 3; rev++) {
          if (!carModeState) return;
          setAllBrake(255);
          setAllHead(255);
          for (int f = 1500; f < 2800; f += 150) {
            playTone(f);
            smartDelay(10);
          }
          setAllBrake(50);
          setAllHead(50);
          for (int f = 2800; f > 1000; f -= 50) {
            playTone(f);
            smartDelay(10);
          }
          smartDelay(200);
        }
        allOff();
        stopTone();
        break;
      }
    case 17:
      {
        for (int i = 0; i < 4; i++) {
          if (!carModeState) return;
          for (int j = 0; j < 3; j++) {
            setHead(255, 0);
            setBrake(255, 0);
            setUnder(100);
            smartDelay(40);
            allOff();
            smartDelay(40);
          }
          playTone(250);
          smartDelay(150);
          stopTone();
          for (int j = 0; j < 3; j++) {
            setHead(0, 255);
            setBrake(0, 255);
            setUnder(100);
            smartDelay(40);
            allOff();
            smartDelay(40);
          }
          playTone(250);
          smartDelay(150);
          stopTone();
        }
        break;
      }
    case 18:
      {
        for (int i = 0; i < 2; i++) {
          if (!carModeState) return;
          setAllHead(255);
          setAllBrake(255);
          playTone(2500);
          smartDelay(50);
          allOff();
          stopTone();
          smartDelay(100);
        }
        smartDelay(400);
        for (int i = 0; i < 255; i += 3) {
          if (!carModeState) return;
          setAllBrake(i);
          smartDelay(15);
        }
        for (int i = 255; i > 0; i -= 3) {
          if (!carModeState) return;
          setAllBrake(i);
          smartDelay(15);
        }
        break;
      }
    case 19:
      {
        for (int i = 0; i < 255; i += 5) {
          if (!carModeState) return;
          setAllHead(i);
          setAllBrake(255 - i);
          setUnder(i);
          playTone(200 + i * 5);
          smartDelay(20);
        }
        for (int i = 0; i < 10; i++) {
          if (!carModeState) return;
          setAllHead(255);
          setAllBrake(255);
          setUnder(255);
          playTone(3500);
          smartDelay(40);
          allOff();
          stopTone();
          smartDelay(40);
        }
        break;
      }
    case 20:
      {
        for (int i = 0; i < 8; i++) {
          if (!carModeState) return;
          setHead(255, 0);
          setBrake(0, 255);
          setUnder(100);
          playTone(800);
          smartDelay(300);
          setHead(0, 255);
          setBrake(255, 0);
          setUnder(200);
          playTone(1000);
          smartDelay(300);
        }
        allOff();
        stopTone();
        break;
      }
    case 21:
      {
        setAllHead(255);
        setUnder(100);
        for (int f = 1000; f < 3000; f += 100) {
          if (!carModeState) return;
          playTone(f);
          smartDelay(15);
        }
        for (int i = 0; i < 20; i++) {
          if (!carModeState) return;
          setAllBrake(255);
          playTone(3000);
          smartDelay(30);
          setAllBrake(0);
          stopTone();
          smartDelay(random(20, 50));
        }
        for (int f = 3000; f > 1000; f -= 50) {
          if (!carModeState) return;
          playTone(f);
          smartDelay(10);
        }
        allOff();
        stopTone();
        break;
      }
    case 22:
      {
        for (int i = 0; i < 8; i++) {
          if (!carModeState) return;
          setUnder(255);
          playTone(150);
          smartDelay(80);
          setUnder(50);
          stopTone();
          smartDelay(80);
          setUnder(255);
          playTone(150);
          smartDelay(80);
          setUnder(0);
          stopTone();
          smartDelay(300);
        }
        allOff();
        break;
      }
    case 23:
      {
        for (int i = 0; i < 3; i++) {
          if (!carModeState) return;
          setAllBrake(255);
          playTone(1000);
          smartDelay(400);
          setAllBrake(0);
          stopTone();
          smartDelay(600);
        }
        setAllHead(255);
        setAllBrake(255);
        setUnder(255);
        playTone(2000);
        smartDelay(1200);
        allOff();
        stopTone();
        break;
      }
    case 24:
      {
        for (int i = 0; i < 4; i++) {
          if (!carModeState) return;
          for (int f = 400; f < 800; f += 10) {
            setUnder(map(f, 400, 800, 50, 255));
            playTone(f);
            smartDelay(15);
          }
          for (int f = 800; f > 400; f -= 10) {
            setUnder(map(f, 400, 800, 50, 255));
            playTone(f);
            smartDelay(15);
          }
        }
        allOff();
        stopTone();
        break;
      }
    case 25:
      {
        setUnder(50);
        setAllHead(100);
        for (int i = 0; i < 30; i++) {
          if (!carModeState) return;
          setAllBrake(255);
          playTone(100);
          smartDelay(20);
          setAllBrake(0);
          stopTone();
          smartDelay(25);
        }
        allOff();
        break;
      }
    case 26:
      {
        for (int i = 0; i < 40; i++) {
          if (!carModeState) return;
          if (random(0, 2)) setHead(255, 0);
          else setHead(0, 255);
          if (random(0, 2)) setBrake(255, 0);
          else setBrake(0, 255);
          setUnder(random(0, 255));
          playTone(random(500, 4000));
          smartDelay(random(10, 80));
          allOff();
          stopTone();
          smartDelay(random(10, 50));
        }
        break;
      }
    case 27:
      {
        for (int i = 0; i < 3; i++) {
          if (!carModeState) return;
          setAllHead(255);
          setAllBrake(255);
          setUnder(255);
          for (int f = 800; f < 2200; f += 100) {
            playTone(f);
            smartDelay(10);
          }
          allOff();
          stopTone();
          smartDelay(150);
        }
        smartDelay(1000);
        break;
      }
    case 28:
      {
        setUnder(50);
        playTone(300);
        setAllHead(100);
        smartDelay(100);
        stopTone();
        setAllHead(0);
        smartDelay(100);
        playTone(300);
        setAllHead(100);
        smartDelay(100);
        stopTone();
        setAllHead(0);
        smartDelay(100);
        for (int f = 300; f > 50; f -= 10) {
          if (!carModeState) return;
          playTone(f);
          setUnder(f / 6);
          smartDelay(20);
        }
        allOff();
        stopTone();
        smartDelay(1000);
        break;
      }
    case 29:
      {
        setAllHead(255);
        setAllBrake(255);
        setUnder(255);
        for (int i = 255; i > 0; i -= 2) {
          if (!carModeState) return;
          setAllHead(i);
          setAllBrake(i);
          setUnder(i);
          playTone(1000 + i * 2);
          smartDelay(25);
        }
        allOff();
        stopTone();
        smartDelay(1000);
        break;
      }
    case 30:
      {
        for (int i = 0; i < 3; i++) {
          if (!carModeState) return;
          setAllBrake(255);
          playTone(800);
          smartDelay(100);
          setAllBrake(0);
          stopTone();
          smartDelay(100);
          setAllBrake(255);
          playTone(800);
          smartDelay(100);
          setAllBrake(0);
          stopTone();
          smartDelay(800);
        }
        setAllBrake(255);
        setUnder(255);
        playTone(1500);
        smartDelay(2000);
        for (int i = 255; i > 0; i -= 5) {
          if (!carModeState) return;
          setAllBrake(i);
          setUnder(i);
          smartDelay(20);
        }
        allOff();
        stopTone();
        break;
      }
    case 31:
      {
        for (int i = 0; i < 50; i++) {
          if (!carModeState) return;
          setAllHead(random(0, 2) * 255);
          setAllBrake(random(0, 2) * 255);
          setUnder(random(0, 255));
          playTone(random(2000, 3000));
          smartDelay(random(20, 60));
        }
        allOff();
        stopTone();
        break;
      }
    case 32:
      {
        int dot = 150, dash = 400, gap = 150;
        auto beep = [&](int time) {
          setAllHead(255);
          playTone(1200);
          smartDelay(time);
          setAllHead(0);
          stopTone();
          smartDelay(gap);
        };
        for (int i = 0; i < 3; i++) {
          if (!carModeState) return;
          beep(dot);
        }
        smartDelay(200);
        for (int i = 0; i < 3; i++) {
          if (!carModeState) return;
          beep(dash);
        }
        smartDelay(200);
        for (int i = 0; i < 3; i++) {
          if (!carModeState) return;
          beep(dot);
        }
        smartDelay(1000);
        break;
      }
    case 33:
      {
        for (int i = 0; i < 3; i++) {
          if (!carModeState) return;
          setAllHead(255);
          setAllBrake(255);
          for (int f = 2000; f > 100; f -= 20) {
            playTone(f);
            setUnder(map(f, 2000, 100, 0, 255));
            smartDelay(10);
          }
          stopTone();
          allOff();
          smartDelay(500);
        }
        break;
      }
    case 34:
      {
        setUnder(10);
        for (int i = 0; i < 4; i++) {
          if (!carModeState) return;
          smartDelay(random(500, 2000));
          setAllHead(255);
          setAllBrake(255);
          setUnder(255);
          smartDelay(random(20, 80));
          allOff();
          setUnder(10);
          smartDelay(random(20, 80));
          setAllHead(255);
          setAllBrake(255);
          setUnder(255);
          smartDelay(random(20, 80));
          allOff();
          setUnder(10);
          smartDelay(random(200, 600));
          for (int j = 0; j < 15; j++) {
            playTone(random(80, 150));
            smartDelay(random(20, 60));
          }
          stopTone();
        }
        allOff();
        break;
      }
    case 35:
      {
        for (int i = 0; i < 5; i++) {
          if (!carModeState) return;
          setAllHead(255);
          setAllBrake(255);
          setUnder(255);
          playTone(3000);
          smartDelay(40);
          allOff();
          stopTone();
          smartDelay(60);
          setAllHead(255);
          setAllBrake(255);
          setUnder(255);
          playTone(3000);
          smartDelay(40);
          allOff();
          stopTone();
          smartDelay(1500);
        }
        break;
      }
    case 36:
      {
        for (int i = 0; i < 6; i++) {
          if (!carModeState) return;
          setAllBrake(255);
          setUnder(100);
          playTone(600);
          smartDelay(200);
          allOff();
          stopTone();
          smartDelay(800);
        }
        break;
      }
    case 37:
      {
        playTone(500);
        setUnder(50);
        smartDelay(500);
        stopTone();
        smartDelay(200);
        playTone(1000);
        setBrake(255, 0);
        smartDelay(500);
        stopTone();
        smartDelay(200);
        playTone(1500);
        setBrake(255, 255);
        smartDelay(500);
        stopTone();
        smartDelay(200);
        playTone(2000);
        setHead(255, 0);
        smartDelay(500);
        stopTone();
        smartDelay(200);
        playTone(2500);
        setHead(255, 255);
        smartDelay(500);
        stopTone();
        smartDelay(500);
        allOff();
        break;
      }
    case 38:
      {
        int notes[] = { 1000, 1200, 1500, 2000, 2500, 3000 };
        for (int i = 0; i < 6; i++) {
          if (!carModeState) return;
          setAllHead(i * 40);
          setAllBrake(i * 40);
          setUnder(i * 40);
          playTone(notes[i]);
          smartDelay(100);
          stopTone();
        }
        smartDelay(1000);
        allOff();
        break;
      }
    case 39:
      {
        for (int f = 200; f < 4000; f += 15) {
          if (!carModeState) return;
          playTone(f);
          setUnder(map(f, 200, 4000, 0, 255));
          smartDelay(15);
        }
        setAllHead(255);
        setAllBrake(255);
        setUnder(255);
        playTone(4000);
        smartDelay(100);
        stopTone();
        smartDelay(1500);
        for (int i = 255; i > 0; i -= 2) {
          if (!carModeState) return;
          setAllHead(i);
          setAllBrake(i);
          setUnder(i);
          smartDelay(20);
        }
        allOff();
        smartDelay(1500);
        break;
      }
    case 40:
      {
        for (int f = 500; f < 3000; f += 50) {
          if (!carModeState) return;
          playTone(f);
          setUnder(map(f, 500, 3000, 0, 255));
          smartDelay(20);
        }
        setAllHead(255);
        setAllBrake(255);
        playTone(3000);
        smartDelay(1500);
        for (int f = 3000; f > 500; f -= 100) {
          if (!carModeState) return;
          playTone(f);
          setAllHead(0);
          setAllBrake(0);
          setUnder(map(f, 500, 3000, 0, 255));
          smartDelay(15);
        }
        allOff();
        stopTone();
        break;
      }
    case 41:
      {
        for (int i = 0; i < 15; i++) {
          if (!carModeState) return;
          if (random(0, 2)) setHead(255, 0);
          else setHead(0, 255);
          for (int f = 3000; f > 1000; f -= 200) {
            playTone(f);
            smartDelay(5);
          }
          allOff();
          stopTone();
          smartDelay(random(20, 100));
        }
        break;
      }
    case 42:
      {
        for (int i = 0; i < 3; i++) {
          if (!carModeState) return;
          setAllHead(10);
          setAllBrake(10);
          setUnder(255);
          playTone(2500);
          smartDelay(100);
          stopTone();
          for (int u = 255; u > 0; u -= 2) {
            if (!carModeState) return;
            setUnder(u);
            smartDelay(20);
          }
          smartDelay(1500);
        }
        break;
      }
    case 43:
      {
        for (int i = 0; i < 6; i++) {
          if (!carModeState) return;
          setAllHead(255);
          setAllBrake(0);
          setUnder(100);
          playTone(800);
          smartDelay(350);
          setAllHead(0);
          setAllBrake(255);
          setUnder(255);
          playTone(1200);
          smartDelay(350);
        }
        setAllHead(255);
        setAllBrake(255);
        setUnder(255);
        playTone(1500);
        smartDelay(1000);
        allOff();
        stopTone();
        break;
      }
    case 44:
      {
        for (int d = 150; d > 10; d -= 5) {
          if (!carModeState) return;
          setAllHead(255);
          setAllBrake(255);
          playTone(100);
          smartDelay(15);
          allOff();
          stopTone();
          smartDelay(d);
        }
        for (int i = 0; i < 30; i++) {
          if (!carModeState) return;
          setAllHead(255);
          playTone(150);
          smartDelay(10);
          allOff();
          stopTone();
          smartDelay(10);
        }
        break;
      }
    case 45:
      {
        setUnder(50);
        for (int i = 0; i < 30; i++) {
          if (!carModeState) return;
          if (random(0, 2)) setHead(255, 0);
          else setHead(0, 255);
          playTone(random(1500, 4000));
          smartDelay(random(10, 50));
          stopTone();
          allOff();
          smartDelay(random(5, 20));
        }
        setAllHead(255);
        setUnder(255);
        playTone(2500);
        smartDelay(800);
        allOff();
        stopTone();
        break;
      }
    case 46:
      {
        for (int i = 0; i < 40; i++) {
          if (!carModeState) return;
          setAllHead(255);
          setAllBrake(255);
          playTone(random(100, 250));
          smartDelay(15);
          allOff();
          stopTone();
          smartDelay(20);
        }
        smartDelay(500);
        break;
      }
    case 47:
      {
        for (int f = 100; f < 3500; f += 20) {
          if (!carModeState) return;
          playTone(f);
          setAllHead(map(f, 100, 3500, 0, 255));
          smartDelay(10);
        }
        stopTone();
        allOff();
        smartDelay(100);
        setAllHead(255);
        setAllBrake(255);
        setUnder(255);
        playTone(100);
        smartDelay(300);
        for (int i = 255; i > 0; i -= 5) {
          if (!carModeState) return;
          setAllHead(i);
          setAllBrake(i);
          setUnder(i);
          smartDelay(15);
        }
        allOff();
        stopTone();
        break;
      }
    case 48:
      {
        for (int i = 0; i < 5; i++) {
          if (!carModeState) return;
          setHead(255, 0);
          setBrake(255, 0);
          for (int f = 2000; f > 1000; f -= 100) {
            playTone(f);
            smartDelay(10);
          }
          allOff();
          stopTone();
          smartDelay(50);
          setHead(0, 255);
          setBrake(0, 255);
          for (int f = 2000; f > 1000; f -= 100) {
            playTone(f);
            smartDelay(10);
          }
          allOff();
          stopTone();
          smartDelay(150);
        }
        break;
      }
    case 49:
      {
        for (int i = 0; i < 20; i++) {
          if (!carModeState) return;
          setAllHead(random(10, 150));
          setUnder(random(0, 50));
          if (random(0, 3) == 0) playTone(random(2000, 3000));
          else stopTone();
          smartDelay(random(10, 200));
        }
        allOff();
        stopTone();
        break;
      }
    case 50:
      {
        for (int i = 0; i < 4; i++) {
          if (!carModeState) return;
          setAllBrake(255);
          playTone(800);
          smartDelay(50);
          allOff();
          stopTone();
          smartDelay(50);
          setAllBrake(255);
          playTone(800);
          smartDelay(50);
          allOff();
          stopTone();
          smartDelay(600);
        }
        playTone(1500);
        setAllBrake(50);
        smartDelay(1500);
        stopTone();
        allOff();
        smartDelay(400);
        setAllHead(255);
        setAllBrake(255);
        setUnder(255);
        playTone(100);
        smartDelay(150);
        allOff();
        stopTone();
        smartDelay(600);
        setAllBrake(255);
        playTone(800);
        smartDelay(50);
        allOff();
        stopTone();
        break;
      }
    case 51:
      {
        int notes[] = { 440, 494, 523, 587, 659, 698, 784, 880 };
        for (int j = 0; j < 3; j++) {
          for (int i = 0; i < 8; i++) {
            if (!carModeState) return;
            if (i % 2 == 0) setAllHead(255);
            else setAllBrake(255);
            playTone(notes[i] * 2);
            smartDelay(40);
            allOff();
          }
        }
        stopTone();
        break;
      }
    case 52:
      {
        setAllHead(255);
        setAllBrake(255);
        setUnder(255);
        playTone(1000);
        smartDelay(500);
        for (int i = 255; i > 0; i -= 3) {
          if (!carModeState) return;
          setAllHead(i);
          setAllBrake(i);
          setUnder(i);
          playTone(map(i, 255, 0, 1000, 100));
          smartDelay(15);
        }
        playTone(80);
        smartDelay(100);
        allOff();
        stopTone();
        smartDelay(1000);
        break;
      }
    case 53:
      {
        for (int gear = 1; gear <= 4; gear++) {
          if (!carModeState) return;
          setAllHead(gear * 60);
          setAllBrake(gear * 10);
          for (int f = 1000; f < 3500; f += 80) {
            playTone(f);
            smartDelay(10);
          }
          stopTone();
          smartDelay(40);
        }
        for (int f = 3500; f > 1000; f -= 30) {
          if (!carModeState) return;
          playTone(f);
          smartDelay(10);
        }
        allOff();
        stopTone();
        break;
      }
    case 54:
      {
        for (int i = 0; i < 4; i++) {
          if (!carModeState) return;
          setUnder(255);
          for (int f = 800; f < 1600; f += 20) {
            setHead(255, 0);
            playTone(f);
            smartDelay(5);
          }
          for (int f = 1600; f > 800; f -= 20) {
            setHead(0, 255);
            playTone(f);
            smartDelay(5);
          }
        }
        allOff();
        stopTone();
        break;
      }
    case 55:
      {
        for (int i = 0; i < 6; i++) {
          if (!carModeState) return;
          setAllHead(255);
          playTone(150);
          smartDelay(80);
          allOff();
          stopTone();
          smartDelay(600);
        }
        break;
      }
    case 56:
      {
        int delayTime = 500;
        while (delayTime > 20 && carModeState) {
          setAllBrake(255);
          playTone(2000);
          smartDelay(40);
          allOff();
          stopTone();
          smartDelay(delayTime);
          delayTime -= (delayTime > 100) ? 40 : 10;
        }
        setAllHead(255);
        setAllBrake(255);
        setUnder(255);
        playTone(100);
        smartDelay(400);
        for (int i = 255; i > 0; i -= 2) {
          if (!carModeState) return;
          setAllHead(i);
          setAllBrake(i);
          setUnder(i);
          smartDelay(15);
        }
        allOff();
        stopTone();
        break;
      }
    case 57:
      {
        for (int i = 0; i < 20; i++) {
          if (!carModeState) return;
          if (i % 2 == 0) {
            setHead(255, 0);
            setBrake(0, 255);
          } else {
            setHead(0, 255);
            setBrake(255, 0);
          }
          setUnder(255);
          playTone(random(2000, 3500));
          smartDelay(30);
          allOff();
          stopTone();
          smartDelay(30);
        }
        break;
      }
    case 58:
      {
        for (int i = 0; i < 16; i++) {
          if (!carModeState) return;
          int r = random(0, 4);
          if (r == 0) setAllHead(255);
          else if (r == 1) setAllBrake(255);
          else if (r == 2) setHead(255, 0);
          else setHead(0, 255);
          setUnder(random(0, 255));
          playTone(150);
          smartDelay(40);
          allOff();
          stopTone();
          smartDelay(120);
        }
        break;
      }
    case 59:
      {
        allOff();
        stopTone();
        smartDelay(3000);
        if (!carModeState) return;
        playTone(120);
        smartDelay(50);
        stopTone();
        smartDelay(2000);
        if (!carModeState) return;
        setAllHead(255);
        setAllBrake(255);
        setUnder(255);
        playTone(4000);
        smartDelay(250);
        allOff();
        stopTone();
        break;
      }
    case 60:
      {
        for (int i = 0; i < 30; i++) {
          if (!carModeState) return;
          int r = random(0, 3);
          if (r == 0) setAllHead(255);
          else if (r == 1) setAllBrake(255);
          else setUnder(255);
          playTone(3500);
          smartDelay(15);
          allOff();
          stopTone();
          smartDelay(random(20, 150));
        }
        break;
      }
    case 61:
      {
        setUnder(30);
        setAllHead(10);
        for (int i = 0; i < 80; i++) {
          if (!carModeState) return;
          setAllBrake(255);
          playTone(2000);
          smartDelay(5);
          setAllBrake(0);
          stopTone();
          smartDelay(random(10, 150));
        }
        allOff();
        break;
      }
    case 62:
      {
        for (int f = 200; f < 3000; f += 20) {
          if (!carModeState) return;
          playTone(f);
          setUnder(map(f, 200, 3000, 0, 255));
          smartDelay(15);
        }
        setAllHead(255);
        setAllBrake(255);
        playTone(3000);
        smartDelay(100);
        allOff();
        stopTone();
        smartDelay(1000);
        break;
      }
    case 63:
      {
        for (int i = 0; i < 25; i++) {
          if (!carModeState) return;
          setHead(255, 0);
          setBrake(0, 255);
          playTone(1500);
          smartDelay(30);
          setHead(0, 255);
          setBrake(255, 0);
          playTone(2000);
          smartDelay(30);
        }
        setAllHead(255);
        setAllBrake(255);
        setUnder(255);
        playTone(3000);
        smartDelay(1000);
        allOff();
        stopTone();
        break;
      }
    case 64:
      {
        for (int i = 0; i < 3; i++) {
          for (int b = 0; b < 150; b += 2) {
            if (!carModeState) return;
            setAllHead(b);
            playTone(100);
            smartDelay(20);
          }
          for (int b = 150; b > 0; b -= 2) {
            if (!carModeState) return;
            setAllHead(b);
            playTone(80);
            smartDelay(20);
          }
          stopTone();
          smartDelay(800);
        }
        allOff();
        break;
      }
    case 65:
      {
        for (int i = 0; i < 6; i++) {
          if (!carModeState) return;
          setHead(255, 255);
          setBrake(0, 0);
          for (int f = 2000; f > 1000; f -= 100) {
            playTone(f);
            smartDelay(10);
          }
          setHead(0, 0);
          setBrake(255, 255);
          for (int f = 2000; f > 1000; f -= 100) {
            playTone(f);
            smartDelay(10);
          }
        }
        allOff();
        stopTone();
        break;
      }
    case 66:
      {
        setUnder(20);
        for (int i = 0; i < 15; i++) {
          if (!carModeState) return;
          int sparkTime = random(20, 100);
          setAllHead(255);
          playTone(random(1000, 3000));
          smartDelay(sparkTime);
          setAllHead(0);
          stopTone();
          smartDelay(random(10, 50));
        }
        smartDelay(500);
        allOff();
        break;
      }
    case 67:
      {
        for (int i = 0; i < 255; i += 2) {
          if (!carModeState) return;
          setAllHead(i);
          setUnder(i);
          playTone(map(i, 0, 255, 200, 800));
          smartDelay(20);
        }
        for (int i = 255; i > 0; i -= 2) {
          if (!carModeState) return;
          setAllHead(i);
          setUnder(i);
          playTone(map(i, 0, 255, 200, 800));
          smartDelay(20);
        }
        stopTone();
        allOff();
        smartDelay(500);
        break;
      }
    case 68:
      {
        setAllBrake(255);
        setUnder(255);
        for (int f = 800; f > 50; f -= 10) {
          if (!carModeState) return;
          playTone(f);
          smartDelay(20);
        }
        stopTone();
        allOff();
        smartDelay(1000);
        break;
      }
    case 69:
      {
        for (int i = 0; i < 3; i++) {
          if (!carModeState) return;
          setAllHead(255);
          playTone(2000);
          smartDelay(50);
          setAllHead(0);
          playTone(2500);
          smartDelay(100);
          stopTone();
          smartDelay(200);
        }
        break;
      }
    case 70:
      {
        for (int f = 100; f < 4000; f += 10) {
          if (!carModeState) return;
          playTone(f);
          setAllBrake(map(f, 100, 4000, 0, 255));
          smartDelay(10);
        }
        stopTone();
        allOff();
        smartDelay(500);
        break;
      }
    case 71:
      {
        setUnder(10);
        for (int i = 0; i < 50; i++) {
          if (!carModeState) return;
          int p[] = { L_HEAD_PIN, R_HEAD_PIN, L_BRAKE_PIN, R_BRAKE_PIN };
          int pin = p[random(0, 4)];
          analogWrite(pin, 150);
          playTone(random(500, 1000));
          smartDelay(10);
          analogWrite(pin, 0);
          stopTone();
          smartDelay(random(50, 200));
        }
        allOff();
        break;
      }
    case 72:
      {
        for (int i = 0; i < 2; i++) {
          if (!carModeState) return;
          setHead(255, 0);
          setBrake(0, 255);
          for (int f = 600; f < 1400; f += 40) {
            playTone(f);
            smartDelay(10);
          }
          setHead(0, 255);
          setBrake(255, 0);
          for (int f = 1400; f > 600; f -= 40) {
            playTone(f);
            smartDelay(10);
          }
        }
        allOff();
        stopTone();
        break;
      }
    case 73:
      {
        for (int i = 0; i < 15; i++) {
          if (!carModeState) return;
          setAllHead(255);
          playTone(800);
          smartDelay(15);
          setAllHead(0);
          stopTone();
          smartDelay(random(80, 250));
        }
        setAllBrake(255);
        playTone(2500);
        smartDelay(100);
        setAllBrake(0);
        stopTone();
        smartDelay(400);
        break;
      }
    case 74:
      {
        setUnder(50);
        for (int i = 0; i < 4; i++) {
          if (!carModeState) return;
          setAllBrake(255);
          for (int f = 150; f < 400; f += 20) {
            playTone(f);
            smartDelay(10);
          }
          setAllBrake(50);
          for (int f = 400; f > 150; f -= 20) {
            playTone(f);
            smartDelay(10);
          }
          smartDelay(random(50, 200));
        }
        allOff();
        stopTone();
        break;
      }
    case 75:
      {
        for (int i = 0; i < 10; i++) {
          if (!carModeState) return;
          setAllHead(255);
          setUnder(255);
          playTone(120);
          smartDelay(random(10, 80));
          allOff();
          stopTone();
          smartDelay(random(10, 150));
        }
        setAllHead(255);
        setUnder(255);
        playTone(120);
        smartDelay(1000);
        allOff();
        stopTone();
        break;
      }
    case 76:
      {
        for (int i = 0; i < 4; i++) {
          if (!carModeState) return;
          setHead(255, 0);
          playTone(3000);
          smartDelay(50);
          setHead(0, 0);
          setBrake(255, 0);
          playTone(2500);
          smartDelay(50);
          setBrake(0, 0);
          setUnder(255);
          playTone(2000);
          smartDelay(50);
          setUnder(0);
          setBrake(0, 255);
          playTone(2500);
          smartDelay(50);
          setBrake(0, 0);
          setHead(0, 255);
          playTone(3000);
          smartDelay(50);
          setHead(0, 0);
          stopTone();
          smartDelay(200);
        }
        break;
      }
    case 77:
      {
        int beatDelay = 800;
        while (beatDelay > 100 && carModeState) {
          setAllBrake(255);
          playTone(400);
          smartDelay(50);
          setAllBrake(0);
          stopTone();
          smartDelay(50);
          setAllBrake(255);
          playTone(400);
          smartDelay(50);
          setAllBrake(0);
          stopTone();
          smartDelay(beatDelay);
          beatDelay -= 80;
        }
        setAllBrake(255);
        playTone(2000);
        smartDelay(1000);
        allOff();
        stopTone();
        break;
      }
    case 78:
      {
        for (int i = 0; i < 40; i++) {
          if (!carModeState) return;
          setUnder(random(0, 255));
          setAllHead(random(0, 50));
          playTone(random(400, 3500));
          smartDelay(random(20, 100));
        }
        allOff();
        stopTone();
        break;
      }
    case 79:
      {
        for (int i = 0; i < 255; i += 10) {
          if (!carModeState) return;
          setAllHead(i);
          setAllBrake(i);
          setUnder(i);
          playTone(i * 10);
          smartDelay(20);
        }
        for (int i = 0; i < 15; i++) {
          if (!carModeState) return;
          setAllHead(255);
          setAllBrake(0);
          playTone(3000);
          smartDelay(30);
          setAllHead(0);
          setAllBrake(255);
          playTone(1500);
          smartDelay(30);
        }
        setAllHead(255);
        setAllBrake(255);
        setUnder(255);
        playTone(4000);
        smartDelay(1500);
        allOff();
        stopTone();
        smartDelay(1000);
        break;
      }
  }
}

void runMarioMode() {
  static bool isFreshStart = true;
  if (!marioModeState) { isFreshStart = true; return; }
  
  static int marioPhase = 0;
  int tempo = 125;

  if (isFreshStart) {
    marioPhase = 0;
    isFreshStart = false;
  }

  // 👇 This is the piece that got accidentally deleted! 👇
  auto playMusicChunk = [&](const int notes[], const int dur[], int len, int speed) {
    for (int i = 0; i < len; i++) {
      if (!marioModeState || cancelEffect) {
        stopTone();
        allOff();
        return;
      }
      int n = notes[i];
      int noteDuration = speed * dur[i];
      unsigned long noteStartTime = millis();

      if (n > 0) {
        playTone(n);
        if (n >= 784) {
          setAllHead(255);
          setAllBrake(255);
          setUnder(255);
        } else if (n >= 523) {
          setAllHead(255);
          setAllBrake(0);
          setUnder(100);
        } else {
          setAllHead(50);
          setAllBrake(50);
          setUnder(0);
        }
      } else {
        stopTone();
        allOff();
      }

      while (millis() - noteStartTime < (noteDuration * 0.85)) {
        wsTick(); // <--- FIXES MARIO UI DELAY
        delay(1);
        yield();
      }
      stopTone();
      allOff();
      handleBonnet();
      handleLeftDoor();
      handleRightDoor();
      while (millis() - noteStartTime < noteDuration) {
        wsTick(); // <--- FIXES MARIO UI DELAY
        delay(1);
        yield();
      }
    }
  };

  switch (marioPhase) {
    case 0:
      {
        const int nMain[] = { 659, 659, 0, 659, 0, 523, 659, 0, 784, 0, 0, 392, 0, 0, 523, 0, 0, 392, 0, 0, 330, 0, 0, 440, 0, 494, 0, 466, 440, 0, 392, 659, 784, 880, 0, 698, 784, 0, 659, 0, 523, 587, 494, 0, 0, 523, 0, 0, 392, 0, 0, 330, 0, 0, 440, 0, 494, 0, 466, 440, 0, 392, 659, 784, 880, 0, 698, 784, 0, 659, 0, 523, 587, 494, 0, 0 };
        const int dMain[] = { 1, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 2, 1, 1, 2, 1, 1, 2, 1, 1, 2, 1, 1, 2, 1, 2, 1, 1, 2, 1, 1, 1, 1, 2, 1, 1, 2, 1, 2, 1, 1, 1, 2, 1, 1, 2, 1, 1, 2, 1, 1, 2, 1, 1, 2, 1, 2, 1, 1, 2, 1, 1, 1, 1, 2, 1, 1, 2, 1, 2, 1, 1, 1, 2, 1, 1 };
        playMusicChunk(nMain, dMain, sizeof(nMain) / sizeof(nMain[0]), tempo);
        unsigned long pauseStart = millis();
        while (millis() - pauseStart < 800) {
          delay(1);
          yield();
        }
        marioPhase++;
        break;
      }
    case 1:
      {
        static bool playVictoryNext = true; // Remembers what to play!
        
        if (playVictoryNext) {
          const int nVic[] = { 392, 523, 659, 784, 1047, 1319, 1568, 1319, 415, 523, 622, 831, 1047, 1245, 1661, 1245, 466, 587, 698, 932, 1175, 1397, 1865, 1865, 1865, 2093 };
          const int dVic[] = { 1, 1, 1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 4 };
          playMusicChunk(nVic, dVic, sizeof(nVic) / sizeof(nVic[0]), 110);
        } else {
          const int nDeath[] = { 523, 554, 587, 0, 0, 494, 698, 698, 698, 659, 587, 523, 330, 330, 262 };
          const int dDeath[] = { 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 2, 1, 1, 4 };
          playMusicChunk(nDeath, dDeath, sizeof(nDeath) / sizeof(nDeath[0]), 140);
        }
        
        playVictoryNext = !playVictoryNext; // Flip the switch for next time
        marioPhase++;
        break;
      }
    case 2:
      {
        allOff();
        marioPhase = 0;
        break;
      }
  }
}

void runHighwayRunMode() {
  if (!highwayRunState) return;
  static float speedKmh = 0;
  static float rpm = 800;
  static int gear = 1;
  static int driveState = 0;
  static unsigned long lastUpdate = 0;
  static unsigned long lastStateChange = 0;
  static bool trafficActive = false;
  static bool rainMode = false;
  static bool nightMode = false;
  static unsigned long envTimer = 0;
  static int lastFreq = 0;

  if (forceHighwayStart) {
    forceHighwayStart = false;

    // 🚨 THE HIGHWAY LAUNCH SEQUENCE 🚨
    auto tempOff = []() { setAllHead(0); setAllBrake(0); setUnder(0); };
    tempOff(); stopTone(); smartDelay(500);

    // Temporarily trick the system into thinking Car Mode is active so the theme plays
    bool oldCarMode = carModeState;
    carModeState = true;  
    
    // Play Theme 23 ("RACE COUNTDOWN"): 3 Red Beeps, 1 Huge Green/White "GO!"
    executeTheme(23); 

    // Restore the system lock
    carModeState = oldCarMode;  
    stopTone();
    // -----------------------------------

    driveState = 2; // Instantly start accelerating
    speedKmh = 60;
    lastStateChange = millis();
    lastUpdate = millis(); // Reset the timer so the math doesn't glitch from the delay
  }
  
  if (millis() - lastUpdate < 50) return;
  lastUpdate = millis();

  if (millis() - envTimer > 20000) {
    envTimer = millis();
    rainMode = random(0, 100) < 30;
    nightMode = random(0, 100) < 50;
  }
  if (millis() - lastStateChange > random(4000, 8000)) {
    lastStateChange = millis();
    int r = random(0, 100);
    if (!trafficActive && r > 80) {
      trafficActive = true;
      driveState = 3;
    } else if (trafficActive) {
      trafficActive = false;
      driveState = 2;
    } else {
      driveState = (r < 60) ? 0 : 1;
    }
  }

  if (driveState == 3) {
    setAllBrake(255);
    setUnder(50);
    playTone(1600);
    if (smartDelay(200)) return;
    stopTone();
    lastFreq = 0;
    for (int i = 0; i < 4; i++) {
      setAllBrake(0);
      if (smartDelay(40)) return;
      setAllBrake(255);
      playTone(120);
      if (smartDelay(40)) return;
      stopTone();
    }
    driveState = 0;
    speedKmh -= 30;
    lastFreq = 0;
  } else if (driveState == 2) {
    for (int b = 0; b < 2; b++) {
      setHead(255, 180);
      setBrake(255, 40);
      playTone(600);
      if (smartDelay(100)) return;
      setHead(180, 180);
      setBrake(40, 40);
      stopTone();
      if (smartDelay(100)) return;
    }
    setUnder(255);
    for (int f = 300; f < 850; f += 50) {
      playTone(f);
      if (smartDelay(30)) return;
    }
    driveState = 1;
    speedKmh += 25;
    lastFreq = 0;
  }

  if (driveState == 1) speedKmh += (rainMode ? 0.4 : 0.9);
  else speedKmh += (random(-2, 3) * 0.1);
  speedKmh = constrain(speedKmh, 30, 130);

  if (speedKmh < 45) gear = 2;
  else if (speedKmh < 70) gear = 3;
  else if (speedKmh < 95) gear = 4;
  else gear = 5;
  rpm = (speedKmh * (50 / gear)) + 600;

  int head = map(speedKmh, 0, 130, nightMode ? 150 : 120, 255);
  int under = map(rpm, 800, 6000, 50, 255);
  
  if (headlightState) setAllHead(head); else setAllHead(0);
  if (brakeState) setAllBrake(40); else setAllBrake(0);
  if (underbodyState) setUnder(rainMode && random(0, 15) == 0 ? random(20, 80) : under); else setUnder(0);

  int freq = map(rpm, 800, 6000, 140, 850);
  if (abs(freq - lastFreq) > 5) {
    playTone(freq);
    lastFreq = freq;
  }
}

void runIdleAnimation() {
  static unsigned long lastUpdate = 0;
  static unsigned long lastEventTime = millis();
  static unsigned long lastTwitchTime = millis();
  static unsigned long lastStateChange = millis();
  static unsigned long lastMoodChange = millis();
  
  static unsigned long eventInterval = 300000;  // 5 to 15 mins for the 80 themes
  static unsigned long twitchInterval = 45000;  

  /// --- 🌍 1. THE GLOBAL MOOD SYSTEM ---
  static int currentMood = 0; // 0 = Normal, 1 = Aggressive, 2 = Sleepy
  if (millis() - lastMoodChange > 3600000) { // Shifts every 1 hour!
    currentMood = random(0, 3);
    lastMoodChange = millis();
  }

  /// --- 🧠 2. THE SMART MEMORY DECKS (Perfect Non-Repeating Math) ---
  static int headDeck[30]; static int headIndex = 30; 
  static int brakeDeck[20]; static int brakeIndex = 20;

  static int currentHead = 0, nextHead = 0;
  static int currentBrake = 0, nextBrake = 0;
  
  static bool isCrossfading = false;
  static unsigned long crossfadeStart = 0;

  static float phase = 0;
  float baseSpeed = 0.012;
  if (currentMood == 1) baseSpeed = 0.018; // Aggressive breathes faster
  if (currentMood == 2) baseSpeed = 0.006; // Sleepy breathes much slower

  if (millis() - lastUpdate > 100) { phase += baseSpeed; } 

  // --- 🔀 THE CROSSFADE TRIGGER (Every 1 to 2 minutes) ---
  if (millis() - lastStateChange > random(60000, 120000)) {
    lastStateChange = millis();
    
    // Shuffle Headlights (30 Cards)
    if (headIndex >= 30) {
      for (int i = 0; i < 30; i++) headDeck[i] = i;
      for (int i = 29; i > 0; i--) { int j = random(0, i + 1); int t = headDeck[i]; headDeck[i] = headDeck[j]; headDeck[j] = t; }
      headIndex = 0;
    }
    nextHead = headDeck[headIndex++];
    
    // Shuffle Brakes (20 Cards)
    if (brakeIndex >= 20) {
      for (int i = 0; i < 20; i++) brakeDeck[i] = i;
      for (int i = 19; i > 0; i--) { int j = random(0, i + 1); int t = brakeDeck[i]; brakeDeck[i] = brakeDeck[j]; brakeDeck[j] = t; }
      brakeIndex = 0;
    }
    nextBrake = brakeDeck[brakeIndex++];
    
    // Start the 5-second Morphing Transition!
    isCrossfading = true;
    crossfadeStart = millis();
  }

  // --- 🧮 THE MATHEMATICAL ENGINE (Runs 30x a second) ---
  if (millis() - lastUpdate > 30) {
    lastUpdate = millis();
    phase += baseSpeed;
    if (phase > 6.28318) phase = 0;

    int minHead = (currentMood == 2) ? 10 : 40;   
    int maxSwing = (currentMood == 2) ? 100 : 215; // Sleepy mood limits maximum brightness

    // HELPER 1: The Perfect Circle (Sine Wave)
    auto sineWave = [](float p, float offset) { return (sin(p + offset) + 1.0) / 2.0; };
    
    // HELPER 2: 1D Simulated Perlin Noise (Fractal Brownian Motion for smoke/plasma)
    auto pNoise = [](float p) { return ((sin(p) + 0.5*sin(p*2.31) + 0.25*sin(p*4.73)) / 1.75 + 1.0) / 2.0; };

    // THE CALCULATION MATRIX (We put this in a lambda so we can run it twice for the crossfade)
    auto calcIdleLights = [&](int hState, int bState, float p, float &outHL, float &outHR, float &outBL, float &outBR, float &outUV) {
      // -- HEADLIGHT CALCS --
      if (hState <= 1)      { outHL = minHead + (maxSwing * sineWave(p, 0)); outHR = minHead + (maxSwing * sineWave(p, 3.14)); } // 180 Pan
      else if (hState <= 3) { outHL = minHead + (maxSwing * sineWave(p, 0)); outHR = minHead + (maxSwing * sineWave(p, 1.57)); } // 90 Swirl
      else if (hState <= 5) { outHL = minHead + (maxSwing * sineWave(p*1.3, 0)); outHR = minHead + (maxSwing * sineWave(p*1.3, 3.14)); } // Fast Pan
      else if (hState <= 7) { outHL = minHead + (maxSwing * sineWave(p, 0)); outHR = minHead + (maxSwing * sineWave(p, 0.785)); } // 45 Shimmer
      else if (hState <= 9) { float s = pow(sineWave(p*0.3, 0), 12); outHL = 60 + (195*s); outHR = outHL; } // High-Beam Surge
      else if (hState <= 11){ outHL = minHead + (maxSwing * pow(sineWave(p*3.0, 0), 8)); outHR = outHL; } // Heartbeat
      else if (hState <= 13){ outHL = minHead + (maxSwing * sineWave(p*0.8, 0)); outHR = minHead + (maxSwing * sineWave(p*1.5, 0)); } // Asymmetric
      else if (hState <= 15){ outHL = minHead + (maxSwing * pow(sineWave(p*0.15, 0), 14)); outHR = outHL; } // Lighthouse
      else if (hState <= 17){ outHL = (sin(p*6.0) > 0) ? 255 : minHead; outHR = (sin(p*6.0) < 0) ? 255 : minHead; } // Strobe
      else if (hState <= 19){ outHL = minHead + ((maxSwing/2.5) * sineWave(p*4.0, 0)); outHR = minHead + ((maxSwing/2.5) * sineWave(p*4.0, 3.14)); } // Panting
      else if (hState <= 21){ outHL = minHead + (maxSwing * pow(sineWave(p, 0), 12)); outHR = minHead + (maxSwing * pow(sineWave(p, 0.8), 12)); } // Predator
      else if (hState <= 23){ outHL = minHead + (maxSwing * pNoise(p*2.0)); outHR = minHead + (maxSwing * pNoise(p*2.0 + 3.14)); } // ☁️ THE NEW PERLIN NOISE SMOKE!
      else if (hState <= 25){ outHL = minHead + (maxSwing * pNoise(p*5.0)); outHR = minHead + (maxSwing * pNoise(p*5.0 + 1.5)); } // ⚡ THE NEW PERLIN PLASMA!
      else if (hState <= 27){ outHL = minHead + (maxSwing * sineWave(p, 0)); outHR = minHead + (40 * sineWave(p*0.5, 0)); } // Lazy Eye
      else                  { outHL = 10 + (60 * sineWave(p*0.4, 0)); outHR = outHL; } // Deep Hibernation

      // -- BRAKE CALCS (Perfectly synced to their specific headlight logic) --
      if (bState == 0)      { outBL = map(outHL, minHead, 255, 180, 20); outBR = map(outHR, minHead, 255, 180, 20); } // See-Saw
      else if (bState == 1) { outBL = map(outHR, minHead, 255, 30, 200); outBR = map(outHL, minHead, 255, 30, 200); } // Diagonal
      else if (bState == 2) { outBL = (outHL > 220) ? 255 : 30; outBR = (outHR > 220) ? 255 : 30; } // Heavy Foot
      else if (bState == 3) { outBL = 20 + (180 * sineWave(p, -1.0)); outBR = 20 + (180 * sineWave(p, 2.14)); } // Trailing Ghost
      else if (bState == 4) { outBL = map((outHL+outHR)/2, minHead, 255, 150, 20); outBR = outBL; } // Power Draw
      else if (bState == 5) { outBL = 20 + (25 * sineWave(p*0.2, 0)); outBR = outBL; } // Hot Carbon
      else if (bState == 6) { outBL = (sin(p*4.0) > 0) ? 255 : 20; outBR = outBL; } // Hazard Flash
      else if (bState == 7) { outBL = (sin(p*5.0) > 0) ? 200 : 20; outBR = (sin(p*5.0) < 0) ? 200 : 20; } // Alt Warning
      else if (bState == 8) { outBL = outHL; outBR = outHR; } // Direct Mirror
      else if (bState == 9) { outBL = (random(0,100)>85) ? 255 : 30; outBR = outBL; } // F1 Rain
      else if (bState == 10){ outBL = 20 + (235 * pow(sineWave(p*0.6, 0), 30)); outBR = outBL; } // Phantom Pulse
      else if (bState == 11){ outBL = 20 + (150 * sineWave(p*1.5, 0)); outBR = 20 + (150 * sineWave(p*1.5, 3.14)); } // Alt Ghost
      else if (bState == 12){ outBL = (sin(p*2.5) > 0.8) ? random(150,255) : 30; outBR = outBL; } // Nervous Jitters
      else if (bState == 13){ outBL = map(outHL, minHead, 255, 30, 200); outBR = 20; } // Left Awake Only
      else if (bState == 14){ outBL = 20; outBR = map(outHR, minHead, 255, 30, 200); } // Right Awake Only
      else if (bState == 15){ outBL = 10 + (20 * sineWave(p*0.3, 0)); outBR = outBL; } // Deep Sleep
      else if (bState == 16){ outBL = 20 + (200 * pow(sineWave(p*3.0, 0), 8)); outBR = outBL; } // Heartbeat Brake
      else if (bState == 17){ outBL = map(outHR, minHead, 255, 20, 200); outBR = map(outHL, minHead, 255, 20, 200); } // Cross-Wired
      else if (bState == 18){ outBL = (outHL > 150) ? 20 : 255; outBR = (outHR > 150) ? 20 : 255; } // Opposite Extremes
      else                  { outBL = 40 + random(0,80) + (100 * sineWave(p*8.0, 0)); outBR = outBL; } // Engine Shake

      outUV = 80 + (60 * pNoise(p*0.5)) + random(-2, 3); // Underglow now uses smooth Perlin noise!
    };

    // --- APPLY THE CROSSFADE MATRIX ---
    float hL_A, hR_A, bL_A, bR_A, uV_A;
    calcIdleLights(currentHead, currentBrake, phase, hL_A, hR_A, bL_A, bR_A, uV_A);

    int final_hL, final_hR, final_bL, final_bR, final_uV;

    if (isCrossfading) {
      float blend = (millis() - crossfadeStart) / 5000.0; // 5-second mathematical morph!
      if (blend >= 1.0) {
        blend = 1.0;
        isCrossfading = false;
        currentHead = nextHead;
        currentBrake = nextBrake;
      }
      float hL_B, hR_B, bL_B, bR_B, uV_B;
      calcIdleLights(nextHead, nextBrake, phase, hL_B, hR_B, bL_B, bR_B, uV_B);
      
      final_hL = (int)(hL_A * (1.0 - blend) + hL_B * blend);
      final_hR = (int)(hR_A * (1.0 - blend) + hR_B * blend);
      final_bL = (int)(bL_A * (1.0 - blend) + bL_B * blend);
      final_bR = (int)(bR_A * (1.0 - blend) + bR_B * blend);
      final_uV = (int)(uV_A * (1.0 - blend) + uV_B * blend);
    } else {
      final_hL = (int)hL_A; final_hR = (int)hR_A; final_bL = (int)bL_A; final_bR = (int)bR_A; final_uV = (int)uV_A;
    }

    if (headlightState) setHead(final_hL, final_hR); else setAllHead(0);
    if (brakeState) setBrake(final_bL, final_bR); else setAllBrake(0);
    if (underbodyState) setUnder(final_uV); else setUnder(0);
  }

  // --- ⚡ 3. CONTEXT-AWARE TWITCHES (Weighted Probability) ---
  if (millis() - lastTwitchTime > twitchInterval && (brakeState || headlightState || underbodyState)) {
    lastTwitchTime = millis();
    twitchInterval = random(30000, 120000);
    
    // Check Context: Is the car doing a dark/sleepy animation?
    bool isSleepyState = (currentHead >= 28 || currentMood == 2);
    
    int twitchRoll = random(0, 100);

    if (isSleepyState) {
      // SLEEPY SHIELD: Only allow gentle underglow flickers
      if (underbodyState) { setUnder(40); smartDelay(30); setUnder(10); smartDelay(40); }
    } 
    else {
      // NORMAL SYSTEM: Weighted Probability!
      if (twitchRoll < 60 && brakeState) {  
        // 60% Chance: Brakes
        setAllBrake(255); smartDelay(100);
      } 
      else if (twitchRoll < 85 && underbodyState) {  
        // 25% Chance: Underglow
        setUnder(255); smartDelay(20); setUnder(50); smartDelay(20);
      } 
      else if (twitchRoll < 95 && headlightState && brakeState && underbodyState) {  
        // 10% Chance: The "Voltage Drop" Complex Twitch!
        setUnder(20); setAllBrake(20); setAllHead(0); smartDelay(40);
        setAllHead(255); smartDelay(20); setAllHead(0); smartDelay(30);
      } 
      else if (headlightState) {
        // 5% Chance: Ultra-rare High Beam Flicker
        setHead(40, 255); smartDelay(150); setAllHead(255);
      }
    }
  }

  // --- 🎬 4. CINEMATIC MICRO-EVENTS (The 80-Theme Guaranteed Memory Deck) ---
  if (millis() - lastEventTime > eventInterval && (brakeState || underbodyState || headlightState)) {
    lastEventTime = millis();
    eventInterval = random(300000, 900000); // Happens every 5 to 15 mins
    
    // PERFECT NON-REPEATING MEMORY ENFORCEMENT
    static int idleThemeDeck[80];
    static int idleThemeIndex = 80; 

    if (idleThemeIndex >= 80) {
      for (int i = 0; i < 80; i++) idleThemeDeck[i] = i;
      for (int i = 79; i > 0; i--) {
        int j = random(0, i + 1);
        int temp = idleThemeDeck[i]; idleThemeDeck[i] = idleThemeDeck[j]; idleThemeDeck[j] = temp;
      }
      idleThemeIndex = 0;
    }
    int currentMicroTheme = idleThemeDeck[idleThemeIndex++];
    
    // Prep the system
    auto tempDim = []() { setAllHead(40); setAllBrake(20); setUnder(20); };
    tempDim(); stopTone(); smartDelay(500);

    // Signature Heartbeat Audio
    setUnder(100); playToneDur(150, 60); smartDelay(100); tempDim(); smartDelay(100);
    setUnder(100); playToneDur(150, 60); smartDelay(100); tempDim(); smartDelay(500);

    bool oldCarMode = carModeState;
    bool oldSound = globalSoundEnabled; 

    carModeState = true;  
    globalSoundEnabled = false; // Mute the buzzer so it's a stealthy visual event!

    // Play the mathematically guaranteed, non-repeated theme!
    executeTheme(currentMicroTheme); 

    // Restore reality exactly as it was
    carModeState = oldCarMode;  
    globalSoundEnabled = oldSound; 
    
    stopTone();
    restoreLights(); 
    lastUpdate = 0; // Force the math engine to recalculate smoothly on the next tick
  }
}

void runOverloadAnimation() {
  if (!pcOverloadState) return;
  setAllHead(0);
  setUnder(100);
  setAllBrake(255);
  playToneDur(2500, 30);
  if (smartDelay(60)) return;
  setAllBrake(0);
  if (smartDelay(60)) return;
}