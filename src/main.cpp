#include <Arduino.h>
#include <NTPClient.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <DallasTemperature.h>
#include <OneWire.h>      //comunicação com dispositivos

//configuração do wifi
//const char *ssid = "Metropole"; C:\Users\Louise\Downloads\Arduinolib
//const char *password = "908070Metropole";
#define WIFI_SSID "Metropole"   //ultima alteração
#define WIFI_SENHA "908070Metropole" //última alteração
//configuração do mqtt
const char *mqtt_server = "localhost"; //ou 127.17.0.2 ip
const int mqtt_port = 1883;

WiFiClient espClient;
PubSubClient client (espClient);
WiFiUDP udp;
NTPClient ntp(udp, "a.st1.ntp.br", -3 * 3600, 60000); //servidor ntp
char temperatura[] = "temperatura"; //sensor lê
char movimento[] = "movimento";
char evaporadora[] = "evaporadora";
char condensadora[] = "condensadora";
char estado [] = "estado";
const char* topicData = "dataAtual";
const char* topicHora = "horaAtual";

char dataFormatada[64] = "data";
char horaFormatada[64] = "hora";

unsigned long enviaMQTT = millis() + 60000;    // tópicos são publicados a cada 1 minuto
unsigned long tempo = 600000; //10 minutos      
unsigned long ultimoGatilho = millis() + tempo; //pir
unsigned long last = 0;

float tempAtual = 0;
int tempIdeal = 24;
int data_semana = 0;
int Hliga = 8;
int Hdes = 18;
int movimentou = 0;
int ar = 0;

OneWire pino(32);  // cria instância da classe onewire e usa o pino 32 para comunicação
DallasTemperature barramento(&pino);  //cria instância barramento e barramento usa &pino para se comunicar
DeviceAddress sensor;                 //guarda endereço do sensor

struct tm data; // armazena data

const int pirPin1 = 33;
const int con = 25;
const int eva = 26;
const int ledCon = 22;
const int ledEva = 23;

void setupWiFi(){
  Serial.print("Conectando ao Wi-Fi");
  WiFi.begin(WIFI_SSID, WIFI_SENHA);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Conectado ao Wi-Fi");
  Serial.print("Endereço IP: ");
  Serial.println(WiFi.localIP());
}
void connectMQTT(){
  while (!client.connected()){
    Serial.print("Tentando se conectar ao MQTT");
    delay(2000);
  }
  if (client.connect("esp32Client")){
    Serial.println("Conectado ao MQTT");
  
    client.subscribe(temperatura);
    client.subscribe(movimento);
    client.subscribe(evaporadora);
    client.subscribe(condensadora);
    client.subscribe(topicData);
    client.subscribe(topicHora);
    client.subscribe(estado);

  } else {
    Serial.println("Falha na conexão ao MQTT");
    delay(2000);
  }
}
void obterDatahora(char *dataFormatada){
  time_t tt = time(NULL); //obtem tempo atual em segundos e armazena na variável tipo time_tt
  data = *gmtime(&tt);    //Converte o tempo atual para uma estrutura tm em UTC
  strftime(dataFormatada, 64, "%w - %d/%m/%y %H:%M:%S", &data); // hora completa
  int anoEsp = data.tm_year; //armazena o ano atual
  //tm representa o ano como o número de anos desde 1900
  if (anoEsp < 2020)
  {
    ntp.forceUpdate();  //atualizar
  } 
}
void publicarDatahora (PubSubClient &client, unsigned long&enviaMQTT) {
  char dataFormatada[64];
  obterDatahora(dataFormatada);
    if (millis () >= enviaMQTT){
      client.publish (topicData, dataFormatada);
      enviaMQTT = millis()+60000;
    }
}
void callback(char *topicc, byte *payload, unsigned int length){

}

