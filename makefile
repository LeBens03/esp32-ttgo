# ============================================
# Configuration Projet TTGO T-Display
# ============================================
VERSION = 0.1

# Nom du projet (doit correspondre au fichier .ino)
TARGET = main

# Bibliothèques Arduino standard utilisées
ARDLIBS = WiFi WebServer

# Bibliothèques de ~/Documents/Arduino/libraries/ ou ~/sketchbook/libraries/
USERLIBS = TFT_eSPI Button2 Adafruit_Sensor

# Bibliothèques locales (dans le répertoire du projet)
LOCALLIBS = libraries/TFT_eSPI libraries/Button2

# Modèle Arduino - IMPORTANT pour TTGO T-Display
MODEL ?= esp32

# Vitesse du port série
TERM_SPEED ?= 115200

# Port USB (détection automatique par défaut)
# Sous Linux: /dev/ttyUSB0 ou /dev/ttyACM0
# Sous macOS: /dev/tty.usbserial-* ou /dev/tty.SLAB_USBtoUART
# Sous Windows: COM3, COM4, etc.
# PORT ?= /dev/ttyUSB0

# Définitions supplémentaires
DEFINES ?=
