#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <esp_partition.h>
#include <time.h>

// Crie wifi_config.h a partir de wifi_config.example.h para configurar a rede.
#if __has_include("wifi_config.h")
#include "wifi_config.h"
#else
const char* WIFI_NOME = "";
const char* WIFI_SENHA = "";
#endif

const int OLED_ALTURA = 64;
const int OLED_ENDERECO = 0x3C;
const int TOTAL_DEDOS = 4;

struct Dedo {
  const char* sigla;
  int pino;
  int esticado;
  int dobrado;
  int leitura;
  int porcentagem;
};

// Os valores de 0% e 100% são medidos pelo painel e carregados da memória.
Dedo dedos[TOTAL_DEDOS] = {
  {"IND", 32, 0, 0, 0, -1},
  {"MED", 33, 0, 0, 0, -1},
  {"ANE", 34, 0, 0, 0, -1},
  {"MIN", 35, 0, 0, 0, -1}
};

const char* NOMES_DEDOS[] = {"Indicador", "Médio", "Anelar", "Mindinho"};
const int DIFERENCA_MINIMA = 30;  // Em unidades do sensor, para evitar calibrar com ruído.
const unsigned long TEMPO_CALIBRACAO = 120000;

struct Calibracao {
  int esticado[TOTAL_DEDOS];
  int dobrado[TOTAL_DEDOS];
};

Preferences memoria;
bool memoriaDisponivel = false;
unsigned long capturaEsticada = 0;
unsigned long sessaoCalibracao = 0;
Calibracao novaCalibracao = {};

WebServer servidor(80);
Adafruit_SSD1306 oled(128, OLED_ALTURA, &Wire, -1);

bool oledDisponivel = false;
bool oledLigado = false;
bool flashDisponivel = false;
bool wifiConectado = false;
bool jaConectou = false;
bool horaConfirmada = false;
bool avisouSemHora = false;
bool avisouFalhaWifi = false;

unsigned long ultimaLeitura = 0;
unsigned long ultimoSerial = 0;
unsigned long ultimaTentativaWifi = 0;
unsigned long momentoConexao = 0;

const char* LOG_ATUAL = "/eventos.txt";
const char* LOG_ANTERIOR = "/eventos-anteriores.txt";
const size_t LIMITE_LOG = 8192;

extern const char PAGINA[];

bool horaValida() {
  return time(nullptr) >= 1704067200;
}

void registrarEvento(const char* nivel, const String& mensagem) {
  char data[64];
  if (horaValida()) {
    time_t agora = time(nullptr);
    struct tm horaLocal;
    localtime_r(&agora, &horaLocal);
    strftime(data, sizeof(data), "%d/%m/%Y %H:%M:%S", &horaLocal);
  } else {
    snprintf(data, sizeof(data), "Hora nao sincronizada (+%lus)", millis() / 1000UL);
  }

  String linha = String(data) + " [" + nivel + "] " + mensagem;
  Serial.println(linha);
  if (!flashDisponivel) return;

  // Quando o arquivo atual enche, ele substitui o histórico mais antigo.
  File atual = LittleFS.open(LOG_ATUAL, "r");
  size_t tamanho = atual ? atual.size() : 0;
  atual.close();
  if (tamanho + linha.length() + 2 > LIMITE_LOG) {
    if ((LittleFS.exists(LOG_ANTERIOR) && !LittleFS.remove(LOG_ANTERIOR)) ||
        !LittleFS.rename(LOG_ATUAL, LOG_ANTERIOR)) {
      Serial.println("[ERRO] Falha ao rotacionar o log.");
      return;
    }
  }

  File arquivo = LittleFS.open(LOG_ATUAL, "a");
  if (!arquivo || arquivo.println(linha) < linha.length() + 2) {
    Serial.println("[ERRO] Falha ao gravar evento na flash.");
  }
  arquivo.close();
}

