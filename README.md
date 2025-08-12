## G‑meter by bolino08c (ESP32‑S3 + LVGL, écran rond 480×480)

### Présentation
- **But**: Afficher en temps réel les accélérations (G) sur un écran rond 2.1" (480×480) avec historique visuel et pics.
- **Lib**: LVGL v8.x, affichage par canvas superposés, accéléromètre QMI8658.

### Matériel requis
- ** https://www.waveshare.com/esp32-s3-touch-lcd-2.1.htm?sku=28169
- **MCU**: ESP32‑S3 avec PSRAM recommandé 
- **Écran**: rond 2.1" 480×480 (driver ST7701)
- **Accéléromètre**: QMI8658 (I2C)

### Utilisation
- Au démarrage, le G‑meter s’affiche et lit automatiquement l’accéléromètre.
- Le point blanc montre la force G instantanée; la tache rouge accumule le parcours.
- Les labels affichent les pics (haut/bas/gauche/droite) et le pic total.
- **Appui long sur l’écran**: efface la tache rouge, remet les pics à zéro et lance un calibrage (double bip bref).

### Signaux sonores
- Un **double bip bref** est émis:
  - au premier démarrage après le calibrage initial,
  - et à chaque **toucher long sur l'écran** lorsque le reset + calibrage sont effectués.

### Calibration et orientation
- Le calibrage moyenne quelques échantillons puis détecte l’axe dominant de gravité (X/Y/Z).
- Le plan d’affichage s’adapte automatiquement: XY (à plat), XZ (vertical…), etc.

### Licence / Crédit
- G‑meter par bolino08c. Bibliothèques tierces sous leurs licences respectives.


