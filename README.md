# SED_2526_RIEGO

Proyecto de un sistema de riego inteligente para la asignatura de Sistemas Empotrados Distribuidos.

## Miembros
- Francisco Mollá
- Roberto Narváez
- Francys Paucarima

## Vídeo demostrativo

https://youtu.be/DgtTSYaGMsQ

## Instrucciones de uso

Para ejecutar el proyecto se recomienda replicar las instrucciones dadas a continuación.

1. Cargar las variables de entorno: source ~/sed/esp-idf/export.sh
2. Compilar ambos proyectos: idf.py build. Ejecutar en las rutas /pFinalSensor y /pFinalActuador.
3. Ejecutarlos: idf.py flash monitor (la opción monitor es para mostrar el monitor serie).  Ejecutar en las rutas /pFinalSensor y /pFinalActuador.

Otros comandos necesarios para algunas acciones:

1. Para detener la visualización del monitor.
- Ctr + ]
3. Este comando debe ejecutarse después de realizar idf.py build de la versión de la que queremos el artefacto. Con este comando generamos el artefacto para la actualización OTA.
- mender-artifact write rootfs-image --compression none --device-type esp32 --artifact-name 1.0.1 --file build/pFinalActuador.bin --output-path update_v1.0.1.mender
3. Puede ser necesario activar y desactivar el contenedor de docker para el dashboard.
- docker stop mynodered
- docker start mynodered

## Hardware y Nodos

### Nodo Sensor
- ESP32 C3
- Sensor temperatura y humedad (si7021)
- Sensor humedad del suelo (Capaitive Soil Moisture Sensor v1.2) 
- Sensor de distancia (sharp 0A41sk)

### Nodo Actuador
- ESP32 V4
- Relé
- Led
- Bomba