void lerDedos() {
  for (Dedo& dedo : dedos) {
    int soma = 0;
    // A média ajuda a reduzir oscilações na leitura.
    for (int amostra = 0; amostra < 8; amostra++) {
      soma += analogRead(dedo.pino);
    }
    dedo.leitura = soma / 8;

    if (dedo.esticado != dedo.dobrado) {
      long valor = map(dedo.leitura, dedo.esticado, dedo.dobrado, 0, 100);
      dedo.porcentagem = constrain(valor, 0L, 100L);
    }
  }
}

bool faixaValida(const Calibracao& calibracao, int i) {
  int inicio = calibracao.esticado[i];
  int fim = calibracao.dobrado[i];
  return inicio >= 0 && inicio <= 4095 && fim >= 0 && fim <= 4095 &&
         abs(fim - inicio) >= DIFERENCA_MINIMA;
}

void aplicarCalibracao(const Calibracao& calibracao) {
  for (int i = 0; i < TOTAL_DEDOS; i++) {
    dedos[i].esticado = calibracao.esticado[i];
    dedos[i].dobrado = calibracao.dobrado[i];
  }
}

void carregarCalibracao() {
  memoriaDisponivel = memoria.begin("motusglove", false);
  if (!memoriaDisponivel) {
    registrarEvento("ERRO", "Memoria da calibracao indisponivel.");
    return;
  }

  Calibracao salva;
  if (memoria.getBytesLength("cal-v1") != sizeof(salva) ||
      memoria.getBytes("cal-v1", &salva, sizeof(salva)) != sizeof(salva)) return;

  for (int i = 0; i < TOTAL_DEDOS; i++) {
    if (!faixaValida(salva, i)) return;
  }
  aplicarCalibracao(salva);
}

void atualizarDisplay() {
  if (!oledDisponivel || !oledLigado) return;

  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(0, 0);

  if (wifiConectado && millis() - momentoConexao < 4000) {
    oled.println("MOTUSGLOVE");
    oled.println("Abra no navegador:");
    oled.print(WiFi.localIP().toString());
    oled.display();
    return;
  }

  // A tela de 32 pixels de altura alterna os pares a cada 2,5 segundos.
  int primeiro = OLED_ALTURA == 32 ? ((millis() / 2500) % 2) * 2 : 0;
  int quantidade = OLED_ALTURA == 32 ? 2 : 4;
  for (int i = 0; i < quantidade; i++) {
    const Dedo& dedo = dedos[primeiro + i];
    int x = (i % 2) * 64 + 5;
    int y = (i / 2) * 32;
    oled.setTextSize(1);
    oled.setCursor(x, y + 2);
    oled.print(dedo.sigla);
    oled.setTextSize(2);
    oled.setCursor(x, y + 12);
    if (dedo.porcentagem < 0) {
      oled.print("--");
    } else {
      oled.print(dedo.porcentagem);
      oled.print('%');
    }
  }
  oled.display();
}

void atualizarWifi() {
  unsigned long agora = millis();
  bool conectado = WiFi.status() == WL_CONNECTED;

  if (conectado && !wifiConectado) {
    wifiConectado = true;
    momentoConexao = agora;
    avisouFalhaWifi = false;
    configTzTime("BRT3", "pool.ntp.org", "time.google.com");

    String mensagem = jaConectou ? "Wi-Fi reconectado. IP: " : "Wi-Fi conectado. IP: ";
    registrarEvento("INFO", mensagem + WiFi.localIP().toString());
    jaConectou = true;
  }

  if (!conectado && wifiConectado) {
    wifiConectado = false;
    registrarEvento("AVISO", "Conexao Wi-Fi perdida.");
  }

  // Tenta reconectar sem pausar a leitura dos sensores.
  if (!conectado && WIFI_NOME[0] && agora - ultimaTentativaWifi >= 15000) {
    ultimaTentativaWifi = agora;
    if (!avisouFalhaWifi) {
      registrarEvento("AVISO", "Sem Wi-Fi. Tentando conectar novamente.");
      avisouFalhaWifi = true;
    }
    WiFi.reconnect();
  }

  if (!horaConfirmada && horaValida()) {
    horaConfirmada = true;
    registrarEvento("INFO", "Relogio sincronizado.");
  }
  if (conectado && !horaConfirmada && !avisouSemHora && agora - momentoConexao >= 20000) {
    avisouSemHora = true;
    registrarEvento("AVISO", "Aguardando sincronizacao do horario.");
  }
}

