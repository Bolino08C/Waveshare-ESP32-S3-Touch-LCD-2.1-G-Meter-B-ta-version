## G‑meter by bolino08c (ESP32‑S3 + LVGL, écran rond 480×480)

### Présentation
- **But**: Afficher en temps réel les accélérations (G) sur un écran rond 2.1" (480×480) avec historique visuel et pics.
- **Lib**: LVGL v8.x, affichage par canvas superposés, accéléromètre QMI8658.

### Matériel requis
- ** https://www.waveshare.com/esp32-s3-touch-lcd-2.1.htm?sku=28169
- **MCU**: ESP32‑S3 avec PSRAM recommandé 
- **Écran**: rond 2.1" 480×480 (driver ST7701)
- **Accéléromètre**: QMI8658 (I2C)

## Fonctionnalités principales

### Affichage
- **Point blanc** : Suit les mouvements du capteur en temps réel
- **Zone rouge** : Trace persistante montrant les zones parcourues
- **Grille circulaire** : Graduations de 0.5G à 2.0G
- **Valeurs maximales** : Affichage des pics sur chaque axe (avant, arrière, gauche, droite)
- **Pic total XYZ** : Magnitude maximale des forces G cumulées

### Calibration 0G intelligente
- **Calibration complète sur tous les axes** : X, Y, Z simultanément
- **Démarrage automatique** : Calibration moyenne sur 50 échantillons
- **Appui long** : Reset des valeurs max + recalibration depuis la position actuelle

### Contrôles
- **Appui long** : Reset des pics + recalibration 0G + double bip
- **Double bip** : Au démarrage et lors de l'appui long

## Architecture technique

### Capteur
- **QMI8658** : Capteur 6 axes (3 accéléromètre + 3 gyroscope)
- **Utilisation** : Seulement les 3 axes accéléromètre (X, Y, Z)
- **Fréquence** : Lecture toutes les 5ms
- **Précision** : Détection des inclinaisons résiduelles (0.1G)
- **Gyroscope** : Non utilisé dans cette version (peut être activé pour rotation)

## Calibration et axes

### Détection automatique de l'axe de gravité
- **Mode plat** : Gravité sur Z → plan XY affiché
- **Mode vertical** : Gravité sur Y → plan XZ affiché  
- **Mode rare** : Gravité sur X → plan YZ affiché

### Mapping dynamique des axes
- **Swap XY** : Échange des axes selon l'orientation
- **Inversion X** : Correction droite/gauche
- **Inversion Y** : Correction avant/arrière

- **Deltas calculés** : Mouvements relatifs à la position de référence

## Exemples de fonctionnement

### Scénario 1 : Démarrage avec inclinaison
```
Position initiale : 0.1G en X+, 0.2G en Y-
→ Calibration : Cette position devient le 0G de référence
→ Mouvement +0.5G en X : Affichage 0.4G (0.5 - 0.1)
→ Mouvement -0.3G en Y : Affichage -0.1G (-0.3 - (-0.2))
```

### Scénario 2 : Changement d'orientation
```
Écran à plat → Écran vertical
→ Détection : Aucune détection automatique pendant le fonctionnement
→ Adaptation : L'axe de gravité reste celui détecté au démarrage
→ Recalage : Seulement lors d'un appui long explicite
```


### Scénario 3 : Appui long de recalage
```
Écran incliné de 0.3G en X-
→ Appui long : Reset des pics + calibration depuis cette position
→ Résultat : Cette inclinaison devient le nouveau 0G
→ Mouvements : Relatifs à cette nouvelle référence
```

### Performance
- **Timer LVGL** : 20ms pour interface fluide
- **Driver loop** : 5ms pour lecture capteur rapide

### Logs et debug
- **Double bip** : Confirme la calibration
- **Valeurs affichées** : Vérifier la cohérence des axes
- **Point blanc** : Doit être visible et suivre les mouvements

### Licence / Crédit
- G‑meter par bolino08c. Bibliothèques tierces sous leurs licences respectives.


