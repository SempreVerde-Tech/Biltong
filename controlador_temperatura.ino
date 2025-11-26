/*
  Controlador de Temperatura com DHT22 e Matriz de LED (Timer Interrupt)

  Versão Profissional:
  - Utiliza uma interrupção de hardware (Timer1) para a atualização da matriz
    de LED, eliminando qualquer oscilação (flicker).
  - A imagem no display é perfeitamente estável, independente de outras
    tarefas como a leitura do sensor.
  - Mantém o controle manual de baixo nível via shiftOut.
*/

// --- Bibliotecas ---
#include <Adafruit_Sensor.h>
#include <DHT.h>

// --- Definições de Pinos ---
#define DHT_PIN         5
#define MOSFET_PIN      2
#define UP_BUTTON_PIN   3
#define DOWN_BUTTON_PIN 4
#define DATA_PIN        11
#define CLOCK_PIN       13
#define LATCH_PIN       10

// --- Constantes do Sistema ---
#define DHTTYPE         DHT22
#define SENSOR_READ_INTERVAL 2000
#define DISPLAY_TIMEOUT 10000
#define DEBOUNCE_DELAY  50

// --- Instanciação dos Objetos ---
DHT dht(DHT_PIN, DHTTYPE);

// --- Variáveis Globais ---
float currentTemperature = 0.0;
int userSetTemperature = 25;
bool displayShowsSetTemp = false;
unsigned long lastInteractionTime = 0;
unsigned long lastSensorReadTime = 0;
bool lastUpButtonState = LOW;
bool lastDownButtonState = LOW;
unsigned long lastDebounceTime = 0;

// --- Lógica da Matriz de LED ---
// Use 'volatile' para variáveis usadas dentro e fora de uma ISR
volatile byte displayBuffer[8] = {0};
volatile int currentColumn = 0;

// Fonte de caracteres (ajustada para ficar um pouco mais para cima)
const byte font[10][3] = {
  { B00111110, B00100010, B00111110 }, // 0
  { B00010000, B00111110, B00000000 }, // 1
  { B00110010, B00101010, B00100100 }, // 2
  { B00100010, B00101010, B00111100 }, // 3
  { B00001100, B00001000, B00111110 }, // 4
  { B00101110, B00101010, B00111010 }, // 5
  { B00111100, B00101010, B00111000 }, // 6
  { B00000110, B00000100, B00111100 }, // 7
  { B00111110, B00101010, B00111110 }, // 8
  { B00101110, B00101010, B00111110 }  // 9
};

// --- Funções de Configuração ---

void setup() {
  Serial.begin(9600);
  pinMode(MOSFET_PIN, OUTPUT);
  pinMode(UP_BUTTON_PIN, INPUT);
  pinMode(DOWN_BUTTON_PIN, INPUT);
  digitalWrite(MOSFET_PIN, LOW);

  pinMode(DATA_PIN, OUTPUT);
  pinMode(CLOCK_PIN, OUTPUT);
  pinMode(LATCH_PIN, OUTPUT);

  dht.begin();
  readSensor(); // Leitura inicial

  setupTimer1(); // Configura a interrupção para o display
}

void setupTimer1() {
  cli(); // Desabilita interrupções globais para configurar

  // Configura o Timer1 para disparar a cada 1ms
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1  = 0;

  OCR1A = 1999; // Compare Match Register (16MHz/8/1000Hz - 1) = 1999
  TCCR1B |= (1 << WGM12); // Modo CTC
  TCCR1B |= (1 << CS11);  // Prescaler de 8

  TIMSK1 |= (1 << OCIE1A); // Habilita a interrupção por comparação

  sei(); // Habilita interrupções globais
}

// --- Rotina de Serviço de Interrupção (ISR) ---
// Esta função é chamada AUTOMATICAMENTE pelo hardware a cada 1ms

ISR(TIMER1_COMPA_vect) {
  // Desliga todos os LEDs para evitar "ghosting"
  digitalWrite(LATCH_PIN, LOW);
  shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, 0xFF); // Todas as linhas OFF
  shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, 0x00); // Todas as colunas OFF
  digitalWrite(LATCH_PIN, HIGH);

  // Avança para a próxima coluna
  currentColumn++;
  if (currentColumn >= 8) {
    currentColumn = 0;
  }

  // Ativa a coluna correta e envia os dados da linha
  byte rowData = displayBuffer[currentColumn];
  byte colData = 1 << (7 - currentColumn);

  digitalWrite(LATCH_PIN, LOW);
  shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, ~rowData);
  shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, colData);
  digitalWrite(LATCH_PIN, HIGH);
}


// --- Loop Principal ---

void loop() {
  handleButtons();
  readSensor();
  controlMosfet();
  updateDisplay(); // Apenas prepara o buffer, não desenha
}


// --- Funções Auxiliares ---

void prepareDisplayBuffer(int number) {
  byte tempBuffer[8] = {0}; // Buffer temporário

  if (number > 99) number = 99;
  if (number < 0) number = 0;

  if (number >= 10) {
    int tens = number / 10;
    int ones = number % 10;
    for (int i = 0; i < 3; i++) tempBuffer[i+1] = font[tens][i];
    for (int i = 0; i < 3; i++) tempBuffer[i+5] = font[ones][i];
  } else {
    int ones = number % 10;
    for (int i = 0; i < 3; i++) tempBuffer[i+2] = font[ones][i];
  }

  // Copia os dados para o buffer volátil de forma segura
  noInterrupts(); // Desabilita interrupções para a cópia
  for (int i=0; i<8; i++) {
    displayBuffer[i] = tempBuffer[i];
  }
  interrupts(); // Reabilita interrupções
}

void updateDisplay() {
  if (displayShowsSetTemp && (millis() - lastInteractionTime > DISPLAY_TIMEOUT)) {
    displayShowsSetTemp = false;
  }
  int tempToDisplay = displayShowsSetTemp ? userSetTemperature : round(currentTemperature);
  prepareDisplayBuffer(tempToDisplay);
}

void handleButtons() {
  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    bool upState = digitalRead(UP_BUTTON_PIN);
    bool downState = digitalRead(DOWN_BUTTON_PIN);

    if (upState != lastUpButtonState && upState == HIGH) {
      userSetTemperature++;
      lastInteractionTime = millis();
      displayShowsSetTemp = true;
      lastDebounceTime = millis();
    }
    if (downState != lastDownButtonState && downState == HIGH) {
      userSetTemperature--;
      lastInteractionTime = millis();
      displayShowsSetTemp = true;
      lastDebounceTime = millis();
    }
    lastUpButtonState = upState;
    lastDownButtonState = downState;
  }
}

void readSensor() {
  if (millis() - lastSensorReadTime >= SENSOR_READ_INTERVAL) {
    lastSensorReadTime = millis();
    float t = dht.readTemperature();
    if (!isnan(t)) {
      currentTemperature = t;
    }
  }
}

void controlMosfet() {
  if (currentTemperature > 0 && currentTemperature < userSetTemperature) {
    digitalWrite(MOSFET_PIN, HIGH);
  } else {
    digitalWrite(MOSFET_PIN, LOW);
  }
}