void enviarDados() {
  String json = "{\"ip\":\"" + WiFi.localIP().toString();
  json += "\",\"mac\":\"" + WiFi.macAddress();
  json += "\",\"oledDisponivel\":";
  json += oledDisponivel ? "true" : "false";
  json += ",\"oledLigado\":";
  json += oledLigado ? "true" : "false";
  json += ",\"dedos\":[";

  for (int i = 0; i < TOTAL_DEDOS; i++) {
    if (i > 0) json += ',';
    json += "{\"valor\":" + String(dedos[i].porcentagem);
    json += ",\"bruto\":" + String(dedos[i].leitura) + '}';
  }

  json += "]}";
  servidor.sendHeader("Cache-Control", "no-store");
  servidor.send(200, "application/json", json);
}

void calibrarPeloPainel() {
  if (!memoriaDisponivel) {
    servidor.send(503, "text/plain; charset=utf-8", "Memória da calibração indisponível.");
    return;
  }

  String etapa = servidor.arg("etapa");
  if (etapa == "esticado") {
    lerDedos();
    for (int i = 0; i < TOTAL_DEDOS; i++) {
      novaCalibracao.esticado[i] = dedos[i].leitura;
    }
    capturaEsticada = millis();
    sessaoCalibracao++;
    servidor.send(200, "application/json", "{\"sessao\":" + String(sessaoCalibracao) + "}");
    return;
  }

  if (etapa != "dobrado" || sessaoCalibracao == 0 ||
      servidor.arg("sessao") != String(sessaoCalibracao) ||
      millis() - capturaEsticada >= TEMPO_CALIBRACAO) {
    servidor.send(409, "text/plain; charset=utf-8", "Registre 0% novamente para iniciar a calibração.");
    return;
  }

  lerDedos();
  String revisar;
  for (int i = 0; i < TOTAL_DEDOS; i++) {
    novaCalibracao.dobrado[i] = dedos[i].leitura;
    if (!faixaValida(novaCalibracao, i)) {
      if (revisar.length()) revisar += ", ";
      revisar += NOMES_DEDOS[i];
    }
  }
  if (revisar.length()) {
    servidor.send(422, "text/plain; charset=utf-8",
                  "A leitura mudou pouco em: " + revisar + ". Confira a montagem ou dobre mais e tente novamente.");
    return;
  }

  // Salva os quatro dedos juntos. A calibração anterior vale até esta gravação.
  if (memoria.putBytes("cal-v1", &novaCalibracao, sizeof(novaCalibracao)) != sizeof(novaCalibracao)) {
    registrarEvento("ERRO", "Falha ao salvar a calibracao.");
    servidor.send(500, "text/plain; charset=utf-8", "Não foi possível salvar. Tente novamente.");
    return;
  }

  aplicarCalibracao(novaCalibracao);
  // Expira esta captura para evitar salvar duas vezes o mesmo passo.
  capturaEsticada = millis() - TEMPO_CALIBRACAO;
  lerDedos();
  atualizarDisplay();
  registrarEvento("INFO", "Calibracao dos quatro dedos salva pelo painel.");
  enviarDados();
}

void controlarOled() {
  if (!oledDisponivel) {
    servidor.send(503, "text/plain", "OLED indisponivel.");
    return;
  }
  String estado = servidor.arg("estado");
  if (estado != "ligado" && estado != "desligado") {
    servidor.send(400, "text/plain", "Estado invalido.");
    return;
  }

  bool ligar = estado == "ligado";
  if (ligar != oledLigado) {
    oled.ssd1306_command(ligar ? SSD1306_DISPLAYON : SSD1306_DISPLAYOFF);
    oledLigado = ligar;
    registrarEvento("INFO", ligar ? "OLED ligado pelo painel." : "OLED desligado pelo painel.");
    atualizarDisplay();
  }
  enviarDados();
}

