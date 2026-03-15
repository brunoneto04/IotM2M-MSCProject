# oneM2M-MCM-P2B


# Comandos MQTT

sudo apt-get install libssl-dev

sudo apt install mosquitto mosquitto-clients

sudo apt install libpaho-mqtt-dev

sudo systemctl status mosquitto


# Biblioteca CoAP
sudo apt install libpaho-mqtt-dev

# 1. Install required dependencies
sudo apt-get update
sudo apt-get install -y cmake build-essential git libcunit1-dev
# 2. Clone libcoap from GitHub
git clone https://github.com/obgm/libcoap.git
cd libcoap
# 3. Create build directory and configure
mkdir build
cd build	
cmake -DCMAKE_BUILD_TYPE=Release \
      -DENABLE_TESTS=OFF \
      -DENABLE_EXAMPLES=OFF \	
      -DENABLE_DOCUMENTATION=OFF \
      ..
# 4. Build and install
make
sudo make install
# 5. Update shared library cache
sudo ldconfig
# 6. Verify installation
pkg-config --modversion libcoap-3



# Comandos HTTP notificações

sudo apt install libcurl4-openssl-dev