# LoRaFieldLink Coverage

LoRaFieldLink Coverage es una interfaz web para medir cobertura LoRaWAN en campo usando:

- Celular Android con Chrome.
- ESP32-C3 OLED como puente BLE hacia UART.
- Módulo RAK3272-SIP.
- Comando `AT+LINKCHECK` para obtener RSSI/SNR.
- Google Apps Script + Google Sheets como base de datos.
- Leaflet + Leaflet.heat para visualizar el mapa de calor.

La página está orientada a mediciones por dispositivo. Al seleccionar `Dev-01`, el mapa carga solo los puntos guardados para `Dev-01`. Las hojas de Google Sheets se crean automáticamente cuando llega el primer dato de cada dispositivo.

## Flujo de uso

1. Abrir la página en Chrome Android.
2. Seleccionar el dispositivo, por ejemplo `Dev-01`.
3. Pulsar **Conectar BLE**.
4. La página verifica `AT+NJS=?` automáticamente.
5. Si el RAK no está unido, intenta `AT+JOIN` automáticamente.
6. Si JOIN falla, aparece **Reintentar JOIN**.
7. Cuando el estado LoRaWAN es correcto, se habilita **Tomar dato**.
8. **Tomar dato** ejecuta:
   - GPS del celular.
   - `AT+LINKCHECK=1`.
   - `AT+SEND=2:<payload>`.
   - espera `+EVT:LINKCHECK`.
   - extrae RSSI, SNR y cantidad de gateways.
   - guarda el dato en Google Sheets.
   - actualiza el mapa del dispositivo seleccionado.

## Configuración de GitHub Pages

El archivo `index.html` carga `config.js`. En producción, `config.js` se genera con GitHub Actions desde variables y secrets del repositorio.

Workflow:

```text
.github/workflows/deploy-pages.yml
```

Para usarlo, en GitHub configura Pages con fuente **GitHub Actions**.

### Repository variables

Crea estas variables en:

```text
Settings -> Secrets and variables -> Actions -> Variables
```

Variables recomendadas:

```text
LFL_APPS_SCRIPT_URL = https://script.google.com/macros/s/<DEPLOYMENT_ID>/exec
LFL_DEVICE_START = 1
LFL_DEVICE_END = 8
LFL_GATEWAY_LAT = 3.341264
LFL_GATEWAY_LON = -76.529448
LFL_MAP_ZOOM = 17
```

`LFL_APPS_SCRIPT_URL` no es el ID del Google Sheet. Es la URL de despliegue de la Web App de Apps Script, normalmente terminada en `/exec`.

### Repository secret

Crea este secret en:

```text
Settings -> Secrets and variables -> Actions -> Secrets
```

```text
LFL_API_TOKEN = <token-simple-para-la-api>
```

Nota: si un token se usa desde una página web estática, será visible para el navegador. Úsalo como barrera básica, no como secreto fuerte. La validación importante debe estar en Apps Script: formato del dispositivo, rangos de GPS, rangos de RSSI/SNR y límites de uso.

## Configuración local

Para probar sin GitHub Actions:

```bash
cp config.example.js config.js
```

Luego edita `config.js` con la URL de Apps Script y los valores de configuración.

## Hardware

```text
ESP32-C3 3.3V        -> RAK3272 3.3V
ESP32-C3 GND         -> RAK3272 GND
ESP32-C3 TX / GPIO21 -> RAK3272 UART2_RX / PA3
ESP32-C3 RX / GPIO20 <- RAK3272 UART2_TX / PA2
ESP32-C3 GPIO3       -> RAK3272 RST / PA12
```

OLED integrada:

```text
SDA = GPIO5
SCL = GPIO6
Controlador = SSD1306 72x40 I2C
```

## Firmware

Firmware principal:

```text
firmware/esp32c3_ble_uart_bridge/esp32c3_ble_uart_bridge.ino
```

Ajustes de Arduino IDE:

```text
USB CDC On Boot = Enabled
Serial Monitor = 115200
```

Librerías:

- `U8g2 by oliver`.
- BLE incluido con el core ESP32 para Arduino.

## Google Sheets

Apps Script crea una hoja por dispositivo al recibir el primer dato.

Ejemplos:

```text
Dev-01
Dev-02
Dev-03
...
```

Columnas por hoja:

```text
timestamp | lat | lon | rssi | snr | gw_count | payload
```

## API esperada

La web usa método GET con JSONP para evitar problemas de CORS en GitHub Pages.

Guardar muestra:

```text
?action=save_sample&dev_id=Dev-01&timestamp=...&lat=...&lon=...&rssi=-103&snr=4&gw_count=1&payload=0101&token=...
```

Leer puntos de un dispositivo:

```text
?action=get_points&dev_id=Dev-01&token=...
```

La respuesta debe usar el callback JSONP enviado por la página.

## Seguridad operativa

- La página no guarda AppKey ni credenciales TTN.
- El RAK debe estar configurado previamente en TTN.
- El token de la web no debe considerarse secreto fuerte.
- Apps Script debe validar el formato de `dev_id` y los rangos de los datos antes de escribir en Sheets.