String lerArquivoLog(const char* caminho) {
  File arquivo = LittleFS.open(caminho, "r");
  if (!arquivo) return "";
  String texto = arquivo.readString();
  arquivo.close();
  return texto;
}

void enviarLogs() {
  if (!flashDisponivel) {
    servidor.send(503, "text/plain", "Armazenamento indisponivel. Consulte o Monitor Serial.");
    return;
  }

  String historico = lerArquivoLog(LOG_ANTERIOR);
  historico += lerArquivoLog(LOG_ATUAL);
  servidor.sendHeader("Cache-Control", "no-store");
  if (servidor.hasArg("baixar")) {
    servidor.sendHeader("Content-Disposition", "attachment; filename=MotusGlove-eventos.txt");
  }
  servidor.send(200, "text/plain; charset=utf-8", historico);
}

bool iniciarHistorico() {
  const char* nomeParticao = "spiffs";
  if (LittleFS.begin(false, "/littlefs", 10, nomeParticao)) return true;

  const esp_partition_t* particao = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, nomeParticao);
  if (!particao) {
    Serial.println("[ERRO] Particao spiffs ausente. Confira o esquema de particoes.");
    return false;
  }

  // Só inicializa uma partição inteiramente apagada. Uma falha ao montar
  // uma partição com dados não autoriza apagar o histórico anterior.
  uint8_t bloco[1024];
  for (size_t offset = 0; offset < particao->size; offset += sizeof(bloco)) {
    const size_t restante = particao->size - offset;
    const size_t tamanho = restante < sizeof(bloco) ? restante : sizeof(bloco);
    if (esp_partition_read(particao, offset, bloco, tamanho) != ESP_OK) {
      Serial.println("[ERRO] Nao foi possivel verificar a particao dos logs.");
      return false;
    }
    for (size_t i = 0; i < tamanho; i++) {
      if (bloco[i] != 0xFF) {
        Serial.println("[ERRO] Particao dos logs contem dados. Nenhuma formatacao realizada.");
        return false;
      }
    }
  }

  Serial.println("[INFO] Inicializando a particao vazia dos logs.");
  return LittleFS.begin(true, "/littlefs", 10, nomeParticao);
}

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  flashDisponivel = iniciarHistorico();
  if (!flashDisponivel) registrarEvento("ERRO", "Nao foi possivel montar o LittleFS.");
  registrarEvento("INFO", "MotusGlove iniciada.");
  carregarCalibracao();

  Wire.begin(21, 22);  // SDA e SCL.
  Wire.beginTransmission(OLED_ENDERECO);
  if (Wire.endTransmission() == 0) {
    oledDisponivel = oled.begin(SSD1306_SWITCHCAPVCC, OLED_ENDERECO, true, false);
  }
  oledLigado = oledDisponivel;
  if (!oledDisponivel) registrarEvento("ERRO", "OLED nao encontrado ou falha ao iniciar.");

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  if (WIFI_NOME[0]) {
    WiFi.begin(WIFI_NOME, WIFI_SENHA);
    ultimaTentativaWifi = millis();
  } else {
    registrarEvento("AVISO", "Preencha o nome e a senha do Wi-Fi no codigo.");
  }

  servidor.on("/", HTTP_GET, []() {
    servidor.send_P(200, "text/html; charset=utf-8", PAGINA);
  });
  servidor.on("/dados", HTTP_GET, enviarDados);
  servidor.on("/oled", HTTP_POST, controlarOled);
  servidor.on("/calibracao", HTTP_POST, calibrarPeloPainel);
  servidor.on("/logs", HTTP_GET, enviarLogs);
  servidor.begin();
}

void loop() {
  atualizarWifi();
  servidor.handleClient();

  unsigned long agora = millis();
  if (agora - ultimaLeitura >= 200) {
    ultimaLeitura = agora;
    lerDedos();
    atualizarDisplay();
  }
  if (agora - ultimoSerial >= 1000) {
    ultimoSerial = agora;
    for (const Dedo& dedo : dedos) {
      Serial.printf("%s: %d  ", dedo.sigla, dedo.leitura);
    }
    Serial.println();
  }
  delay(2);
}

