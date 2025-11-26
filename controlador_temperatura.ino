/*
  Controlador de Temperatura com DHT22, Botões Touch, MOSFET e Matriz de LED 8x8

  Funcionalidades:
  - Lê dados de umidade e temperatura do sensor DHT22.
  - Permite ao usuário configurar uma temperatura desejada usando dois botões touch.
  - Ativa um módulo MOSFET se a temperatura atual for menor que a configurada.
  - Exibe a temperatura atual ou a temperatura configurada em uma matriz de LED 8x8.
  - O código é não-bloqueante, usando millis() para um funcionamento responsivo.

  Conexões:
  - Sensor DHT22: Pino D5
  - Módulo MOSFET: Pino D2
  - Botão Touch (Aumentar): Pino D3
  - Botão Touch (Reduzir): Pino D4
  - Matriz de LED (via 74HC595):
    - DI (Data In): Pino 11
    - CLK (Clock): Pino 13
    - LAT (Latch / CS): Pino 10
*/

// --- Inclusão das Bibliotecas ---
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <LedControl.h>

// --- Definições de Pinos ---
#define DHT_PIN      5         // Pino de dados do sensor DHT22
#define MOSFET_PIN   2         // Pino de controle do MOSFET
#define UP_BUTTON_PIN 3        // Pino do botão de aumentar temperatura
#define DOWN_BUTTON_PIN 4      // Pino do botão de reduzir temperatura

// Pinos para o controlador da Matriz de LED (74HC595)
#define DI_PIN       11        // Data In
#define CLK_PIN      13        // Clock
#define LAT_PIN      10        // Latch

// --- Constantes do Sistema ---
#define DHTTYPE      DHT22     // Define o tipo do sensor como DHT22
#define SENSOR_READ_INTERVAL 2000 // Intervalo para ler o sensor (2 segundos), o DHT22 é lento
#define DISPLAY_TIMEOUT 10000     // Tempo para a matriz voltar a exibir a temp. atual (10 segundos)
#define DEBOUNCE_DELAY 50         // Atraso para "debounce" dos botões (50 ms)

// --- Instanciação dos Objetos ---
DHT dht(DHT_PIN, DHTTYPE);
// LedControl(dataPin, clockPin, csPin, numDevices)
LedControl lc = LedControl(DI_PIN, CLK_PIN, LAT_PIN, 1); // 1 dispositivo (a matriz 8x8)

// --- Variáveis Globais ---
float currentTemperature = 0.0;
float currentHumidity = 0.0;
int userSetTemperature = 25; // Temperatura alvo inicial (ex: 25°C)

// Variáveis para controle do display
bool displayShowsSetTemp = false;
unsigned long lastInteractionTime = 0;

// Variáveis para leitura do sensor
unsigned long lastSensorReadTime = 0;

// Variáveis para debounce dos botões
bool lastUpButtonState = LOW;
bool lastDownButtonState = LOW;
unsigned long lastDebounceTime = 0;

// --- Mapa de bits para exibir números (formato de coluna) ---
// Cada número é definido por 3 colunas de 5 pixels de altura, centralizadas verticalmente.
// Ex: B00111110 -> LEDs acesos: 1, 2, 3, 4, 5 (de cima para baixo)
const byte font[10][3] = {
  { B00111110, B00100001, B00111110 }, // 0
  { B00010000, B00111111, B00010000 }, // 1
  { B00110010, B00101001, B00100110 }, // 2
  { B00100010, B00101001, B00111110 }, // 3
  { B00001100, B00001010, B00111111 }, // 4
  { B00101111, B00101001, B00111001 }, // 5
  { B00111110, B00101001, B00111000 }, // 6
  { B00000011, B00000100, B00111100 }, // 7
  { B00111110, B00101001, B00111110 }, // 8
  { B00001110, B00101001, B00111110 }  // 9
};
const byte font_minus[2] = {
  B00001000, B00001000 // Duas colunas para o sinal '-' no meio
};


void setup() {
  Serial.begin(9600);
  Serial.println("Iniciando o sistema de controle de temperatura...");

  // Configuração dos pinos
  pinMode(MOSFET_PIN, OUTPUT);
  pinMode(UP_BUTTON_PIN, INPUT);
  pinMode(DOWN_BUTTON_PIN, INPUT);
  digitalWrite(MOSFET_PIN, LOW); // Garante que o MOSFET comece desligado

  // Inicialização do sensor
  dht.begin();

  // Inicialização da Matriz de LED
  lc.shutdown(0, false);      // Acorda o display
  lc.setIntensity(0, 8);      // Define o brilho (0-15)
  lc.clearDisplay(0);         // Limpa o display

  // Faz uma leitura inicial para ter um valor para exibir
  readSensor();
  updateDisplay();
}

void loop() {
  // Funções principais que rodam continuamente de forma não-bloqueante
  handleButtons();
  readSensor();
  controlMosfet();
  updateDisplay();
}

/**
 * @brief Lê os botões com debounce para evitar leituras múltiplas.
 */
