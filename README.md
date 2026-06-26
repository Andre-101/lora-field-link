# LoRaFieldLink

LoRaFieldLink es una interfaz web y un firmware para usar un ESP32-C3 con pantalla OLED como puente de campo hacia un módulo RAK3272-SIP LoRaWAN.

La página web permite enviar comandos AT al RAK3272 desde dos ambientes:

- **Celular Android:** conexión por Bluetooth Low Energy, con registro GPS del punto de campo.
- **Computador:** conexión por Serial USB, orientada a laboratorio, configuración y diagnóstico.

El firmware mantiene visible el estado básico del nodo en la pantalla OLED integrada: enlace BLE, respuesta del RAK, estado JOIN, contador de transmisión y último payload.

## Arquitectura

```text
Celular Android + Chrome
  -> Web Bluetooth
  -> ESP32-C3 OLED
  -> UART
  -> RAK3272-SIP
  -> LoRaWAN
  -> Gateway / TTN
```

```text
Computador + Chrome/Edge
  -> Web Serial por USB
  -> ESP32-C3 OLED
  -> UART
  -> RAK3272-SIP
  -> LoRaWAN
  -> Gateway / TTN
```

## Hardware

### Componentes

- ESP32-C3-OLED-0.42.
- RAK3272-SIP.
- Antena LoRa conectada al RAK3272-SIP.
- Celular Android con Chrome para uso en campo.
- Computador con Chrome o Edge para uso por Serial USB.

### Conexiones

```text
ESP32-C3 3.3V        -> RAK3272 3.3V
ESP32-C3 GND         -> RAK3272 GND
ESP32-C3 TX / GPIO21 -> RAK3272 UART2_RX / PA3
ESP32-C3 RX / GPIO20 <- RAK3272 UART2_TX / PA2
ESP32-C3 GPIO3       -> RAK3272 RST / PA12
```

Pantalla OLED integrada:

```text
SDA = GPIO5
SCL = GPIO6
Controlador = SSD1306 72x40 I2C
```

Notas eléctricas:

- Alimentar el RAK3272-SIP únicamente con 3.3 V.
- Compartir GND entre ESP32-C3 y RAK3272.
- Conectar la antena antes de transmitir.
- No conectar señales UART del RAK a lógica de 5 V.

## Firmware

Firmware principal:

```text
firmware/esp32c3_ble_uart_bridge/esp32c3_ble_uart_bridge.ino
```

### Ajustes de Arduino IDE

```text
USB CDC On Boot = Enabled
Serial Monitor = 115200
```

### Librerías requeridas

- `U8g2 by oliver`.
- Librerías BLE incluidas con el core ESP32 para Arduino.

### Parámetros modificables

El firmware está documentado para facilitar cambios futuros. Los valores más importantes están al inicio del archivo:

```cpp
#define RAK_RX_PIN 20
#define RAK_TX_PIN 21
#define RAK_RST_PIN 3
#define ENABLE_RAK_RESET 1
#define RAK_BAUD 115200

#define OLED_SDA_PIN 5
#define OLED_SCL_PIN 6
#define OLED_REFRESH_MS 1000

#define BLE_DEVICE_NAME "LoRaFieldLink-C3"
#define BLE_CHUNK_SIZE 20
```

Cambios comunes:

- Para mover el pin de reset del RAK, modificar `RAK_RST_PIN`.
- Para desactivar el reset automático del RAK, cambiar `ENABLE_RAK_RESET` a `0`.
- Para otra placa OLED, modificar `OLED_SDA_PIN`, `OLED_SCL_PIN` y, si aplica, el constructor U8g2.
- Para cambiar el nombre visible en Bluetooth, modificar `BLE_DEVICE_NAME`.

## Página web

Archivo principal:

```text
index.html
```

La página detecta automáticamente el ambiente:

- En celular, usa Bluetooth Low Energy y habilita el registro GPS.
- En computador, usa Web Serial por USB y oculta el flujo GPS.

URL de GitHub Pages:

```text
https://andre-101.github.io/lora-field-link/
```

## Uso en celular Android

1. Encender el nodo ESP32-C3 + RAK3272.
2. Abrir la página en Chrome Android.
3. Pulsar **Conectar BLE**.
4. Seleccionar `LoRaFieldLink-C3`.
5. Consultar JOIN con **Consultar JOIN**.
6. Si el módulo no está unido a TTN, usar **Unirse a TTN**.
7. Leer GPS.
8. Enviar punto.
9. Exportar el JSON de evidencia si se requiere respaldo local.

## Uso en computador

1. Conectar la ESP32-C3 por USB.
2. Abrir la página en Chrome o Edge.
3. Pulsar **Conectar Serial USB**.
4. Seleccionar el puerto de la ESP32-C3.
5. Enviar comandos AT desde los botones principales o desde el campo manual.

El modo computador está pensado para pruebas de laboratorio, configuración y diagnóstico. El flujo GPS se oculta porque normalmente el computador no representa una lectura de campo confiable.

## Comandos AT útiles

```text
AT
AT+NJS=?
AT+JOIN
AT+SEND=2:01
```

La configuración LoRaWAN del RAK3272, incluyendo DevEUI, JoinEUI, AppKey, región y modo OTAA/ABP, debe estar cargada previamente en el módulo. La página no guarda AppKey ni credenciales TTN.

## Pantalla OLED

La pantalla muestra estado compacto del nodo:

```text
LoRaFieldLink
BLE:OK RAK:OK
JOIN:YES TX:2
P:02 OK
```

Campos:

- `BLE`: indica si hay cliente BLE conectado.
- `RAK`: indica si se ha recibido alguna respuesta del RAK.
- `JOIN`: estado LoRaWAN detectado por respuestas `AT+NJS=?` o eventos de JOIN.
- `TX`: cantidad de comandos `AT+SEND` enviados.
- `P`: último payload hexadecimal enviado.
- Último resultado: `OK`, `ERROR`, `JOIN OK`, `NO JOIN`, `TX...`, entre otros.

## Seguridad

- La web no almacena AppKey ni credenciales TTN.
- Los registros se guardan localmente en el navegador hasta exportarlos o borrarlos.
- El payload enviado se construye desde la interfaz, pero la red LoRaWAN y la autenticación quedan bajo control del RAK3272-SIP.
