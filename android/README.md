# T-Echo APRS — application Android

Application Android 8 à 15 (minSdk 26, targetSdk 35) qui pilote le T-Echo par Bluetooth LE.

- **SOTA** : spot vers APRS2SOTA. On choisit le sommet par recherche instantanée (« vign » →
  F/PO-001 Vignemale, « po-1 » → F/PO-001), en cascade association → région → sommet, ou
  « Près de moi » (GPS du T-Echo). Ensuite fréquence (avec préréglages), mode, indicatif /P avec
  préfixe éventuel et commentaire (QRV, QSY…). On suit l'accusé de réception et la réponse de la
  passerelle. Le plan peut être enregistré sur le T-Echo, qui l'affiche sur son écran.
- **Messages** : conversations APRS par correspondant, accusés (✓ émis, ✓✓ acquitté),
  notifications, lien de service qui garde la connexion en arrière-plan.
- **Réglages** : toute la configuration du nœud (mêmes clés et bornes que le configurateur web),
  état (batterie, GPS, radio), balise immédiate, redémarrage, réglages d'usine, console.
- **Langue** : anglais par défaut, français, ou langue du téléphone (Réglages → Application).
  Les textes sont dans `res/values/strings.xml` (anglais) et `res/values-fr/strings.xml` ;
  pour une nouvelle langue, ajouter un `values-xx/strings.xml` et son code dans `L.LANGS`.

## Ce qu'il faut côté firmware

Le service KISS (`00000001-ba2a-…`) transporte les trames radio. La configuration, l'envoi des
messages par le nœud (numérotés et répétés jusqu'à l'accusé) et le plan SOTA passent par le service
de configuration `f4e40001-7e5c-4b8a-9c1d-2a4f4c454b00` (lignes JSON du protocole USB, voir
`firmware/docs/protocol_config_v1.md`). Avec un firmware qui n'a que le KISS, l'app reçoit les
messages et envoie elle-même ses trames, mais n'a pas accès aux réglages.

À la première connexion, Android demande le PIN Bluetooth du T-Echo (`blePin`, 123456 par défaut).

## Compiler

```sh
cd android
./gradlew assembleDebug        # app/build/outputs/apk/debug/app-debug.apk
./gradlew testDebugUnitTest    # tests KISS / AX.25 / APRS / recherche
```

Il faut un JDK 17 et le SDK Android (plateforme 35), indiqué par `sdk.dir` dans
`local.properties`.

## Liste des sommets

`app/src/main/assets/summits.sota` est la liste officielle SOTA, réduite aux sommets valides.
L'app l'importe dans SQLite au premier lancement, et le bouton « Mettre à jour la liste » la
télécharge à nouveau depuis sota.org.uk (c'est le seul usage d'Internet). Pour régénérer l'asset :

```sh
curl -O https://storage.sota.org.uk/summitslist.csv
python3 tools/make_summits_asset.py summitslist.csv
```

## Organisation du code

| Dossier | Rôle |
|---|---|
| `proto/` | KISS, AX.25 ↔ texte TNC2, messages APRS, format du spot |
| `ble/TechoLink.kt` | GATT : scan, appairage, MTU, file d'opérations, reconnexion |
| `node/NodeClient.kt` | protocole JSON du nœud (get/set/status/msg/beacon…) |
| `data/` | SQLite : sommets SOTA, messages |
| `Techo.kt` | assemble le tout : trames reçues → conversations et accusés |
| `ui/` | écrans Compose (SOTA, Messages, Réglages, connexion) |
