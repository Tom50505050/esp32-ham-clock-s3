// POTWIERDZENIE: T_DO jest na GPIO 14. Odczyt pelnych X/Y/Z + sledzenie palca.
//
// Test v4 wykazal, ze GPIO 14 zwraca plynnie zmienne wartosci (0-4095) przy
// ruchu palca, a GPIO 13 tylko szum 0/8191. Ten test potwierdza to odczytem
// X/Y/Z i pokazuje surowe wartosci do kalibracji.
//
// TRZYMAJ I PRZESUWAJ PALEC PO EKRANIE. Sprawdz, czy x/y rosnie w kierunku ruchu.

#include <Arduino.h>

#define T_CS   15
#define T_IRQ  16
#define P_SCLK 12
#define P_MOSI 11
#define P_MISO 14        // <-- T_DO na GPIO 14

static uint8_t bbTransfer(uint8_t b) {
  uint8_t r = 0;
  for (int i = 7; i >= 0; i--) {
    digitalWrite(P_MOSI, (b >> i) & 1);
    delayMicroseconds(1);
    digitalWrite(P_SCLK, HIGH);
    delayMicroseconds(1);
    r = (r << 1) | (digitalRead(P_MISO) & 1);
    digitalWrite(P_SCLK, LOW);
    delayMicroseconds(1);
  }
  return r;
}

static uint16_t bbRead(uint8_t cmd) {
  digitalWrite(T_CS, LOW);
  delayMicroseconds(2);
  bbTransfer(cmd);
  uint8_t hi = bbTransfer(0x00);
  uint8_t lo = bbTransfer(0x00);
  digitalWrite(T_CS, HIGH);
  return ((uint16_t)hi << 8 | lo) >> 3;
}

// Usredniona seria odczytow (mniejszy szum).
static uint16_t bbReadAvg(uint8_t cmd, int n) {
  uint32_t s = 0;
  for (int i = 0; i < n; i++) s += bbRead(cmd);
  return s / n;
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println();
  Serial.println("=== POTWIERDZENIE T_DO = GPIO 14 ===");

  pinMode(P_SCLK, OUTPUT); digitalWrite(P_SCLK, LOW);
  pinMode(P_MOSI, OUTPUT); digitalWrite(P_MOSI, LOW);
  pinMode(T_CS, OUTPUT);   digitalWrite(T_CS, HIGH);
  pinMode(P_MISO, INPUT);
  pinMode(T_IRQ, INPUT_PULLUP);

  Serial.println("Przesuwaj palcem: sprawdz czy x/y zmieniaja sie w kierunku ruchu.\n");
}

void loop() {
  static unsigned long last = 0;
  if (millis() - last < 300) return;
  last = millis();

  int irq = digitalRead(T_IRQ);
  uint16_t x = bbReadAvg(0x90, 8);   // X
  uint16_t y = bbReadAvg(0xD0, 8);   // Y
  uint16_t z1 = bbRead(0xB0);
  uint16_t z2 = bbRead(0xC0);
  int z = (int)z1 + 4095 - (int)z2;

  Serial.printf("T_IRQ=%d  x=%4u y=%4u z=%4d  %s\n",
                irq, x, y, z, (irq == 0) ? "DOTKNIETE" : "");
}