void IRAM_ATTR mudaStatusPir()
{
  movimentou = 1;
  Serial.print("Teve movimento");
  client.publish(movimento, "Teve movimento");
}
void sensorTemp(){
    // busca temp enquanto estiver ligado
    barramento.requestTemperatures(); //barramento inicia a leitura
    float temperatura = barramento.getTempC(sensor); // obtém a temp do sensor
    if (temperatura > 0 && temperatura < 50)
    {
      tempAtual = temperatura;
      Serial.print(tempAtual);
      const char* temperatura = "tempAtual"; // Definindo o tópico corretamente
      char payload[8]; // Buffer para armazenar a string da temperatura
      // Formata a temperatura como uma string
        snprintf(payload, sizeof(payload), "%.2f", tempAtual); // Formata a temperatura com 2 casas decima
        if (millis() >= enviaMQTT){ //Se for maior ou igual a 1 minuto
          client.publish(temperatura, payload); // Publica a temperatura
          enviaMQTT = millis()+60000;
        }
    }  
} 
void arLiga() {
  if (digitalRead(eva) == 1)  // se a evaporadora desligada
  {
    digitalWrite(eva, 0);     //liga a evaporadora
    digitalWrite(ledEva, !digitalRead(eva));  //acende led
    client.publish(evaporadora, "ON");
  }
  else
  {
    if (tempAtual >= (tempIdeal + 1))   //se temperatura atual é maior ou igual a 25 
    { // quente
      if (digitalRead(eva) == 0)     // se eva ligada
      {  
        client.publish(evaporadora, "OFF");                  
        digitalWrite(con, 0);        // liga condensadora
        digitalWrite(ledCon, !digitalRead(con)); //acende led
        client.publish(condensadora, "ON");
      }
      else //se eva desligada
      {
        digitalWrite(eva, 0);      // liga
        digitalWrite(ledEva, !digitalRead(eva)); //liga led
        client.publish(evaporadora, "ON");
        digitalWrite(con, 0);      //liga
        digitalWrite(ledCon, !digitalRead(con));
        client.publish(condensadora, "ON");
      }
    }
    else if (tempAtual <= (tempIdeal - 1))
    { // frio
      digitalWrite(con, 1); //desliga condensadora
      digitalWrite(ledCon, !digitalRead(con)); //apaga led
      client.publish(condensadora, "OFF");
      digitalWrite(eva, 0); //liga evaporadora
      digitalWrite(ledEva, !digitalRead(eva));
      client.publish(evaporadora, "OFF");
    }
  }
  ar = 1;
  client.publish(estado, "Ar ligado");
}
void arDesliga (){
    digitalWrite(con, 1);
    digitalWrite(ledCon, !digitalRead(con));
    digitalWrite(eva, 1);
    digitalWrite(ledEva, !digitalRead(eva));
    ar = 0;
    client.publish(estado, "Ar desligado");
}
void pergunta(){
  int Hora = data.tm_hour;
  if (Hora >= Hliga && Hora < Hdes)
  { while (ultimoGatilho > millis()){
      arLiga();
} //while
} else {
    arDesliga();
}
} //pergunta
void setup() {
  Serial.begin (115200);
  pinMode(pirPin1, INPUT);
  pinMode(con, OUTPUT);
  pinMode(eva, OUTPUT);
  pinMode(ledCon, OUTPUT);
  pinMode(ledEva, OUTPUT);
  digitalWrite(eva, 1);
  digitalWrite(con, 1);

  // Conectar ao Wi-Fi
  // Configurar o cliente MQTT
  client.setServer(mqtt_server, mqtt_port);
  //client.setCallback(callback);
  ntp.begin();
  ntp.forceUpdate();
  if (!ntp.forceUpdate())
    {
      Serial.println("Erro ao carregar a hora");
      delay(1000);
    }
    else
    {
      timeval tv;                     // define a estrutura da hora
      tv.tv_sec = ntp.getEpochTime(); // segundos
      settimeofday(&tv, NULL);
      obterDatahora(dataFormatada);
    }

  // Conectar ao broker MQTT
  connectMQTT();
  attachInterrupt(digitalPinToInterrupt(pirPin1), mudaStatusPir, RISING);

}
void loop() {
  bool teste = false;
  if  (teste){
    Serial.print("teste");
  }
  connectMQTT();
  obterDatahora(dataFormatada);
  sensorTemp();
  publicarDatahora(client, enviaMQTT);
  client.loop();
  }