#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

const char* NOME_REDE = "TesteSensor";
const char* SENHA_REDE = "teste123";

const int PINO_SENSOR = 34;
const int QUANTIDADE_AMOSTRAS = 20;

WebServer servidor(80);
Preferences memoria;

int leituraFiltrada = 0;
int valorAberto = 3000;
int valorFechado = 1000;

const char PAGINA_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Motus Glove</title>
  <style>
    * { box-sizing: border-box; }
    body {
      margin: 0;
      min-height: 100vh;
      display: grid;
      place-items: center;
      padding: 20px;
      background: #07111f;
      color: #e8f1ff;
      font-family: Arial, sans-serif;
    }
    .painel {
      width: min(100%, 520px);
      padding: 24px;
      border: 1px solid #26364d;
      border-radius: 18px;
      background: #101c2c;
      box-shadow: 0 16px 45px #0008;
    }
    h1 { margin: 0; font-size: 28px; }
    .subtitulo { margin: 7px 0 24px; color: #9eb1c9; }
    .estado {
      margin: 12px 0;
      padding: 20px;
      border-radius: 14px;
      text-align: center;
      background: #17263a;
    }
    .estado span { display: block; color: #9eb1c9; font-size: 13px; }
    .estado strong { display: block; margin-top: 8px; font-size: 27px; }
    .barra {
      height: 28px;
      margin: 16px 0 8px;
      overflow: hidden;
      border-radius: 999px;
      background: #07111f;
    }
    .nivel {
      width: 0%;
      height: 100%;
      border-radius: inherit;
      background: linear-gradient(90deg, #22c55e, #facc15, #ef4444);
      transition: width .2s ease;
    }
    .linha {
      display: flex;
      justify-content: space-between;
      gap: 10px;
      color: #9eb1c9;
      font-size: 14px;
    }
    .dados {
      display: grid;
      grid-template-columns: repeat(3, 1fr);
      gap: 10px;
      margin: 22px 0;
    }
    .dado {
      padding: 12px 6px;
      text-align: center;
      border-radius: 10px;
      background: #17263a;
    }
    .dado span { display: block; color: #9eb1c9; font-size: 12px; }
    .dado strong { display: block; margin-top: 6px; }
    .botoes { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
    button {
      border: 0;
      border-radius: 10px;
      padding: 14px 10px;
      color: white;
      font-weight: bold;
      background: #2563eb;
      cursor: pointer;
    }
    button:last-child { background: #dc2626; }
    .instrucao { margin: 18px 0 0; color: #9eb1c9; font-size: 13px; line-height: 1.5; }
  </style>
</head>
<body>
  <main class="painel">
    <h1>Motus Glove</h1>
    <p class="subtitulo">Teste do sensor optico</p>

    <section class="estado">
      <span>MOVIMENTO DETECTADO</span>
      <strong id="estado">Aguardando...</strong>
    </section>

    <div class="barra"><div class="nivel" id="nivel"></div></div>
    <div class="linha"><span>Tubo aberto</span><strong id="porcentagem">0%</strong><span>Tubo dobrado</span></div>

    <section class="dados">
      <div class="dado"><span>Leitura</span><strong id="leitura">0</strong></div>
      <div class="dado"><span>Aberto</span><strong id="aberto">0</strong></div>
      <div class="dado"><span>Fechado</span><strong id="fechado">0</strong></div>
    </section>

    <div class="botoes">
      <button onclick="calibrar('/calibrar-aberto')">Calibrar aberto</button>
      <button onclick="calibrar('/calibrar-fechado')">Calibrar dobrado</button>
    </div>

    <p class="instrucao">
      Primeiro deixe o tubo reto e toque em <b>Calibrar aberto</b>.
      Depois dobre o tubo ate a posicao maxima desejada e toque em
      <b>Calibrar dobrado</b>.
    </p>
  </main>

  <script>
    async function atualizar() {
      try {
        const resposta = await fetch('/dados');
        const dados = await resposta.json();
        document.getElementById('estado').textContent = dados.estado;
        document.getElementById('porcentagem').textContent = dados.percentual + '%';
        document.getElementById('nivel').style.width = dados.percentual + '%';
        document.getElementById('leitura').textContent = dados.leitura;
        document.getElementById('aberto').textContent = dados.aberto;
        document.getElementById('fechado').textContent = dados.fechado;
      } catch (erro) {
        document.getElementById('estado').textContent = 'Sem conexao';
      }
    }

    async function calibrar(endereco) {
      await fetch(endereco, { method: 'POST' });
      await atualizar();
    }

    setInterval(atualizar, 250);
    atualizar();
  </script>
</body>
</html>
)rawliteral";

int lerSensor(int amostras = QUANTIDADE_AMOSTRAS) {
  long soma = 0;

  for (int i = 0; i < amostras; i++) {
    soma += analogRead(PINO_SENSOR);
    delay(2);
  }

  return soma / amostras;
}

int calcularPercentual(int leitura) {
  int diferenca = valorFechado - valorAberto;

  if (abs(diferenca) < 50) {
    return 0;
  }

  float percentual = ((float)(leitura - valorAberto) / diferenca) * 100.0;
  return constrain((int)round(percentual), 0, 100);
}

String identificarMovimento(int percentual) {
  if (percentual < 25) return "ABERTO / RETO";
  if (percentual < 70) return "DOBRA PARCIAL";
  return "DOBRADO / FECHADO";
}

void enviarPagina() {
  servidor.send_P(200, "text/html; charset=utf-8", PAGINA_HTML);
}

void enviarDados() {
  int percentual = calcularPercentual(leituraFiltrada);
  String json = "{";
  json += "\"leitura\":" + String(leituraFiltrada) + ",";
  json += "\"aberto\":" + String(valorAberto) + ",";
  json += "\"fechado\":" + String(valorFechado) + ",";
  json += "\"percentual\":" + String(percentual) + ",";
  json += "\"estado\":\"" + identificarMovimento(percentual) + "\"";
  json += "}";

  servidor.send(200, "application/json", json);
}

void calibrarAberto() {
  valorAberto = lerSensor(100);
  memoria.putInt("aberto", valorAberto);
  servidor.send(200, "text/plain", "OK");
}

void calibrarFechado() {
  valorFechado = lerSensor(100);
  memoria.putInt("fechado", valorFechado);
  servidor.send(200, "text/plain", "OK");
}

void setup() {
  Serial.begin(115200);
  pinMode(PINO_SENSOR, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(PINO_SENSOR, ADC_11db);

  memoria.begin("motus-glove", false);
  valorAberto = memoria.getInt("aberto", 3000);
  valorFechado = memoria.getInt("fechado", 1000);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(NOME_REDE, SENHA_REDE);

  servidor.on("/", HTTP_GET, enviarPagina);
  servidor.on("/dados", HTTP_GET, enviarDados);
  servidor.on("/calibrar-aberto", HTTP_POST, calibrarAberto);
  servidor.on("/calibrar-fechado", HTTP_POST, calibrarFechado);
  servidor.onNotFound([]() {
    servidor.sendHeader("Location", "/");
    servidor.send(302, "text/plain", "");
  });
  servidor.begin();

  leituraFiltrada = lerSensor(50);

  Serial.println();
  Serial.println("Motus Glove iniciada.");
  Serial.println("Conecte-se a rede Wi-Fi: MotusGlove");
  Serial.println("Senha: motus123");
  Serial.println("Abra no navegador: http://192.168.4.1");
}

void loop() {
  servidor.handleClient();

  int novaLeitura = lerSensor();
  leituraFiltrada = (leituraFiltrada * 7 + novaLeitura * 3) / 10;

  static unsigned long ultimaImpressao = 0;
  if (millis() - ultimaImpressao >= 500) {
    ultimaImpressao = millis();
    int percentual = calcularPercentual(leituraFiltrada);

    Serial.print("Leitura: ");
    Serial.print(leituraFiltrada);
    Serial.print(" | Dobra: ");
    Serial.print(percentual);
    Serial.print("% | Estado: ");
    Serial.println(identificarMovimento(percentual));
  }
}