// HTML, CSS e JavaScript do painel. O ESP32 envia tudo pela rede local.
const char PAGINA[] PROGMEM = R"MOTUS(
<!doctype html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>MotusGlove</title>
<style>
  * { box-sizing: border-box; }
  body { margin: 0; color: #7a1424; background: #fff8f5; font: 16px system-ui, sans-serif; }
  main { max-width: 800px; margin: auto; padding: 24px 16px; }
  header { background: linear-gradient(115deg,#661121,#a52538); color: white; padding: 20px; border-radius: 18px; }
  h1, h2, p { margin: 0 0 8px; }
  h1 span { color: #ffb097; }
  h2 { font-size: 1.15rem; }
  section { margin-top: 20px; }
  .grade { display: grid; grid-template-columns: repeat(2,minmax(0,1fr)); gap: 12px; }
  .cartao { background: white; border: 1px solid #fbe3df; border-radius: 14px; padding: 14px; }
  strong { display: block; font-size: 2rem; margin: 4px 0; }
  progress { width: 100%; accent-color: #e8693a; }
  small { display: block; margin-top: 5px; }
  .atrasado { opacity: .6; }
  button, a { display: inline-block; margin: 6px 4px 0 0; padding: 9px 12px; color: white; background: #7a1424; border: 0; border-radius: 8px; font: inherit; text-decoration: none; }
  button:disabled { opacity: .5; }
  pre { white-space: pre-wrap; overflow-wrap: anywhere; max-height: 260px; overflow: auto; }
</style>
</head>
<body>
<main>
  <header><h1>Motus<span>Glove</span></h1><p>Do trauma ao movimento.</p></header>
  <section>
    <h2>Movimento dos dedos</h2>
    <p id="estado" role="status">Conectando ao ESP32…</p>
    <div id="dedos" class="grade"></div>
  </section>
  <section class="cartao">
    <h2>Calibração</h2>
    <p>Com a luva vestida, estique os quatro dedos e registre 0%. Depois, dobre os quatro e registre 100%.</p>
    <button id="esticado" disabled>Registrar 0%</button>
    <button id="dobrado" disabled>Registrar 100%</button>
    <small id="calibracao" role="status"></small>
  </section>
  <section class="cartao">
    <h2>Dispositivo</h2>
    <p id="rede">IP e MAC aguardando conexão</p>
    <button id="oled" disabled>Controle do OLED</button>
    <small id="comando" role="status"></small>
  </section>
  <section class="cartao">
    <h2>Histórico do sistema</h2>
    <button id="logs">Atualizar histórico</button>
    <a href="/logs?baixar=1">Baixar log</a>
    <pre id="historico">Carregando…</pre>
  </section>
</main>
<script>
  const nomes = ['Indicador', 'Médio', 'Anelar', 'Mindinho'];
  const grade = document.querySelector('#dedos');
  const estado = document.querySelector('#estado');
  const botao = document.querySelector('#oled');
  const comando = document.querySelector('#comando');
  let conectado = false;
  let oledLigado = false;
  let oledDisponivel = false;
  let ocupado = false;
  let sessaoCalibracao = null;
  const botaoEsticado = document.querySelector('#esticado');
  const botaoDobrado = document.querySelector('#dobrado');
  const mensagemCalibracao = document.querySelector('#calibracao');

  grade.innerHTML = nomes.map(nome => `
    <div class="cartao">
      <span>${nome}</span>
      <strong>—</strong>
      <progress max="100" value="0" aria-label="${nome}"></progress>
      <small>Aguardando leitura</small>
    </div>
  `).join('');

  async function consultar(url, opcoes = {}) {
    const controle = new AbortController();
    const limite = setTimeout(() => controle.abort(), 3000);
    try {
      const resposta = await fetch(url, { ...opcoes, signal: controle.signal });
      if (!resposta.ok) {
        const erro = new Error(await resposta.text());
        erro.status = resposta.status;
        throw erro;
      }
      return url === '/logs' ? await resposta.text() : await resposta.json();
    } finally {
      clearTimeout(limite);
    }
  }

  function atualizarBotao() {
    botaoEsticado.disabled = !conectado || ocupado;
    botaoDobrado.disabled = !conectado || ocupado || sessaoCalibracao === null;
    botao.disabled = !conectado || !oledDisponivel || ocupado;
    botao.textContent = oledDisponivel
      ? (oledLigado ? 'Desligar OLED' : 'Ligar OLED')
      : 'OLED indisponível';
  }

  function mostrarDados(dados) {
    conectado = true;
    estado.textContent = 'Conectado · leituras ao vivo';
    grade.classList.remove('atrasado');
    document.querySelector('#rede').textContent = `IP ${dados.ip} · MAC ${dados.mac}`;
    dados.dedos.forEach((dedo, i) => {
      const cartao = grade.children[i];
      const calibrado = dedo.valor >= 0;
      cartao.querySelector('strong').textContent = calibrado ? `${dedo.valor}%` : '—';
      cartao.querySelector('progress').value = calibrado ? dedo.valor : 0;
      cartao.querySelector('small').textContent =
        calibrado ? `Sensor: ${dedo.bruto}` : `Calibração pendente · sensor: ${dedo.bruto}`;
    });
    oledLigado = dados.oledLigado;
    oledDisponivel = dados.oledDisponivel;
    atualizarBotao();
  }

  // Atualiza o painel periodicamente, sem recarregar a página.
  async function atualizarPainel() {
    try {
      if (!ocupado) {
        const dados = await consultar('/dados');
        if (!ocupado) mostrarDados(dados);
      }
    } catch {
      conectado = false;
      estado.textContent = 'Sem resposta do ESP32 · leituras desatualizadas';
      grade.classList.add('atrasado');
      atualizarBotao();
    } finally {
      setTimeout(atualizarPainel, 800);
    }
  }

  botao.onclick = async () => {
    ocupado = true;
    atualizarBotao();
    try {
      const dados = await consultar('/oled', {
        method: 'POST',
        body: new URLSearchParams({ estado: oledLigado ? 'desligado' : 'ligado' })
      });
      mostrarDados(dados);
      comando.textContent = oledLigado ? 'OLED ligado.' : 'OLED desligado.';
      carregarLogs();
    } catch {
      comando.textContent = 'O ESP32 não confirmou o comando.';
    } finally {
      ocupado = false;
      atualizarBotao();
    }
  };

  async function calibrar(etapa) {
    ocupado = true;
    atualizarBotao();
    try {
      const dados = await consultar('/calibracao', {
        method: 'POST',
        body: new URLSearchParams({ etapa, sessao: sessaoCalibracao ?? '' })
      });
      if (etapa === 'esticado') {
        sessaoCalibracao = dados.sessao;
        mensagemCalibracao.textContent = '0% registrado. Dobre os dedos e registre 100% em até dois minutos.';
      } else {
        sessaoCalibracao = null;
        mostrarDados(dados);
        mensagemCalibracao.textContent = 'Calibração salva para os quatro dedos.';
        carregarLogs();
      }
    } catch (erro) {
      if (erro.status === 409) sessaoCalibracao = null;
      mensagemCalibracao.textContent = erro.status ? erro.message : 'Sem confirmação do ESP32. Tente novamente.';
    } finally {
      ocupado = false;
      atualizarBotao();
    }
  }

  botaoEsticado.onclick = () => calibrar('esticado');
  botaoDobrado.onclick = () => calibrar('dobrado');

  async function carregarLogs() {
    const historico = document.querySelector('#historico');
    try {
      const texto = await consultar('/logs');
      historico.textContent = texto.trim().split('\n').reverse().join('\n') || 'Sem eventos.';
    } catch (erro) {
      historico.textContent = `Falha ao carregar o histórico. ${erro.message}`;
    }
  }

  document.querySelector('#logs').onclick = carregarLogs;
  atualizarPainel();
  carregarLogs();
</script>
</body>
</html>
)MOTUS";
