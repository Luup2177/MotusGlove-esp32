# Lista de materiais (BOM)

[Voltar ao projeto](../README.md) · [Montagem e pinagem](montagem.md)

## Protótipo de quatro dedos

| Material | Quantidade | Especificação ou uso |
| --- | ---: | --- |
| ESP32 | 1 | Placa com Wi-Fi e quatro entradas analógicas para os sensores. O relatório registra duas placas de 30 pinos compradas para o projeto. |
| Módulo laser vermelho | 4 | Uma fonte de luz por dedo. Os módulos comprados são de 5 V, 650 nm e 6 mm. |
| LDR | 4 | Um sensor por dedo. O lote registrado no relatório é GL5528, de 5 mm. |
| Resistor | 4 | Um por divisor de tensão. O lote comprado é de 10 kΩ e ¼ W. Confirme o valor e a tolerância na montagem. |
| Tubo flexível | 4 trechos | Caminho óptico entre cada laser e LDR. O relatório cita PTFE no desenho inicial. Confirme o material e as dimensões dos tubos instalados. |
| Display OLED I²C | 1 | SSD1306. O firmware está configurado para 128 × 64 pixels, endereço `0x3C`. |
| Luva | 1 | Base para posicionar os canais ópticos. |
| Suporte dorsal | 1 | Organiza os quatro tubos. [Arquivo STL](../3D/Suporte_MotusGlove.stl). |
| Caixa | 1 | Peça modelada para acomodar a eletrônica. [Arquivo STL](../3D/Caixa_MotusGlove.stl). |
| Protoboard | 1 para bancada | MB-102 de 830 pontos, registrada na compra inicial. |
| Fios ou jumpers | Conforme a montagem | O relatório lista de 10 a 20 jumpers para a etapa de bancada. |
| Cabo USB de dados | 1 | Para alimentar a placa e gravar o firmware. Use o conector correspondente ao ESP32. |

As especificações de compra ajudam a identificar os componentes. A conferência física da montagem final deve registrar o modelo da placa, a alimentação do OLED e dos lasers, os resistores usados e como os tubos e fios foram fixados.

## Compras registradas na etapa de acompanhamento

O relatório registra as compras abaixo em **10/09/2026**. Os valores correspondem às compras da equipe, incluindo peças de reserva e itens de bancada.

| Item comprado | Quantidade comprada | Valor registrado |
| --- | --- | ---: |
| ESP32-WROOM, 30 pinos, USB-C | 1 placa | R$ 36,12 |
| ESP32, 30 pinos | 1 placa | R$ 35,88 |
| Resistores de 10 kΩ, ¼ W | 1 pacote com 20 | R$ 19,55 |
| LDR GL5528 de 5 mm | 1 kit com 10 | R$ 22,00 |
| Módulos laser vermelhos de 5 V, 650 nm, 6 mm | 1 kit com 5 | R$ 23,39 |
| Protoboard MB-102 de 830 pontos | 1 unidade | R$ 19,00 |
| **Desembolso registrado** | | **R$ 155,94** |

O relatório também estima **R$ 30,00** em materiais reaproveitados ou pré-existentes e **R$ 200,00** para a solução como um todo na etapa de acompanhamento. O custo final da montagem de quatro dedos ainda não foi consolidado.

*Fonte: relatório de acompanhamento da MotusGlove, seções 3, 6 e 6.1.*
