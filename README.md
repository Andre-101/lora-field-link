# LoRaFieldLink

LoRaFieldLink es una consola web mínima para pruebas de campo LoRaWAN con un celular Android, una ESP32-C3 y un módulo RAK3272-SIP.

## Objetivo de esta versión

Validar tres supuestos técnicos:

1. La página carga desde GitHub Pages.
2. El navegador puede leer el GPS del celular.
3. El navegador puede abrir el puerto serial USB de la ESP32-C3 mediante Web Serial y enviar comandos AT al RAK3272.

## Arquitectura

```text
Celular Android + Chrome
  -> GitHub Pages / LoRaFieldLink
  -> Web Serial por USB-C/OTG
  -> ESP32-C3
  -> UART
  -> RAK3272-SIP
  -> LoRaWAN
  -> Gateway / TTN
```

## Requisitos de prueba

- Android con Chrome actualizado.
- Cable USB-C/OTG.
- ESP32-C3 con firmware puente USB Serial <-> UART RAK.
- RAK3272-SIP conectado al UART de la ESP32-C3.
- RAK alimentado a 3.3 V y con antena conectada.
- Dispositivo LoRaWAN previamente configurado en TTN.

## Uso mínimo

1. Abrir la URL de GitHub Pages del repositorio.
2. Conectar la ESP32-C3 al celular.
3. Pulsar **Conectar serial**.
4. Seleccionar el dispositivo USB.
5. Enviar `AT` y esperar `OK`.
6. Pulsar **Leer GPS**.
7. Probar `AT+NJS=?`, `AT+JOIN` o `AT+SEND=2:01`.

## Seguridad

Esta versión no guarda credenciales TTN ni AppKey en la página. La web solo envía comandos seriales al RAK y registra información local de campo.
