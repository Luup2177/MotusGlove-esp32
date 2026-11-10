# API HTTP

[Voltar ao projeto](../README.md)

O ESP32 escuta na porta **80**. Os exemplos abaixo usam `192.168.1.50` como IP ilustrativo. Substitua pelo endereço exibido no Monitor Serial. O firmware serve a página e a API pelo mesmo endereço. Use uma rede local de teste, pois a API não tem autenticação nem HTTPS.

## Rotas e respostas

| Método e rota | Parâmetros | Resposta de sucesso |
| --- | --- | --- |
| `GET /` | — | `200` com o painel em HTML |
| `GET /dados` | — | `200` com o JSON de estado |
| `POST /calibracao` | `etapa=esticado` | `200` com o número da sessão em `sessao` |
| `POST /calibracao` | `etapa=dobrado` e `sessao` | `200` com o JSON de estado após salvar |
| `POST /oled` | `estado=ligado` ou `estado=desligado` | `200` com o JSON de estado atualizado |
| `GET /logs` | `baixar` opcional | `200` com o histórico em texto. Com o parâmetro, o navegador recebe um anexo |

Nos exemplos de POST, os parâmetros são enviados como `application/x-www-form-urlencoded`.

## Consultar o estado

```sh
curl http://192.168.1.50/dados
```

Exemplo ilustrativo de resposta:

```json
{
  "ip": "192.168.1.50",
  "mac": "02:00:00:00:00:01",
  "oledDisponivel": true,
  "oledLigado": true,
  "dedos": [
    { "valor": 18, "bruto": 1210 },
    { "valor": 42, "bruto": 1420 },
    { "valor": 51, "bruto": 1580 },
    { "valor": 77, "bruto": 1920 }
  ]
}
```

`dedos` contém indicador, médio, anelar e mindinho, nessa ordem. `bruto` é a média de oito leituras de ADC de 12 bits. `valor` é a flexão relativa de 0 a 100, ou `-1` antes de haver calibração válida. O cliente indica se está conectado conforme o sucesso das consultas.

## Controlar o OLED

```sh
curl -d "estado=desligado" http://192.168.1.50/oled
curl -d "estado=ligado" http://192.168.1.50/oled
```

O firmware envia o comando de ligar ou desligar ao SSD1306. A rota retorna `400` para estado inválido ou `503` se o display estiver indisponível.

## Calibrar os quatro dedos

```sh
curl -d "etapa=esticado" http://192.168.1.50/calibracao
curl -d "etapa=dobrado&sessao=1" http://192.168.1.50/calibracao
```

Estique os dedos na primeira chamada. Dobre-os na segunda e use o número de sessão que o dispositivo acabou de devolver, em até dois minutos. Cada canal precisa variar pelo menos 30 unidades do ADC. O firmware salva as referências dos quatro dedos juntas em `Preferences`.

| Código | Motivo |
| --- | --- |
| `409` | Etapa inválida, captura inicial ausente, sessão incorreta ou expirada |
| `422` | Variação insuficiente em um ou mais dedos |
| `500` | Falha na gravação da calibração |
| `503` | Memória da calibração indisponível |

## Consultar o histórico

```sh
curl http://192.168.1.50/logs
curl -o MotusGlove-eventos.txt "http://192.168.1.50/logs?baixar=1"
```

O histórico reúne o arquivo anterior e o atual, nessa ordem. O firmware rotaciona o arquivo atual ao atingir o limite de 8 KiB. Os eventos usam níveis INFO, AVISO e ERRO. Antes da sincronização NTP, o registro informa o tempo desde a inicialização. Na primeira inicialização, o firmware prepara a partição `spiffs` se ela estiver totalmente apagada. Se houver dados em uma partição que não abre, o firmware preserva esses dados. Se o LittleFS estiver indisponível, a rota retorna `503` e o Monitor Serial a 115200 baud informa o motivo.
