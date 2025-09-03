# High-Speed-File-Download---HTTPS_SPIFFS
The goal of this project is to create an ESP32 Dev Kit high-performance firmware solution that supports secure, high-speed HTTPS-based file downloads and optimum storage in the on-board SPI Flash File System (SPIFFS).

## 1. Install ESP-IDF in VS Code

Install VS Code

Download from official website (Visual Studio Code).

Install ESP-IDF Extension

Open VS Code → go to Extensions (Ctrl+Shift+X).

Search ESP-IDF → click install.

Install ESP-IDF Tools

After extension installs → click on the ESP-IDF logo on the left.

Select “Install ESP-IDF”.

Choose Express Install.

It will download ESP-IDF, Python, Ninja, CMake, etc. automatically.

Wait… it takes time but only once.

## 2. Create a New ESP32 Project

Open VS Code → ESP-IDF menu → “Create Project”.

Select template → hello_world (just for test).

Save the project in your folder (for you: C:\Users\acer\proj).

U can create project using command idf.py create-project my_project

## 3. Configure COM Port (Your ESP32 is on COM8)

Plug ESP32 with USB cable.

Open Device Manager → Ports (COM & LPT) → note COM number (mine is COM8).

In VS Code, open command palette (Ctrl+Shift+P) → type ESP-IDF: Select Port → choose COM8.
or use idf.py set-target esp32 // for controller select 
idf.py -p COM8 <action>

## 4. Build the Project

In VS Code, open Command Palette (Ctrl+Shift+P).

Run: ESP-IDF: Build Project. or "idf.py build" after opening respective folder
Wait till it compiles.

First build takes time.

If successful, you’ll see “Build finished”.

## 5. Flash the ESP32

Again Command Palette (Ctrl+Shift+P).

Select ESP-IDF: Flash . or "idf.py -p COM8 flash" 

It will erase old firmware and write the new one on ESP32 (COM8).

## 6. Monitor the Serial Output

After flashing → run ESP-IDF: Monitor. or idf.py -p COM8 monitor 

You’ll see live logs from ESP32.

Example: “Hello world” or your HTTPS download logs.

To stop, press Ctrl + ].

## 7. Change Configurations (menuconfig)

Open Command Palette (Ctrl+Shift+P). or start idf.py menuconig 

Run ESP-IDF: Menuconfig.

A terminal menu will open.

Here you can change:

WiFi SSID / Password

SPIFFS settings

Flash size, etc.

Press S to save → Q to quit.

After that → run build again.

## 8. Typical Flow Every Time

Edit code in main/.

idf.py build (or from VS Code build).

idf.py -p COM8 flash.

idf.py -p COM8 monitor.

## 9. For Your Project (HTTPS + SPIFFS)

Put your WiFi SSID & Password in code.

Flash the board.

Open monitor → you’ll see download progress.

Files will be saved into SPIFFS.
