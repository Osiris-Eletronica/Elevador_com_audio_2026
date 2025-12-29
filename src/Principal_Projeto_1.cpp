#include <Arduino.h>
#include <TFT_eSPI.h>
#include <cstdint>
#include "driver/twai.h"
#include "../include/Meu_logo_III.h"

#include <DFRobotDFPlayerMini.h>
#include <HardwareSerial.h>

// ---------------- PINOS ----------------
const int pinBotoes[] = {22, 27, 35};
const int pinTrincos = 17;
const int pinSensorCabina = 25;
const int motorSobe = 4;
const int motorDesce = 16;

// DFPLAYER
#define DF_RX 26
#define DF_TX 32

// ---------------- OBJETOS ----------------
TFT_eSPI tft = TFT_eSPI();
HardwareSerial dfSerial(2);
DFRobotDFPlayerMini dfPlayer;

// ---------------- PWM DISPLAY ----------------
#define TFT_BL_PIN 21
#define LEDC_CHANNEL_0 0
#define LEDC_RESOLUTION 8
#define LEDC_FREQUENCY 5000

// ---------------- CAN BUS ----------------
const gpio_num_t canTxPin = GPIO_NUM_27;
const gpio_num_t canRxPin = GPIO_NUM_35;

// ---------------- VARIÁVEIS ----------------
int andarAtual = 1;
int andarDestino = 1;
bool emMovimento = false;
bool subindo = false;
bool detectouPrimeiroIman = false;
bool sensorUltimoEstado = HIGH;

bool audioExecutado = false;

unsigned long millisPiscar = 0;
bool mostrarNumero = true;

// ---------------- PROTÓTIPOS ----------------
void desenharBackground();
void setupCan();
void enviarStatusAndar(int andar);

// Áudio
void avisarChegada(int andar);
void avisarPortaAberta();
void avisarFechamento();
void avisarAcessibilidade();

// ---------------- FUNÇÕES ----------------
void desenharBackground() {
    tft.pushImage(0, 0, 320, 240, (uint16_t*)Meu_logo_III);
}

// ---------------- SETUP ----------------
void setup() {
    Serial.begin(115200);

    // Display
    tft.init();
    tft.setRotation(1);
    tft.invertDisplay(true);
    tft.setSwapBytes(true);

    ledcSetup(LEDC_CHANNEL_0, LEDC_FREQUENCY, LEDC_RESOLUTION);
    ledcAttachPin(TFT_BL_PIN, LEDC_CHANNEL_0);
    ledcWrite(LEDC_CHANNEL_0, 150);

    desenharBackground();

    // DFPlayer
    dfSerial.begin(9600, SERIAL_8N1, DF_RX, DF_TX);
    if (dfPlayer.begin(dfSerial)) {
        dfPlayer.volume(25);
    }

    setupCan();

    for (int i = 0; i < 3; i++) pinMode(pinBotoes[i], INPUT_PULLUP);
    pinMode(pinTrincos, INPUT_PULLUP);
    pinMode(pinSensorCabina, INPUT_PULLUP);

    pinMode(motorSobe, OUTPUT);
    pinMode(motorDesce, OUTPUT);

    digitalWrite(motorSobe, LOW);
    digitalWrite(motorDesce, LOW);

    tft.setTextColor(TFT_BLUE);
    tft.drawCentreString("OSIRIS ELETRONICA", 160, 10, 2);
    tft.setTextColor(TFT_WHITE);
    tft.drawCentreString("MONTA-PRATO 2026", 160, 27, 2);
}

// ---------------- CAN ----------------
void setupCan() {
    twai_general_config_t g_config =
        TWAI_GENERAL_CONFIG_DEFAULT(canTxPin, canRxPin, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_125KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    twai_driver_install(&g_config, &t_config, &f_config);
    twai_start();
}

void enviarStatusAndar(int andar) {
    twai_message_t msg;
    msg.identifier = 0x100;
    msg.flags = TWAI_MSG_FLAG_NONE;
    msg.data_length_code = 1;
    msg.data[0] = andar;
    twai_transmit(&msg, pdMS_TO_TICKS(100));
}

// ---------------- ÁUDIO ----------------
void avisarChegada(int andar) {
    dfPlayer.play(andar);      // 001,002,003...
    delay(1800);
}

void avisarPortaAberta() {
    dfPlayer.play(10);         // Porta aberta
    delay(1500);
}

void avisarAcessibilidade() {
    dfPlayer.play(40);
    delay(2500);
}

void avisarFechamento() {
    dfPlayer.play(11);         // "Porta fechará em dez segundos"
    delay(2000);

    for (int i = 0; i < 10; i++) {
        dfPlayer.play(20 + i); // Contagem 10..1
        delay(1000);
    }

    dfPlayer.play(30);         // Porta fechando
}

// ---------------- LOOP ----------------
void loop() {
    unsigned long agora = millis();

    // ---------------- PORTA ABERTA ----------------
    if (digitalRead(pinTrincos) == HIGH) {
        digitalWrite(motorSobe, LOW);
        digitalWrite(motorDesce, LOW);
        emMovimento = false;

        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.drawCentreString("PORTA ABERTA - BLOQUEADO", 160, 210, 2);

        // Executa áudio UMA VEZ por parada
        if (!audioExecutado) {
            avisarChegada(andarAtual);
            avisarPortaAberta();
            avisarAcessibilidade();
            avisarFechamento();
            audioExecutado = true;
        }

        return;
    } else {
        tft.fillRect(0, 210, 320, 25, TFT_BLACK);
    }

    // ---------------- PARADO ----------------
    if (!emMovimento) {
        audioExecutado = false;

        if (agora - millisPiscar >= 800) {
            millisPiscar = agora;
            mostrarNumero = !mostrarNumero;

            if (mostrarNumero) {
                tft.setTextSize(12);
                tft.setTextColor(TFT_GREEN, TFT_BLACK);
                tft.drawNumber(andarAtual, 125, 60);
            }
        }

        for (int i = 0; i < 3; i++) {
            if (digitalRead(pinBotoes[i]) == LOW) {
                andarDestino = i + 1;
                if (andarDestino != andarAtual) {
                    subindo = andarDestino > andarAtual;
                    emMovimento = true;
                    detectouPrimeiroIman = false;
                }
            }
        }
    }

    // ---------------- EM MOVIMENTO ----------------
    if (emMovimento) {
        digitalWrite(motorSobe, subindo);
        digitalWrite(motorDesce, !subindo);

        bool leitura = digitalRead(pinSensorCabina) == LOW;
        if (leitura && sensorUltimoEstado == HIGH) {
            if (!detectouPrimeiroIman) {
                detectouPrimeiroIman = true;
            } else {
                andarAtual += subindo ? 1 : -1;
                detectouPrimeiroIman = false;

                if (andarAtual == andarDestino) {
                    emMovimento = false;
                    enviarStatusAndar(andarAtual);
                } else {
                    enviarStatusAndar(andarAtual);
                }
            }
            delay(200);
        }
        sensorUltimoEstado = leitura;
    }
}