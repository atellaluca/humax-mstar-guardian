/*
 * Humax / MStar Guardian
 * ESP32-WROOM-32
 *
 * Host USB: 921600 baud
 * Target:   115200 8N1
 * RX2 GPIO16 <- target TX
 * TX2 GPIO17 -> target RX
 * Relay GPIO25, active LOW
 *
 * Deny-by-default: only exact commands listed below can reach the target.
 * No SPI erase/write command is present.
 */

#include <Arduino.h>

constexpr uint32_t PC_BAUD = 921600;
constexpr uint32_t TARGET_BAUD = 115200;
constexpr int TARGET_RX = 16;
constexpr int TARGET_TX = 17;
constexpr int RELAY_PIN = 25;
constexpr uint8_t RELAY_ON = LOW;
constexpr uint8_t RELAY_OFF = HIGH;

bool powerOn = false;
bool armed = false;
bool hunterFired = false;
bool consoleReady = false;
String hostLine;
char pattern[128];
size_t patternLen = 0;

enum class CommandClass : uint8_t { INSPECT, READ_DEVICE, READ_USB, WRITE_USB };

struct AllowedCommand {
  const char* text;
  CommandClass kind;
};

// Exact-match allowlist. Target-specific addresses and commands.
const AllowedCommand ALLOWLIST[] = {
  {"help", CommandClass::INSPECT},
  {"?", CommandClass::INSPECT},
  {"version", CommandClass::INSPECT},
  {"printenv", CommandClass::INSPECT},
  {"bdinfo", CommandClass::INSPECT},
  {"coninfo", CommandClass::INSPECT},
  {"mversion", CommandClass::INSPECT},
  {"showversion", CommandClass::INSPECT},
  {"systeminfo_print", CommandClass::INSPECT},
  {"loaderinfo_print", CommandClass::INSPECT},
  {"read_boot_info", CommandClass::INSPECT},
  {"help spi", CommandClass::INSPECT},
  {"help crc32", CommandClass::INSPECT},
  {"help fatwrite", CommandClass::INSPECT},
  {"help fatload", CommandClass::INSPECT},
  {"usb start", CommandClass::INSPECT},
  {"usb start 0", CommandClass::INSPECT},
  {"usb start 1", CommandClass::INSPECT},
  {"usb tree", CommandClass::INSPECT},
  {"usb info", CommandClass::INSPECT},
  {"usb storage", CommandClass::INSPECT},
  {"usb dev", CommandClass::INSPECT},
  {"usb dev 0", CommandClass::INSPECT},
  {"usb part", CommandClass::INSPECT},
  {"fatinfo usb 0", CommandClass::READ_USB},
  {"fatinfo usb 0:1", CommandClass::READ_USB},
  {"fatls usb 0", CommandClass::READ_USB},
  {"fatls usb 0:1", CommandClass::READ_USB},
  {"spi info", CommandClass::INSPECT},
  {"spi rdc 0x81000000 0x00000000 0x00800000", CommandClass::READ_DEVICE},
  {"crc32 0x81000000 0x00800000", CommandClass::READ_DEVICE},
  {"fatwrite usb 0 0x81000000 firmware.bin 0x00800000", CommandClass::WRITE_USB},
  {"fatload usb 0:1 0x82000000 firmware.bin", CommandClass::READ_USB},
  {"crc32 0x82000000 0x00800000", CommandClass::READ_USB},
};

void resetPattern() {
  patternLen = 0;
  pattern[0] = '\0';
}

void pushPattern(uint8_t b) {
  if (!b) return;
  if (patternLen < sizeof(pattern) - 1) {
    pattern[patternLen++] = static_cast<char>(b);
  } else {
    memmove(pattern, pattern + 1, sizeof(pattern) - 2);
    pattern[sizeof(pattern) - 2] = static_cast<char>(b);
    patternLen = sizeof(pattern) - 1;
  }
  pattern[patternLen] = '\0';
}

const AllowedCommand* findAllowed(const String& input) {
  for (const auto& item : ALLOWLIST) {
    if (input == item.text) return &item;
  }
  return nullptr;
}

const char* className(CommandClass c) {
  switch (c) {
    case CommandClass::INSPECT: return "INSPECT";
    case CommandClass::READ_DEVICE: return "READ_DEVICE";
    case CommandClass::READ_USB: return "READ_USB";
    case CommandClass::WRITE_USB: return "WRITE_USB";
  }
  return "UNKNOWN";
}

