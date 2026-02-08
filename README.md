# Projeto-Final-Residência-em-Sistemas-Embarcados-Cubo-Interativo
Sistema embarcado interativo com FreeRTOS e IoT para monitoramento de foco e emoções

📘 Projeto Final – Cubo Interativo de Foco e Emoções

Este repositório apresenta o desenvolvimento de um sistema embarcado interativo, utilizando FreeRTOS e Internet das Coisas (IoT), com o objetivo de apoiar atividades de estímulo ao foco, atenção e respostas emocionais por meio de interações físicas e sensoriais.
O projeto foi desenvolvido como Projeto Final da Residência em Sistemas Embarcados, integrando hardware, firmware e serviços de monitoramento local e em nuvem.

🧠 Funcionalidades Principais

Detecção de orientação do cubo (6 faces) via MPU6050
Modos de jogo interativos (nível 1, memória normal e memória rápida)
Aquisição de sinais de áudio por microfone (ADC + DMA)
Feedback visual (LEDs) e sonoro (buzzer)
Interface local via display OLED SSD1306
Monitoramento:
Local (Servidor UDP)
Remoto (ThingsBoard via MQTT)

🧩 Arquitetura do Sistema

Microcontrolador: RP2040 (Raspberry Pi Pico W)
Sistema Operacional: FreeRTOS
Protocolos: I2C, GPIO, ADC, UDP, MQTT, Wi-Fi
Plataformas de monitoramento:
Servidor local em Python (UDP)
Plataforma IoT ThingsBoard (nuvem)

📂 Organização do Repositório

app/ – Lógica principal do jogo
microfone/ – Aquisição e processamento de áudio
FreeRTOS/ – Kernel do sistema operacional
cubo_serve/ – Servidor UDP local e relatórios web
lib/ – Bibliotecas auxiliares
Imagens do Projeto/ – Fotos do protótipo e interfaces

👨‍💻 Autor
Davi Liallem Passos dos Santos
Residência em Sistemas Embarcados – 2026

