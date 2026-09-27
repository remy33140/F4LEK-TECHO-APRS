# Table SOTA (associations / régions) du firmware

L'assistant de spot SOTA (`src/sota.cpp`) choisit l'association et la région dans
`src/sota_regions_data.h`. Ce fichier est **généré** : ne pas l'éditer à la main.

| Fichier | Rôle |
|---|---|
| `fetch_sota_regions.py` | Télécharge depuis `api2.sota.org.uk` la liste des associations (`sota_associations.json`) puis les régions de chacune → `sota_regions.csv` |
| `sota_associations.json` | Liste des associations (source de `fetch`) |
| `sota_regions.csv` | Source de la table : `association,region,region_name` |
| `gen_sota_regions_data.py` | `sota_regions.csv` → `src/sota_regions_data.h` (codes seulement) |

## Mettre la table à jour

Depuis `firmware/` :

```bash
python3 tools/sota/fetch_sota_regions.py --refresh-associations   # ~227 requêtes, quelques minutes
python3 tools/sota/gen_sota_regions_data.py
```

Puis recompiler le firmware. Pour un test rapide sur quelques associations :
`python3 tools/sota/fetch_sota_regions.py --only F,EA1,EA2`.

Les récents et le re-spot sont stockés par **code** (`F`, `EA2`, `F/PE-103`), pas par
index : régénérer la table ne les décale pas.