void handleButtons() {
  // Verifica se passou tempo suficiente desde o último toque para o debounce
  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    bool upButtonState = digitalRead(UP_BUTTON_PIN);
    bool downButtonState = digitalRead(DOWN_BUTTON_PIN);

    // Lógica para o botão de AUMENTAR
    if (upButtonState != lastUpButtonState && upButtonState == HIGH) {
      userSetTemperature++;
      lastInteractionTime = millis();
      displayShowsSetTemp = true;
      Serial.print("Temperatura configurada: ");
      Serial.println(userSetTemperature);
      lastDebounceTime = millis(); // Reseta o timer do debounce
    }

    // Lógica para o botão de REDUZIR
    if (downButtonState != lastDownButtonState && downButtonState == HIGH) {
      userSetTemperature--;
      lastInteractionTime = millis();
      displayShowsSetTemp = true;
      Serial.print("Temperatura configurada: ");
      Serial.println(userSetTemperature);
      lastDebounceTime = millis(); // Reseta o timer do debounce
    }

    lastUpButtonState = upButtonState;
    lastDownButtonState = downButtonState;
  }
}

/**
 * @brief Lê a temperatura e umidade do sensor em intervalos definidos.
 */
void readSensor() {
  // A leitura só ocorre se o intervalo de tempo for atingido
  if (millis() - lastSensorReadTime >= SENSOR_READ_INTERVAL) {
    lastSensorReadTime = millis(); // Atualiza o tempo da última leitura

    float h = dht.readHumidity();
    float t = dht.readTemperature();

    // Verifica se a leitura falhou (retorna NaN - Not a Number)
    if (isnan(h) || isnan(t)) {
      Serial.println("Falha ao ler o sensor DHT!");
      return;
    }

    currentTemperature = t;
    currentHumidity = h;

    Serial.print(F("Umidade: "));
    Serial.print(currentHumidity);
    Serial.print(F("%  Temperatura: "));
    Serial.print(currentTemperature);
    Serial.println(F("°C"));
  }
}

/**
 * @brief Ativa ou desativa o MOSFET com base na comparação das temperaturas.
 */
void controlMosfet() {
  // Se a temperatura lida for válida (diferente de 0) e menor que a configurada
  if (currentTemperature > 0 && currentTemperature < userSetTemperature) {
    digitalWrite(MOSFET_PIN, HIGH); // Ativa o MOSFET
  } else {
    digitalWrite(MOSFET_PIN, LOW);  // Desativa o MOSFET
  }
}

/**
 * @brief Gerencia o que é exibido na matriz de LED.
 */
void updateDisplay() {
  // Se o display está mostrando a temperatura configurada, verifica o timeout
  if (displayShowsSetTemp && (millis() - lastInteractionTime > DISPLAY_TIMEOUT)) {
    displayShowsSetTemp = false; // Volta a mostrar a temperatura atual
  }

  int tempToDisplay;
  if (displayShowsSetTemp) {
    tempToDisplay = userSetTemperature;
  } else {
    tempToDisplay = round(currentTemperature); // Arredonda para o inteiro mais próximo
  }

  // Chama a função para efetivamente "desenhar" o número na matriz
  displayNumber(tempToDisplay);
}

/**
 * @brief Exibe um número de até 2 dígitos na matriz 8x8 usando setColumn para renderização correta.
 * @param number O número a ser exibido.
 */
void displayNumber(int number) {
  lc.clearDisplay(0);

  // Limita o número para o intervalo que podemos exibir (-9 a 99)
  if (number > 99) number = 99;
  if (number < -9) number = -9;

  if (number >= 10) {
    // --- Número de dois dígitos (10-99) ---
    int tens = number / 10;
    int ones = number % 10;
    // Exibe o dígito das dezenas (3 colunas, começando da coluna 0)
    for (int i = 0; i < 3; i++) {
      lc.setColumn(0, i, font[tens][i]);
    }
    // Deixa uma coluna de espaço (coluna 3)
    // Exibe o dígito das unidades (3 colunas, começando da coluna 4)
    for (int i = 0; i < 3; i++) {
      lc.setColumn(0, i + 4, font[ones][i]);
    }
  } else if (number >= 0) {
    // --- Número de um dígito (0-9) ---
    int ones = number % 10;
    // Centraliza o dígito (começando da coluna 2)
    for (int i = 0; i < 3; i++) {
      lc.setColumn(0, i + 2, font[ones][i]);
    }
  } else {
    // --- Número negativo (-9 a -1) ---
    int ones = abs(number) % 10;
    // Exibe o sinal de menos (2 colunas, começando da coluna 0)
    for (int i = 0; i < 2; i++) {
      lc.setColumn(0, i, font_minus[i]);
    }
    // Deixa uma coluna de espaço (coluna 2)
    // Exibe o dígito (3 colunas, começando da coluna 3)
    for (int i = 0; i < 3; i++) {
      lc.setColumn(0, i + 3, font[ones][i]);
    }
  }
}
