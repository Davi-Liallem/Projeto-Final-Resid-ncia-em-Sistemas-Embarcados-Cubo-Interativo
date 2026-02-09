# Projeto-Final-Residência-em-Sistemas-Embarcados-Cubo-Interativo
# Cubo Interativo de Foco e Emoções

Projeto desenvolvido no contexto do Programa EmbarcaTech, com o objetivo de criar um dispositivo interativo baseado em sistemas embarcados e Internet das Coisas (IoT), voltado ao estímulo do foco e da interação do usuário.

## 📌 Descrição Geral
O Cubo Interativo utiliza a placa BitDogLab (RP2040 W), sensores de movimento e som, atuadores visuais e sonoros, além de comunicação local e em nuvem, para coletar dados e fornecer feedback em tempo real.

O firmware é desenvolvido em linguagem C/C++ e utiliza o sistema operacional de tempo real FreeRTOS para gerenciamento de tarefas concorrentes.

## 🧠 Funcionalidades
- Detecção de orientação espacial do cubo (MPU6050)
- Jogos interativos com níveis e modos
- Feedback visual por LEDs e display OLED
- Feedback sonoro por buzzer
- Monitoramento local via UDP
- Envio de telemetria via MQTT para ThingsBoard

## 🧩 Arquitetura
- Microcontrolador: RP2040 W (BitDogLab / Pico W)
- Sistema operacional: FreeRTOS
- Sensores: MPU6050, microfone analógico
- Protocolos: UDP (local), MQTT (nuvem)

## 📂 Estrutura do Repositório
- `firmware/` – Código-fonte do sistema embarcado
- `docs/` – Diagramas e imagens do projeto

## 📜 Licença
Projeto de caráter acadêmico e experimental.

👨‍💻 Autor
Davi Liallem Passos dos Santos
Residência em Sistemas Embarcados – 2026