void setPower(bool on) {
  digitalWrite(RELAY_PIN, on ? RELAY_ON : RELAY_OFF);
  powerOn = on;
  if (!on) {
    armed = false;
    hunterFired = false;
    consoleReady = false;
    resetPattern();
  }
  Serial.printf("\r\n@@POWER:%s\r\n", on ? "ON" : "OFF");
}

void fireHunter() {
  if (!armed || hunterFired) return;
  hunterFired = true;
  // Critical byte is emitted before host telemetry.
  Serial2.write(static_cast<uint8_t>(0x0D));
  Serial2.flush();
  Serial.println("\r\n@@HUNTER:TX:CR");
}

void analyzeTarget(uint8_t b) {
  pushPattern(b);
  if (armed && !hunterFired && strstr(pattern, "Changelist")) {
    fireHunter();
    Serial.println("@@PATTERN:CHANGELIST");
  }
  if (!consoleReady && strstr(pattern, "k5tn#")) {
    consoleReady = true;
    armed = false;
    Serial.println("\r\n@@CONSOLE:READY:k5tn#");
  }
}

void executeGuarded(String command) {
  command.trim();
  if (!consoleReady) {
    Serial.println("\r\n@@EXEC:BLOCKED:NO_CONSOLE");
    return;
  }
  const AllowedCommand* allowed = findAllowed(command);
  if (!allowed) {
    Serial.println("\r\n@@EXEC:BLOCKED:NOT_WHITELISTED");
    return;
  }
  Serial.printf("\r\n@@EXEC:TX:%s:%s\r\n", className(allowed->kind), command.c_str());
  Serial2.print(command);
  Serial2.write(static_cast<uint8_t>(0x0D));
  Serial2.flush();
}

void processHostCommand(String command) {
  command.trim();
  String upper = command;
  upper.toUpperCase();

  if (upper == "ON") setPower(true);
  else if (upper == "OFF") setPower(false);
  else if (upper == "ARM") {
    resetPattern();
    hunterFired = false;
    consoleReady = false;
    armed = true;
    Serial.println("\r\n@@HUNTER:ARMED");
  } else if (upper == "DISARM") {
    armed = false;
    Serial.println("\r\n@@HUNTER:DISARMED");
  } else if (upper == "STATUS") {
    Serial.printf("\r\n@@STATUS:POWER=%s,ARMED=%s,FIRED=%s,CONSOLE=%s\r\n",
      powerOn ? "ON" : "OFF", armed ? "YES" : "NO",
      hunterFired ? "YES" : "NO", consoleReady ? "YES" : "NO");
  } else if (upper == "CR") {
    if (!consoleReady) {
      Serial.println("\r\n@@CONSOLE:CR_BLOCKED");
      return;
    }
    Serial2.write(static_cast<uint8_t>(0x0D));
    Serial2.flush();
    Serial.println("\r\n@@CONSOLE:CR");
  } else if (upper.startsWith("EXEC ")) {
    executeGuarded(command.substring(5));
  } else if (upper == "HELP") {
    Serial.println("\r\n@@HELP:ON OFF ARM DISARM STATUS CR EXEC");
    Serial.println("@@HELP:EXEC is deny-by-default and exact-match allowlisted");
  } else {
    Serial.println("\r\n@@ERROR:UNKNOWN_COMMAND");
  }
}

void handleHost() {
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') continue;
    if (c == '\n') {
      if (hostLine.startsWith("@")) processHostCommand(hostLine.substring(1));
      else if (hostLine.length()) Serial.println("\r\n@@BLOCKED:USE_@EXEC");
      hostLine = "";
    } else if (hostLine.length() < 200) {
      hostLine += c;
    } else {
      hostLine = "";
      Serial.println("\r\n@@ERROR:HOST_LINE_TOO_LONG");
    }
  }
}

void handleTarget() {
  static uint8_t buf[256];
  while (Serial2.available()) {
    const size_t available = static_cast<size_t>(Serial2.available());
    const size_t n = Serial2.readBytes(buf, min(available, sizeof(buf)));
    if (!n) return;
    for (size_t i = 0; i < n; ++i) analyzeTarget(buf[i]);
    Serial.write(buf, n);
  }
}

void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_OFF);
  Serial.begin(PC_BAUD);
  Serial2.setRxBufferSize(4096);
  Serial2.setTimeout(1);
  Serial2.begin(TARGET_BAUD, SERIAL_8N1, TARGET_RX, TARGET_TX);
  delay(250);
  Serial.println("\r\n@@GUARDIAN:1.0.0:READY");
  Serial.println("@@POWER:OFF");
  Serial.println("@@POLICY:DENY_BY_DEFAULT");
}

void loop() {
  handleHost();
  handleTarget();
}
