#include <MIDI.h>
#include <JC_Button.h>

const int PIN_LED = 13;
const int PIN_START = 5;
const int PIN_RX = 16;
const int PIN_TX = 17;

const uint8_t MIDI_CH = 2;

Button startButton(PIN_START);

volatile int bpm = 120;
unsigned long lastClockTime = 0;
int pulseCount = 0;

bool isPlaying = false;

unsigned long ledOnTime = 0;
bool ledState = false;

HardwareSerial MIDIserial(2);
MIDI_CREATE_INSTANCE(HardwareSerial, MIDIserial, midiA);

void setup() {
  
  MIDIserial.begin(31250, SERIAL_8N1, PIN_RX, PIN_TX);
  midiA.begin(MIDI_CHANNEL_OMNI);

  startButton.begin();
  Serial.begin(115200);
  Serial.println("Start");
  pinMode(PIN_LED, OUTPUT);
}

void loop() {
  
  startButton.read();
  if (startButton.wasReleased()) {
    Serial.println("Pressed");
    isPlaying = !isPlaying;
    if (isPlaying) {
      midiA.sendRealTime(midi::Start);
      pulseCount = 0;
      lastClockTime = millis();
      Serial.println("MIDI Start");
    } else {
      midiA.sendRealTime(midi::Stop);
      pulseCount = 0;
      lastClockTime = millis();
      Serial.println("MIDI Stop");
    }
  }

  // Send MIDI clock
  if (isPlaying) {
    unsigned long interval = 60000UL / (bpm * 24);
    if (millis() - lastClockTime >= interval) {
      lastClockTime += interval;
      midiA.sendRealTime(midi::Clock);

      // Count pulse
      pulseCount++;
      if (pulseCount >= 24) {
        pulseCount = 0;
        digitalWrite(PIN_LED, HIGH);
        ledState = true;
        ledOnTime = millis();
        Serial.println("4th note");
      }
      Serial.println(pulseCount);
    }
  }

  // Turn off LED
  if (ledState && millis() - ledOnTime >= 50) {
    digitalWrite(PIN_LED, LOW);
    ledState = false;
  }
}