# SED_2526_RIEGO

Proyecto de un sistema de riego inteligente para la asignatura de Sistemas Empotrados Distribuidos.

El sistema propone una solución de riego automatizado y eficiente basada en condiciones ambientales en tiempo real. Nuestro sistema permitiría evitar el desperdicio de agua por riegos programados y mejorar la salud de las plantas, al solo regar atendiendo a la realidad física del medio. Permitiría el riego autónomo, reduciendo la intervención humana. También permitiría el uso de luz artificial como fuente auxiliar de luz.

## Miembros
- Francisco Mollá
- Roberto Narváez
- Francys Paucarima

## Características principales

- Arquitectura distribuida basada en nodos independientes.
- Comunicación mediante MQTT.
- Dashboard de monitorización en Node-RED.
- Actualizaciones OTA mediante Mender.
- Recuperación automática ante pérdida de conexión.
- Gestión de errores y tolerancia a fallos.

## Vídeo demostrativo

https://youtu.be/DgtTSYaGMsQ

## Instrucciones de uso

### Requisitos previos

Antes de ejecutar el proyecto es necesario:

Tener instalado ESP-IDF.
Tener configuradas las variables de entorno.
Disponer de Docker instalado.
Tener acceso a un broker MQTT.
Tener configurado Mender para OTA.

### Comandos importantes

Para ejecutar el proyecto se recomienda replicar las instrucciones dadas a continuación.

1. Cargar las variables de entorno: source ~/sed/esp-idf/export.sh
2. Para el caso del nodo sensor utilizar: idf.py set-target esp32c3
3. Para el caso del nodo actuador utilizar: idf.py set-target esp32
4. Compilar ambos proyectos: idf.py build. Ejecutar en las rutas /pFinalSensor y /pFinalActuador.
5. Ejecutarlos: idf.py flash monitor (la opción monitor es para mostrar el monitor serie).  Ejecutar en las rutas /pFinalSensor y /pFinalActuador.

Otros comandos necesarios para algunas acciones:

1. Para detener la visualización del monitor:
 - Ctr + ]
2. Para la configuración inicial del proyecto (menuconfig):

Permite configurar parámetros del proyecto como credenciales Wi-Fi, configuración MQTT, particiones OTA y opciones del sistema.

- idf.py menuconfig
3. Este comando debe ejecutarse después de realizar idf.py build de la versión de la que queremos el artefacto. Con este comando generamos el artefacto para la actualización OTA:
- mender-artifact write rootfs-image --compression none --device-type esp32 --artifact-name 1.0.1 --file build/pFinalActuador.bin --output-path update_v1.0.1.mender
4. Puede ser necesario activar y desactivar el contenedor de docker para el dashboard:
- docker stop mynodered
- docker start mynodered
5. Para eliminar archivos temporales:
- idf.py clean
- idf.py fullclean
6. Útil para pruebas OTA o problemas de memoria (utilizar con ciudado):
- idf.py erase-flash
7. Compilación + flasheo simplificado:
- idf.py build flash monitor

## Hardware y Nodos

### Nodo Sensor
Responsable de capturar información del entorno y enviarla mediante MQTT.
- ESP32 C3
- Sensor temperatura y humedad (si7021)
- Sensor humedad del suelo (Capaitive Soil Moisture Sensor v1.2) 
- Sensor de distancia (sharp 0A41sk)

### Nodo Actuador
Responsable de activar el sistema de riego según las órdenes recibidas.
- ESP32 V4
- Relé
- Led
- Bomba

## Tecnologías utilizadas

- ESP-IDF
- MQTT
- Node-RED
- Docker
- Mender OTA
- ADC
- I2C
- UART


