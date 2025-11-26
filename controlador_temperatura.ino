/*
  Controlador de Temperatura com DHT22 e Matriz de LED (Controle Manual)

  Versão Final:
  - Utiliza controle de baixo nível para a matriz de LED com shiftOut,
    baseado no hardware específico do usuário.
  - Não depende de bibliotecas externas para o display (LedControl, MD_Parola).
  - Mantém a funcionalidade não-bloqueante para sensor e botões.

  Lógica da Matriz (baseado no feedback do usuário):
  - Primeiro byte enviado via shiftOut controla as LINHAS (LOW para ativar).
  - Segundo byte enviado via shiftOut controla as COLUNAS (HIGH para ativar).
*/

// --- Bibliotecas ---
#include <Adafruit_Sensor.h>
#include <DHT.h>

// --- Definições de Pinos ---
#define DHT_PIN         5    // Pino de dados do sensor DHT22
#define MOSFET_PIN      2    // Pino de controle do MOSFET
#define UP_BUTTON_PIN   3    // Pino do botão de aumentar temperatura
#define DOWN_BUTTON_PIN 4    // Pino do botão de reduzir temperatura

// Pinos para a Matriz de LED (74HC595)
#define DATA_PIN        11   // DS
#define CLOCK_PIN       13   // SH_CP
#define LATCH_PIN       10   // ST_CP

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

// --- Lógica da Matriz de LED (Controle Manual) ---
byte displayBuffer[8] = {0}; // 8 colunas, cada uma um byte para as linhas
int currentColumn = 0;
unsigned long lastDisplayRefresh = 0;

// Fonte de caracteres (5 pixels de altura, 3 de largura)
// Os bits representam as linhas que devem estar ACESAS (HIGH)
const byte font[10][3] = {
  { B01111110, B01000010, B01111110 }, // 0
  { B0010000, B0111111, B0010000 }, // 1
  { B0110010, B0101001, B0100110 }, // 2
  { B0100010, B0101001, B0111110 }, // 3
  { B0001100, B0001010, B0111111 }, // 4
  { B0101111, B0101001, B0111001 }, // 5
  { B0111110, B0101001, B0111000 }, // 6
  { B0000011, B0000100, B0111100 }, // 7
  { B0111110, B0101001, B0111110 }, // 8
  { B0001110, B0101001, B0111110 }  // 9
};

// --- Funções Principais ---

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
}

void loop() {
  // Funções não-bloqueantes
  handleButtons();
  readSensor();
  controlMosfet();
  updateDisplay(); // Prepara o buffer
  refreshDisplay(); // Desenha na tela (multiplexação)
}

// --- Funções de Controle da Matriz de LED ---

// Função que envia os bytes para os registradores
void sendTo595(byte rowData, byte colData) {
  digitalWrite(LATCH_PIN, LOW);
  // A lógica do hardware é: linhas são ativadas com LOW. Invertemos os bits.
  shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, ~rowData);
  shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, colData);
  digitalWrite(LATCH_PIN, HIGH);
}

// Prepara o que deve ser mostrado na tela
void prepareDisplayBuffer(int number) {
  // Limpa o buffer antigo
  for (int i = 0; i < 8; i++) displayBuffer[i] = 0;

  // Limita o número para o intervalo que podemos exibir (0-99)
  if (number > 99) number = 99;
  if (number < 0) number = 0; // Simplificado para não exibir negativos

  if (number >= 10) {
    int tens = number / 10;
    int ones = number % 10;
    // Dígito da dezena (colunas 0, 1, 2)
    for (int i = 0; i < 3; i++) displayBuffer[i] = font[tens][i];
    // Dígito da unidade (colunas 4, 5, 6)
    for (int i = 0; i < 3; i++) displayBuffer[i + 4] = font[ones][i];
  } else {
    int ones = number % 10;
    // Dígito único, centralizado (colunas 2, 3, 4)
    for (int i = 0; i < 3; i++) displayBuffer[i + 2] = font[ones][i];
  }
}

// Atualiza uma coluna da matriz (deve ser chamada constantemente)
void refreshDisplay() {
  // A varredura de colunas deve ser rápida. 2ms por coluna está ótimo.
  if (millis() - lastDisplayRefresh > 2) {
    lastDisplayRefresh = millis();

    // Desliga todos os LEDs para evitar "ghosting"
    sendTo595(B00000000, B00000000);

    // Pega os dados da coluna atual
    byte rowData = displayBuffer[currentColumn];
    // Ativa apenas a coluna atual
    byte colData = 1 << (7 - currentColumn);

    sendTo595(rowData, colData);

    // Avança para a próxima coluna
    currentColumn++;
    if (currentColumn >= 8) {
      currentColumn = 0;
    }
  }
}

// --- Funções do Sistema ---

void updateDisplay() {
  if (displayShowsSetTemp && (millis() - lastInteractionTime > DISPLAY_TIMEOUT)) {
    displayShowsSetTemp = false;
  }

  int tempToDisplay = displayShowsSetTemp ? userSetTemperature : round(currentTemperature);
  prepareDisplayBuffer(tempToDisplay);
}

void handleButtons() {
  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    bool upButtonState = digitalRead(UP_BUTTON_PIN);
    bool downButtonState = digitalRead(DOWN_BUTTON_PIN);

    if (upButtonState != lastUpButtonState && upButtonState == HIGH) {
      userSetTemperature++;
      lastInteractionTime = millis();
      displayShowsSetTemp = true;
      lastDebounceTime = millis();
    }

    if (downButtonState != lastDownButtonState && downButtonState == HIGH) {
      userSetTemperature--;
      lastInteractionTime = millis();
      displayShowsSetTemp = true;
      lastDebounceTime = millis();
    }

    lastUpButtonState = upButtonState;
    lastDownButtonState = downButtonState;
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
