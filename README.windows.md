# Compilare l'eseguibile MobiDoc su Windows

Dal menu di `D:\CODICE\WORK\build-plugins.bat` scegli **4**. Compila i sorgenti
MOBIDOC con MSVC x64 e il KDE SDK gia' esistente in:

```text
D:\CODICE\SDK\KDE-WINDOWS
```

L'eseguibile compilato e' `D:\CODICE\INSTALLER\WINDOWS\MOBIDOC\MobiDoc.exe`.
Se generati, anche i simboli `MobiDoc.pdb` vengono copiati accanto all'eseguibile.
File intermedi e log di MOBIDOC sono in `D:\CODICE\INSTALLER\WINDOWS\MOBIDOC\.build-windows`.
Il menu non scarica un nuovo ambiente e non installa dipendenze; il codice
sorgente di Okular usa una cartella di build separata.

Per avviare la build con l'ambiente KDE necessario, dalla cartella MOBIDOC:

```bat
scripts\build-windows.bat -Run
```

Servono Visual Studio 2022 con strumenti C++ e Windows SDK. Questa fase produce
e prova l'eseguibile di sviluppo sul PC di compilazione. Per distribuirlo su altri
PC, dopo aver verificato la build, si prepara la cartella runtime completa e poi
l'installer Inno Setup. Il solo `.exe` non e' ancora il pacchetto distribuibile.

Per eseguire anche i test gia' selezionati:

```bat
scripts\build-windows.bat -RunTests
```
