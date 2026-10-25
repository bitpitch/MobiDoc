# Compilare su Mac Apple Silicon

Lo script prepara KDE Craft, scarica le dipendenze arm64, compila questa copia
dei sorgenti e genera un'app macOS con le librerie
incluse.

Servono gli strumenti da sviluppatore Apple, una connessione Internet e Python
3.11 o successivo per il primo avvio di Craft. Quando Craft è già installato,
lo script usa la sua copia di Python.

```sh
./scripts/build-macos.sh
```

L'app risultante è:

```text
dist/MobiDoc.app
```

Si può aprire con doppio clic o con:

```sh
open "dist/MobiDoc.app"
```

Si può anche copiare l'app nella cartella Applicazioni. L'app si chiama MobiDoc,
è identificata come `com.mobicod.mobidoc` e conserva i riferimenti di licenza e
attribuzione del progetto KDE Okular. La firma è locale, adatta all'uso su questo Mac.

Per ricompilare dopo una modifica, con le dipendenze già disponibili:

```sh
./scripts/build-macos.sh --skip-deps
```

L'ambiente, i file compilati e i log restano in `.build-macos/`, esclusa da Git.
I log principali sono `bootstrap.log`, `dependencies.log`, `build.log`,
`tests.log` e `package.log`. Anche `dist/` è esclusa da Git.

Il progetto usa il blueprint ufficiale di Okular in Craft, con l'aggiunta della
dipendenza QPDF richiesta da questa modifica. Per il bundle viene usato il
packager macOS di Craft, senza generare un'immagine DMG.

La build macOS attiva il profilo `MOBIDOC_PDF_ONLY`: mantiene Poppler, QPDF,
font e CMap, escludendo i generatori degli altri formati, la sintesi vocale,
la riproduzione audio/video incorporata e l'integrazione Share/Purpose.
KTextWidgets rimane disponibile per i campi dei moduli e gli editor delle
annotazioni. Se la copia installata include la sintesi vocale, il comando di
build la ricompila da sorgente con `WITH_TEXT_TO_SPEECH=OFF` prima di compilare
MobiDoc. Il log è `.build-macos/textwidgets-without-speech.log`.

Il grafo Craft viene controllato anche per le dipendenze di compilazione:
Ghostscript, LibSpectre, Qt Multimedia, Qt Speech e FFmpeg non devono risultare
necessari. I filtri comuni del packager macOS escludono i relativi componenti
prima della risoluzione delle librerie. Un secondo controllo dei collegamenti
Mach-O rifiuta un bundle che li contenga o li richieda, prima della firma e
della creazione di un eventuale DMG. La voce 12 del menu continua a produrre
l'installer PKG usando lo stesso bundle verificato.

Per controllare un bundle già compilato:

```sh
python3 scripts/audit-macos-pdf-bundle.py dist/MobiDoc.app
```

Il bundle usa il compilatore Apple ed esclude la compilazione di LLVM. Anche
l'integrazione opzionale con KWallet è disabilitata: su macOS può bloccare
l'apertura dei PDF dal Finder in attesa di una sessione DBus. Per i PDF cifrati
si inserisce la password all'apertura, senza memorizzarla in KWallet.

In MobiDoc, **Add Bookmark** aggiunge una voce all'indice del PDF.
Usare **Salva** per renderla persistente nel documento.

Trascinare i bookmark tra le righe per riordinarli, sopra un altro bookmark per
annidarli oppure nello spazio vuoto per riportarli al livello principale. Il
menu del tasto destro offre **Remove Bookmark** (rimuove anche i figli). Le
operazioni supportano Annulla/Ripristina. Usare **Salva** per conservarle nel PDF.

Per eseguire anche i test esistenti, aggiungere `--run-tests` allo script.
